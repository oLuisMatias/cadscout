#include "ExtraFormats.h"

#include "Translate.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <zlib.h>

namespace {

using Bytes = std::vector<uint8_t>;

uint32_t rd16(const Bytes& b, size_t o) { return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8); }
uint32_t rd32(const Bytes& b, size_t o) { return rd16(b, o) | (rd16(b, o + 2) << 16); }

uint32_t quantize(float v) { return uint32_t(std::lround(std::fmin(std::fmax(v, 0.f), 1.f) * 255.f)); }

// ---------------------------------------------------------------- zip (3MF projects, and zip files holding models)

struct ZipEntry {
    std::string name;
    uint32_t flags = 0, method = 0, csize = 0, usize = 0, offset = 0;
};

// Raw deflate data -> `usize` bytes. False when the data is damaged.
bool inflateRaw(const uint8_t* src, uint32_t csize, uint32_t usize, Bytes& out) {
    // Deflate cannot shrink data by more than about 1000:1: a bigger claim in the header means a damaged file.
    if (usize > uint64_t(csize) * 1100 + 4096) return false;
    out.assign(usize, 0);
    if (usize == 0) return true;
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = csize;
    zs.next_out = out.data();
    zs.avail_out = usize;
    const int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return rc == Z_STREAM_END || (rc == Z_OK && zs.avail_out == 0);
}

bool isUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return false;
        i += n;
    }
    return true;
}

}  // namespace

// Windows' own code page conversion (declared here so this file does not need <windows.h> and its macros).
extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int codePage, unsigned long flags,
                                                                   const char* text, int size, wchar_t* out, int outSize);

namespace {

// Zip entry names are UTF-8 only when the archive says so (flag bit 11). Older tools, such as the zip folders of older
// Windows versions, store them in the DOS code page of the PC ("Peça.stp" then has a byte that is not valid UTF-8).
std::string zipNameToUtf8(const std::string& raw, uint32_t flags) {
    if ((flags & 0x800) || isUtf8(raw)) return raw;
    constexpr unsigned int kOemCodePage = 1;  // CP_OEMCP
    const int n = MultiByteToWideChar(kOemCodePage, 0, raw.data(), int(raw.size()), nullptr, 0);
    if (n <= 0) return raw;
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(kOemCodePage, 0, raw.data(), int(raw.size()), wide.data(), n);
    return QString::fromWCharArray(wide.data(), n).toUtf8().toStdString();
}

// Reads only the parts of the archive that are needed (the file may be large). `invalidText` is the message for a damaged
// or non-zip file (3MF files say "The 3MF file ...").
struct ZipFile {
    std::ifstream in;
    uint64_t size = 0;
    std::vector<ZipEntry> entries;
    const char* invalidText;

    std::runtime_error invalid() const { return std::runtime_error(tr8(invalidText)); }

    void readAt(uint64_t offset, void* dst, size_t n) {
        in.clear();
        in.seekg(std::streamoff(offset));
        in.read(static_cast<char*>(dst), std::streamsize(n));
        if (in.gcount() != std::streamsize(n)) throw invalid();
    }

    explicit ZipFile(const std::string& path, const char* invalidMessage = "The zip file could not be read.")
        : in(std::filesystem::u8path(path), std::ios::binary), invalidText(invalidMessage) {
        if (!in) throw std::runtime_error(tr8("Could not read the file."));
        in.seekg(0, std::ios::end);
        size = uint64_t(in.tellg());
        if (size < 22) throw invalid();
        const uint64_t tailLen = std::min<uint64_t>(size, 22 + 65535);
        Bytes tail(tailLen);
        readAt(size - tailLen, tail.data(), tail.size());
        size_t eocd = std::string::npos;
        for (size_t i = tail.size() - 22;; --i) {
            if (rd32(tail, i) == 0x06054b50) { eocd = i; break; }
            if (i == 0) break;
        }
        if (eocd == std::string::npos) throw invalid();
        const uint32_t count = rd16(tail, eocd + 10), cdSize = rd32(tail, eocd + 12), cdOffset = rd32(tail, eocd + 16);
        if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu)
            throw std::runtime_error(tr8("Zip files larger than 4 GB are not supported."));
        if (uint64_t(cdOffset) + cdSize > size) throw invalid();
        Bytes cd(cdSize);
        readAt(cdOffset, cd.data(), cd.size());
        size_t p = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (p + 46 > cd.size() || rd32(cd, p) != 0x02014b50) throw invalid();
            ZipEntry e;
            e.flags = rd16(cd, p + 8);
            e.method = rd16(cd, p + 10);
            e.csize = rd32(cd, p + 20);
            e.usize = rd32(cd, p + 24);
            e.offset = rd32(cd, p + 42);
            if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu || e.offset == 0xFFFFFFFFu)
                throw std::runtime_error(tr8("Zip files larger than 4 GB are not supported."));
            const size_t nameLen = rd16(cd, p + 28), extraLen = rd16(cd, p + 30), commentLen = rd16(cd, p + 32);
            if (p + 46 + nameLen > cd.size()) throw invalid();
            e.name = zipNameToUtf8(std::string(reinterpret_cast<const char*>(cd.data() + p + 46), nameLen), e.flags);
            std::replace(e.name.begin(), e.name.end(), '\\', '/');
            entries.push_back(std::move(e));
            p += 46 + nameLen + extraLen + commentLen;
        }
    }

    Bytes read(const ZipEntry& e) {
        if (e.flags & 1) throw std::runtime_error(tr8("This zip file is protected with a password."));
        uint8_t hdr[30];
        readAt(e.offset, hdr, sizeof hdr);
        const Bytes h(hdr, hdr + 30);
        if (rd32(h, 0) != 0x04034b50) throw invalid();
        const uint64_t start = uint64_t(e.offset) + 30 + rd16(h, 26) + rd16(h, 28);
        if (start + e.csize > size) throw invalid();
        Bytes data(e.csize);
        readAt(start, data.data(), data.size());
        if (e.method == 0) return data;
        if (e.method != 8) throw std::runtime_error(tr8("The zip file uses a compression method that is not supported."));
        Bytes out;
        if (!inflateRaw(data.data(), e.csize, e.usize, out)) throw invalid();
        return out;
    }
};

std::string lowerExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.'), slash = name.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool isJunkEntry(const std::string& name) {
    return name.empty() || name.back() == '/' || name.rfind("__MACOSX/", 0) == 0 || name.find("/._") != std::string::npos ||
           name.rfind("._", 0) == 0;
}

// ---------------------------------------------------------------- 3MF (plain, and Bambu Studio / Orca Slicer projects)

// Row-vector convention as in the 3MF spec: p' = p * [m0 m1 m2; m3 m4 m5; m6 m7 m8] + [m9 m10 m11].
using Mat = std::array<double, 12>;
const Mat kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

Vec3 apply(const Mat& m, const Vec3& p) {
    return {p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9], p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10],
            p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11]};
}

// The transform that applies `a` first and then `b`.
Mat compose(const Mat& a, const Mat& b) {
    Mat r{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r[size_t(row * 3 + col)] = a[size_t(row * 3)] * b[size_t(col)] + a[size_t(row * 3 + 1)] * b[size_t(3 + col)] +
                                       a[size_t(row * 3 + 2)] * b[size_t(6 + col)];
    for (int col = 0; col < 3; ++col)
        r[size_t(9 + col)] = a[9] * b[size_t(col)] + a[10] * b[size_t(3 + col)] + a[11] * b[size_t(6 + col)] +
                             b[size_t(9 + col)];
    return r;
}

double determinant(const Mat& m) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}

bool isSpaceChar(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

int toInt(std::string_view s, int def = -1) {
    int v = def;
    if (s.empty()) return def;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() ? v : def;
}

double toDouble(std::string_view s, double def = 0.0) {
    double v = def;
    if (s.empty()) return def;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() ? v : def;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// XML text -> plain UTF-8: the named entities and numeric ones (&#233; &#xE9;).
std::string unescapeXml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const size_t semi = s.find(';', i);
            if (semi != std::string_view::npos && semi - i <= 10) {
                const std::string_view ent = s.substr(i + 1, semi - i - 1);
                char rep = 0;
                if (ent == "amp") rep = '&';
                else if (ent == "lt") rep = '<';
                else if (ent == "gt") rep = '>';
                else if (ent == "quot") rep = '"';
                else if (ent == "apos") rep = '\'';
                if (rep) { out += rep; i = semi; continue; }
                if (ent.size() >= 2 && ent[0] == '#') {
                    const bool hex = ent[1] == 'x' || ent[1] == 'X';
                    const std::string_view digits = ent.substr(hex ? 2 : 1);
                    uint32_t cp = 0;
                    const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), cp, hex ? 16 : 10);
                    if (r.ec == std::errc() && r.ptr == digits.data() + digits.size() && cp > 0 && cp <= 0x10FFFF &&
                        (cp < 0xD800 || cp > 0xDFFF)) {
                        appendUtf8(out, cp);
                        i = semi;
                        continue;
                    }
                }
            }
        }
        out += s[i];
    }
    return out;
}

// Descriptions from model websites (MakerWorld, Printables) are web pages: keep the text, turn paragraphs and line breaks
// into new lines, and drop the other markup.
std::string htmlToText(const std::string& html) {
    // Plain text (no tag such as <p> or </b>) keeps its own line breaks, even when it contains '&' or '<'.
    bool hasTag = false;
    for (size_t p = html.find('<'); p != std::string::npos && !hasTag; p = html.find('<', p + 1))
        hasTag = p + 1 < html.size() &&
                 (std::isalpha(static_cast<unsigned char>(html[p + 1])) || html[p + 1] == '/' || html[p + 1] == '!') &&
                 html.find('>', p) != std::string::npos;
    if (!hasTag) return html;
    std::string out;
    // Like a browser: spaces and line breaks of the page source count as one space; only <br>, </p>, ... start a new line.
    auto newLine = [&] {
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (!out.empty() && out.back() != '\n') out += '\n';
    };
    for (size_t i = 0; i < html.size();) {
        const char c = html[i];
        const bool tagStart = c == '<' && i + 1 < html.size() &&
                              (std::isalpha(static_cast<unsigned char>(html[i + 1])) || html[i + 1] == '/' || html[i + 1] == '!');
        const size_t close = tagStart ? html.find('>', i) : std::string::npos;
        if (close == std::string::npos) {  // ordinary text (a lone '<' as in "<5 mm" stays)
            if (isSpaceChar(c)) {
                if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
            } else {
                out += c;
            }
            ++i;
            continue;
        }
        std::string tag = html.substr(i + 1, close - i - 1);
        for (char& t : tag) t = char(std::tolower(static_cast<unsigned char>(t)));
        if (tag.rfind("br", 0) == 0 || tag == "/p" || tag == "/div" || tag == "/li" || tag.rfind("/h", 0) == 0) newLine();
        i = close + 1;
    }
    // The entities of the page itself: the common HTML ones first (website editors write &nbsp; and typographic marks),
    // then the XML ones and numeric ones.
    static const std::pair<const char*, const char*> kHtmlEntities[] = {
        {"&nbsp;", " "},        {"&ndash;", "–"}, {"&mdash;", "—"}, {"&hellip;", "…"},
        {"&lsquo;", "‘"},  {"&rsquo;", "’"}, {"&ldquo;", "“"}, {"&rdquo;", "”"},
        {"&deg;", "°"},    {"&copy;", "©"},  {"&reg;", "®"},   {"&trade;", "™"},
        {"&times;", "×"},  {"&plusmn;", "±"}, {"&euro;", "€"}, {"&bull;", "•"},
        {"&middot;", "·"}, {"&frac12;", "½"}};
    for (const auto& [entity, text] : kHtmlEntities)
        for (size_t pos = out.find(entity); pos != std::string::npos; pos = out.find(entity, pos + std::strlen(text)))
            out.replace(pos, std::strlen(entity), text);
    out = unescapeXml(out);
    // Trim every line (a &nbsp; at a line end becomes a space) and drop empty lines at the start and end.
    std::string trimmed;
    size_t start = 0;
    while (start <= out.size()) {
        const size_t end = std::min(out.find('\n', start), out.size());
        std::string_view line = std::string_view(out).substr(start, end - start);
        while (!line.empty() && isSpaceChar(line.front())) line.remove_prefix(1);
        while (!line.empty() && isSpaceChar(line.back())) line.remove_suffix(1);
        if (!line.empty()) trimmed += (trimmed.empty() ? "" : "\n") + std::string(line);
        start = end + 1;
    }
    return trimmed;
}

std::string_view trimView(std::string_view s) {
    while (!s.empty() && isSpaceChar(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpaceChar(s.back())) s.remove_suffix(1);
    return s;
}

Mat parseMat(std::string_view s) {
    Mat m = kIdentity;
    Mat tmp{};
    size_t n = 0;
    const char* p = s.data();
    const char* end = s.data() + s.size();
    while (p < end && n < 12) {
        while (p < end && isSpaceChar(*p)) ++p;
        if (p >= end) break;
        const auto r = std::from_chars(p, end, tmp[n]);
        if (r.ec != std::errc()) return m;
        p = r.ptr;
        ++n;
    }
    return n == 12 ? tmp : m;
}

bool parseHexColor(std::string_view text, std::array<float, 3>& out) {
    std::string_view h = trimView(text);
    if (!h.empty() && h.front() == '#') h.remove_prefix(1);
    if (h.size() != 6 && h.size() != 8) return false;
    unsigned v = 0;
    for (size_t i = 0; i < 6; ++i) {
        const char c = h[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') d = unsigned(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = unsigned(c - 'A' + 10);
        else return false;
        v = v * 16 + d;
    }
    out = {float((v >> 16) & 255) / 255.f, float((v >> 8) & 255) / 255.f, float(v & 255) / 255.f};
    return true;
}

int packColor(const std::array<float, 3>& c) {
    return int((quantize(c[0]) << 16) | (quantize(c[1]) << 8) | quantize(c[2]));
}

// A small, fast XML tokenizer. Bambu/Orca object files are tens of megabytes, so a general XML reader is too slow.
// Names have their namespace prefix removed; attribute values are raw (not unescaped).
struct XmlTag {
    std::string_view name;
    bool closing = false, selfClosing = false;
    std::vector<std::pair<std::string_view, std::string_view>> attrs;
    std::string_view get(std::string_view key) const {
        for (const auto& a : attrs)
            if (a.first == key) return a.second;
        return {};
    }
};

class XmlTokenizer {
public:
    explicit XmlTokenizer(const Bytes& b)
        : p_(reinterpret_cast<const char*>(b.data())), end_(reinterpret_cast<const char*>(b.data()) + b.size()) {}

    bool next(XmlTag& t) {
        t.attrs.clear();
        t.closing = t.selfClosing = false;
        for (;;) {
            if (p_ >= end_) return false;
            const void* lt = std::memchr(p_, '<', size_t(end_ - p_));
            if (!lt) { p_ = end_; return false; }
            p_ = static_cast<const char*>(lt) + 1;
            if (p_ >= end_) return false;
            if (*p_ == '?') { skipPast("?>"); continue; }
            if (*p_ == '!') {
                if (end_ - p_ >= 3 && p_[1] == '-' && p_[2] == '-') skipPast("-->");
                else skipPast(">");
                continue;
            }
            break;
        }
        if (*p_ == '/') { t.closing = true; ++p_; }
        const char* s = p_;
        while (p_ < end_ && !isSpaceChar(*p_) && *p_ != '>' && *p_ != '/') ++p_;
        t.name = localName(std::string_view(s, size_t(p_ - s)));
        while (p_ < end_) {
            while (p_ < end_ && isSpaceChar(*p_)) ++p_;
            if (p_ >= end_) break;
            if (*p_ == '>') { ++p_; break; }
            if (*p_ == '/') { t.selfClosing = true; ++p_; continue; }
            const char* a = p_;
            while (p_ < end_ && *p_ != '=' && !isSpaceChar(*p_) && *p_ != '>' && *p_ != '/') ++p_;
            const std::string_view attrName = localName(std::string_view(a, size_t(p_ - a)));
            while (p_ < end_ && isSpaceChar(*p_)) ++p_;
            std::string_view value;
            if (p_ < end_ && *p_ == '=') {
                ++p_;
                while (p_ < end_ && isSpaceChar(*p_)) ++p_;
                if (p_ < end_ && (*p_ == '"' || *p_ == '\'')) {
                    const char quote = *p_++;
                    const char* v = p_;
                    while (p_ < end_ && *p_ != quote) ++p_;
                    value = std::string_view(v, size_t(p_ - v));
                    if (p_ < end_) ++p_;
                }
            }
            if (!attrName.empty()) t.attrs.emplace_back(attrName, value);
        }
        return true;
    }

    // Text between the end of the last start tag and the next '<'.
    std::string_view textAfterTag() const {
        const void* lt = p_ < end_ ? std::memchr(p_, '<', size_t(end_ - p_)) : nullptr;
        return std::string_view(p_, lt ? size_t(static_cast<const char*>(lt) - p_) : size_t(end_ - p_));
    }

private:
    static std::string_view localName(std::string_view s) {
        const size_t colon = s.rfind(':');
        return colon == std::string_view::npos ? s : s.substr(colon + 1);
    }
    void skipPast(std::string_view pattern) {
        const std::string_view rest(p_, size_t(end_ - p_));
        const size_t i = rest.find(pattern);
        p_ = i == std::string_view::npos ? end_ : p_ + i + pattern.size();
    }
    const char* p_;
    const char* end_;
};

struct Obj3mf {
    struct Tri {
        int v[3] = {0, 0, 0};
        int pid = -1, p1 = -1;
        int paint = -1;  // index into ModelFile::paintStrings (Bambu/Orca per-triangle colour painting)
    };
    struct Comp {
        std::string path;  // empty: same file
        int object = 0;
        Mat transform = kIdentity;
    };
    std::string name;
    std::vector<Vec3> verts;
    std::vector<Tri> tris;
    std::vector<Comp> comps;
    int pid = -1, pindex = -1;
};

struct Item {
    int object = -1;
    Mat transform = kIdentity;
};

struct ModelFile {  // one parsed *.model file
    std::map<int, Obj3mf> objects;
    std::map<int, std::vector<std::array<float, 3>>> colorGroups;
    std::vector<Item> items;
    std::vector<std::string> paintStrings;
    std::string application;
    std::map<std::string, std::string> metadata;  // <metadata name="..."> entries of the file (title, designer, dates, ...)
    double unit = 1.0;  // to millimetres
};

ModelFile parseModelXml(const Bytes& xml) {
    ModelFile f;
    std::map<std::string, int> paintIndex;
    XmlTokenizer tk(xml);
    XmlTag t;
    Obj3mf* current = nullptr;
    int group = -1;
    while (tk.next(t)) {
        const std::string_view n = t.name;
        if (t.closing) {
            if (n == "object") current = nullptr;
            else if (n == "basematerials" || n == "colorgroup") group = -1;
            continue;
        }
        if (n == "triangle") {
            if (!current) continue;
            Obj3mf::Tri tri;
            tri.v[0] = toInt(t.get("v1"), 0);
            tri.v[1] = toInt(t.get("v2"), 0);
            tri.v[2] = toInt(t.get("v3"), 0);
            tri.pid = toInt(t.get("pid"));
            tri.p1 = toInt(t.get("p1"));
            const std::string_view paint = t.get("paint_color");
            if (!paint.empty()) {
                const auto ins = paintIndex.try_emplace(std::string(paint), int(f.paintStrings.size()));
                if (ins.second) f.paintStrings.push_back(std::string(paint));
                tri.paint = ins.first->second;
            }
            current->tris.push_back(tri);
        } else if (n == "vertex") {
            if (current) current->verts.push_back({toDouble(t.get("x")), toDouble(t.get("y")), toDouble(t.get("z"))});
        } else if (n == "object") {
            current = &f.objects[toInt(t.get("id"))];
            current->name = unescapeXml(t.get("name"));
            current->pid = toInt(t.get("pid"));
            current->pindex = toInt(t.get("pindex"));
            if (t.selfClosing) current = nullptr;
        } else if (n == "component") {
            if (current) {
                Obj3mf::Comp c;
                c.path = std::string(t.get("path"));
                c.object = toInt(t.get("objectid"));
                c.transform = parseMat(t.get("transform"));
                current->comps.push_back(std::move(c));
            }
        } else if (n == "item") {
            f.items.push_back({toInt(t.get("objectid")), parseMat(t.get("transform"))});
        } else if (n == "model") {
            const std::string_view unit = t.get("unit");
            if (unit == "micron") f.unit = 0.001;
            else if (unit == "centimeter") f.unit = 10.0;
            else if (unit == "inch") f.unit = 25.4;
            else if (unit == "foot") f.unit = 304.8;
            else if (unit == "meter") f.unit = 1000.0;
        } else if (n == "basematerials" || n == "colorgroup") {
            group = toInt(t.get("id"));
            f.colorGroups[group].clear();
            if (t.selfClosing) group = -1;
        } else if ((n == "base" || n == "color") && group >= 0) {
            std::array<float, 3> c{0.72f, 0.74f, 0.78f};
            parseHexColor(t.get(n == "base" ? "displaycolor" : "color"), c);
            f.colorGroups[group].push_back(c);
        } else if (n == "metadata") {
            if (!t.selfClosing && !t.get("name").empty()) {
                const std::string value = unescapeXml(trimView(tk.textAfterTag()));
                f.metadata[std::string(t.get("name"))] = value;
                if (t.get("name") == "Application") f.application = value;
            }
        }
    }
    return f;
}

// ---- Bambu Studio / Orca Slicer extras

struct PartCfg {
    int id = -1;
    std::string name;
    int extruder = 0;  // filament number, 1-based; 0 = not set
    int firstId = -1, lastId = -1;
};
struct ObjCfg {
    std::string name;
    int extruder = 0;
    std::vector<PartCfg> parts;
};
struct PlateInst {
    int object = -1, instance = 0;
};
struct PlateCfg {
    int id = 0;
    std::string name;
    std::vector<PlateInst> instances;
};
struct BambuCfg {
    std::map<int, ObjCfg> objects;
    std::vector<PlateCfg> plates;
};

// Metadata/model_settings.config: object and part names, filament (extruder) choices, and which objects sit on which plate.
BambuCfg parseModelSettings(const Bytes& xml) {
    BambuCfg cfg;
    XmlTokenizer tk(xml);
    XmlTag t;
    ObjCfg* obj = nullptr;
    PartCfg* part = nullptr;
    PlateCfg* plate = nullptr;
    PlateInst inst;
    bool inInstance = false;
    auto closeTag = [&](std::string_view n) {
        if (n == "object") { obj = nullptr; part = nullptr; }
        else if (n == "part" || n == "volume") part = nullptr;
        else if (n == "plate") plate = nullptr;
        else if (n == "model_instance") {
            if (plate) plate->instances.push_back(inst);
            inInstance = false;
        }
    };
    while (tk.next(t)) {
        const std::string_view n = t.name;
        if (t.closing) { closeTag(n); continue; }
        if (n == "object" && !plate) {
            obj = &cfg.objects[toInt(t.get("id"))];
        } else if ((n == "part" || n == "volume") && obj) {
            obj->parts.emplace_back();
            part = &obj->parts.back();
            part->id = toInt(t.get("id"));
            part->firstId = toInt(t.get("firstid"));
            part->lastId = toInt(t.get("lastid"));
        } else if (n == "plate") {
            cfg.plates.emplace_back();
            plate = &cfg.plates.back();
        } else if (n == "model_instance") {
            inst = PlateInst();
            inInstance = true;
        } else if (n == "metadata") {
            const std::string_view key = t.get("key");
            if (key.empty()) continue;  // e.g. <metadata face_count="..."/>
            const std::string value = unescapeXml(t.get("value"));
            const int number = toInt(value, 0);
            if (inInstance) {
                if (key == "object_id") inst.object = number;
                else if (key == "instance_id") inst.instance = number;
            } else if (plate) {
                if (key == "plater_id" || key == "index") plate->id = number;
                else if (key == "plater_name") plate->name = value;
            } else if (part) {
                if (key == "name") part->name = value;
                else if (key == "extruder") part->extruder = number;
            } else if (obj) {
                if (key == "name") obj->name = value;
                else if (key == "extruder") obj->extruder = number;
            }
        }
        if (t.selfClosing && n != "metadata") closeTag(n);
    }
    return cfg;
}

struct FilamentDetail {  // one filament slot of the project
    std::string colour;   // "#RRGGBB" as stored
    std::string type, vendor, profile;
    std::string details;  // "1.75 mm · 1.32 g/cm³ · 15.99 per kg · bed 55 °C"
};

struct ProjectInfo {
    std::vector<std::array<float, 3>> filamentColors;
    std::vector<std::string> filamentTypes;
    std::vector<std::string> filamentVendors;
    std::vector<FilamentDetail> filaments;
    std::vector<InfoSection> sections;  // "Printer" and "Print profile"
    std::string printer;
    double nozzle = 0;
    std::vector<std::array<double, 2>> bed;  // printable_area polygon, plate-local millimetres
};

// Helpers for project_settings.config values: some are text, some one-element arrays, most numbers are stored as text.
std::string settingText(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(key);
    if (v.isString()) return v.toString().toStdString();
    if (v.isDouble()) return QString::number(v.toDouble()).toStdString();
    if (v.isBool()) return v.toBool() ? "1" : "0";
    if (v.isArray() && !v.toArray().isEmpty()) {
        const QJsonValue first = v.toArray().first();
        if (first.isString()) return first.toString().toStdString();
        if (first.isDouble()) return QString::number(first.toDouble()).toStdString();
    }
    return {};
}

std::vector<std::string> settingList(const QJsonObject& o, const char* key) {
    std::vector<std::string> out;
    for (const QJsonValue& v : o.value(key).toArray()) out.push_back(v.isString() ? v.toString().toStdString() : "");
    return out;
}

bool isOn(const std::string& v) { return v == "1" || v == "true"; }
std::string onOff(const std::string& v) { return tr8(isOn(v) ? "On" : "Off"); }

// "Printer" and "Print profile" sections, plus the filament slots, from project_settings.config.
void describeProject(const QJsonObject& o, ProjectInfo& info) {
    auto add = [](InfoSection& s, const char* label, const std::string& value, const char* unit = "") {
        if (!value.empty()) s.rows.push_back({tr8(label), value + unit, ""});
    };

    InfoSection printer{tr8("Printer"), {}};
    add(printer, "Printer", settingText(o, "printer_model"));
    add(printer, "Profile", settingText(o, "printer_settings_id"));
    add(printer, "Nozzle", settingText(o, "nozzle_diameter"), " mm");
    if (info.bed.size() >= 3) {
        double minX = info.bed[0][0], maxX = minX, minY = info.bed[0][1], maxY = minY;
        for (const auto& p : info.bed) {
            minX = std::min(minX, p[0]); maxX = std::max(maxX, p[0]);
            minY = std::min(minY, p[1]); maxY = std::max(maxY, p[1]);
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%g × %g mm", maxX - minX, maxY - minY);
        add(printer, "Bed", buf);
    }
    add(printer, "Maximum height", settingText(o, "printable_height"), " mm");
    add(printer, "G-code flavour", settingText(o, "gcode_flavor"));
    if (!printer.rows.empty()) info.sections.push_back(std::move(printer));

    InfoSection print{tr8("Print profile"), {}};
    add(print, "Profile", settingText(o, "print_settings_id"));
    add(print, "Based on", settingText(o, "default_print_profile"));
    add(print, "Layer height", settingText(o, "layer_height"), " mm");
    add(print, "First layer", settingText(o, "initial_layer_print_height"), " mm");
    add(print, "Walls", settingText(o, "wall_loops"));
    if (!settingText(o, "top_shell_layers").empty())
        add(print, "Top / bottom layers", settingText(o, "top_shell_layers") + " / " + settingText(o, "bottom_shell_layers"));
    if (!settingText(o, "sparse_infill_density").empty())
        add(print, "Infill", settingText(o, "sparse_infill_density") + " " + settingText(o, "sparse_infill_pattern"));
    if (!settingText(o, "enable_support").empty()) {
        std::string s = onOff(settingText(o, "enable_support"));
        if (isOn(settingText(o, "enable_support"))) s += " · " + settingText(o, "support_type") + " " + settingText(o, "support_style");
        add(print, "Supports", s);
    }
    add(print, "Brim", settingText(o, "brim_type"));
    if (settingText(o, "skirt_loops") != "0") add(print, "Skirt loops", settingText(o, "skirt_loops"));
    if (!settingText(o, "raft_layers").empty() && settingText(o, "raft_layers") != "0")
        add(print, "Raft layers", settingText(o, "raft_layers"));
    add(print, "Seam", settingText(o, "seam_position"));
    add(print, "Ironing", settingText(o, "ironing_type"));
    add(print, "Fuzzy skin", settingText(o, "fuzzy_skin"));
    if (!settingText(o, "spiral_mode").empty()) add(print, "Spiral vase", onOff(settingText(o, "spiral_mode")));
    if (!settingText(o, "enable_prime_tower").empty()) add(print, "Prime tower", onOff(settingText(o, "enable_prime_tower")));
    add(print, "Print sequence", settingText(o, "print_sequence"));
    const std::string bedType = settingText(o, "curr_bed_type");
    add(print, "Build plate", bedType);
    // Settings the user changed from the profile they started with (internal names, shortened).
    {
        const std::vector<std::string> changed = settingList(o, "different_settings_to_system");
        std::string joined;
        int count = 0;
        for (const std::string& list : changed) {
            size_t pos = 0;
            while (pos <= list.size()) {
                const size_t semi = list.find(';', pos);
                std::string name = list.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
                pos = semi == std::string::npos ? list.size() + 1 : semi + 1;
                if (name.empty()) continue;
                std::replace(name.begin(), name.end(), '_', ' ');
                if (++count <= 30) joined += (joined.empty() ? "" : ", ") + name;
            }
        }
        if (count > 0) {
            if (count > 30) joined += ", ... " + tr8f("(%1 more)", {std::to_string(count - 30)});
            add(print, "Changed from profile", tr8f("%1 settings: %2", {std::to_string(count), joined}));
        }
    }
    if (!print.rows.empty()) info.sections.push_back(std::move(print));

    // Filament slots. Temperature lists can have several entries per slot (variants); they are shown only when there is one.
    const std::vector<std::string> colours = settingList(o, "filament_colour"), types = settingList(o, "filament_type"),
                                   vendors = settingList(o, "filament_vendor"), profiles = settingList(o, "filament_settings_id"),
                                   diameters = settingList(o, "filament_diameter"), densities = settingList(o, "filament_density"),
                                   costs = settingList(o, "filament_cost");
    const char* bedKey = bedType == "Cool Plate" ? "cool_plate_temp"
                         : bedType == "Engineering Plate" ? "eng_plate_temp"
                         : bedType == "High Temp Plate" ? "hot_plate_temp"
                         : bedType == "Textured PEI Plate" ? "textured_plate_temp" : nullptr;
    const std::vector<std::string> bedTemps = bedKey ? settingList(o, bedKey) : std::vector<std::string>();
    const std::vector<std::string> nozzleTemps = settingList(o, "nozzle_temperature");
    for (size_t i = 0; i < colours.size(); ++i) {
        FilamentDetail d;
        d.colour = colours[i];
        if (i < types.size()) d.type = types[i];
        if (i < vendors.size()) d.vendor = vendors[i];
        if (i < profiles.size()) d.profile = profiles[i];
        std::string text;
        auto part = [&](const std::string& s) {
            if (!s.empty()) text += (text.empty() ? "" : " · ") + s;
        };
        if (i < diameters.size() && !diameters[i].empty()) part(diameters[i] + " mm");
        if (i < densities.size() && !densities[i].empty()) part(densities[i] + " g/cm³");
        if (i < costs.size() && !costs[i].empty()) part(tr8f("%1 per kg", {costs[i]}));
        if (nozzleTemps.size() == colours.size() && !nozzleTemps[i].empty()) part(tr8f("nozzle %1 °C", {nozzleTemps[i]}));
        if (bedTemps.size() == colours.size() && !bedTemps[i].empty() && bedTemps[i] != "0")
            part(tr8f("bed %1 °C", {bedTemps[i]}));
        d.details = text;
        info.filaments.push_back(std::move(d));
    }
}

// Metadata/project_settings.config is JSON.
ProjectInfo parseProjectSettings(const Bytes& json) {
    ProjectInfo info;
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(json.data()), qsizetype(json.size())));
    if (!doc.isObject()) return info;
    const QJsonObject obj = doc.object();
    for (const QJsonValue& v : obj.value("filament_colour").toArray()) {
        std::array<float, 3> c{0.72f, 0.74f, 0.78f};
        parseHexColor(v.toString().toStdString(), c);
        info.filamentColors.push_back(c);
    }
    for (const QJsonValue& v : obj.value("filament_type").toArray()) info.filamentTypes.push_back(v.toString().toStdString());
    for (const QJsonValue& v : obj.value("filament_vendor").toArray()) info.filamentVendors.push_back(v.toString().toStdString());
    info.printer = obj.value("printer_model").toString().toStdString();
    for (const QJsonValue& v : obj.value("printable_area").toArray()) {  // entries look like "256x0"
        const std::string s = v.toString().toStdString();
        const size_t x = s.find('x');
        if (x == std::string::npos) continue;
        info.bed.push_back({toDouble(std::string_view(s).substr(0, x)), toDouble(std::string_view(s).substr(x + 1))});
    }
    const QJsonArray nozzles = obj.value("nozzle_diameter").toArray();
    if (!nozzles.isEmpty()) info.nozzle = nozzles.first().toString().toDouble();
    describeProject(obj, info);
    return info;
}

// The "Project" section: what the file says about itself (any 3MF may carry these <metadata> entries).
InfoSection describeFile(const ModelFile& f) {
    InfoSection s{tr8("Project"), {}};
    auto meta = [&](const char* label, const char* key) {
        const auto it = f.metadata.find(key);
        if (it == f.metadata.end()) return;
        // Only the description is a web page's HTML; the other values are plain text (a title may hold "<5 mm").
        const std::string value = std::strcmp(key, "Description") == 0 ? htmlToText(it->second) : it->second;
        if (!value.empty()) s.rows.push_back({tr8(label), value, ""});
    };
    meta("Made with", "Application");
    meta("Title", "Title");
    meta("Designer", "Designer");
    meta("Description", "Description");
    meta("Copyright", "Copyright");
    meta("Licence", "License");
    meta("Created", "CreationDate");
    meta("Modified", "ModificationDate");
    meta("Origin", "Origin");
    return s;
}

struct SliceFilament {
    int id = 0;
    std::string type, color;  // color as "#RRGGBB"
    double meters = -1, grams = -1;
};

struct SlicePlate {
    int index = 0;
    double seconds = -1, grams = -1;
    std::vector<SliceFilament> filaments;
    std::vector<std::string> warnings;
    int skippedObjects = 0;
};

// Metadata/slice_info.config only has plate entries in files that were sliced.
std::vector<SlicePlate> parseSliceInfo(const Bytes& xml) {
    std::vector<SlicePlate> plates;
    XmlTokenizer tk(xml);
    XmlTag t;
    bool inPlate = false;
    while (tk.next(t)) {
        if (t.closing) {
            if (t.name == "plate") inPlate = false;
            continue;
        }
        if (t.name == "plate") {
            plates.emplace_back();
            inPlate = !t.selfClosing;
        } else if (t.name == "metadata" && inPlate) {
            const std::string_view key = t.get("key");
            const std::string_view value = t.get("value");
            if (key == "index") plates.back().index = toInt(value, 0);
            else if (key == "prediction") plates.back().seconds = toDouble(value, -1);
            else if (key == "weight") plates.back().grams = toDouble(value, -1);
        } else if (t.name == "filament" && inPlate) {
            SliceFilament f;
            f.id = toInt(t.get("id"), 0);
            f.type = unescapeXml(t.get("type"));
            f.color = unescapeXml(t.get("color"));
            f.meters = toDouble(t.get("used_m"), -1);
            f.grams = toDouble(t.get("used_g"), -1);
            plates.back().filaments.push_back(std::move(f));
        } else if (t.name == "warning" && inPlate) {
            const std::string msg = unescapeXml(t.get("msg"));
            if (!msg.empty()) plates.back().warnings.push_back(msg);
        } else if (t.name == "object" && inPlate && t.get("skipped") == "true") {
            ++plates.back().skippedObjects;
        }
    }
    return plates;
}

// Decodes a Bambu/Orca "paint_color" string (a bit stream written as hex, read from the last character, lowest bit
// first) and returns the filament number that covers most of the triangle (0 = not painted).
int dominantPaintState(const std::string& s) {
    std::vector<uint8_t> bits;
    for (size_t i = s.size(); i-- > 0;) {
        const char ch = s[i];
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else return 0;
        for (int b = 0; b < 4; ++b) bits.push_back(uint8_t((d >> b) & 1));
    }
    size_t pos = 0;
    bool ok = true;
    auto take = [&](int count) {
        int v = 0;
        for (int i = 0; i < count; ++i) {
            if (pos >= bits.size()) { ok = false; return 0; }
            v |= int(bits[pos++]) << i;
        }
        return v;
    };
    std::map<int, double> area;
    std::function<void(double, int)> node = [&](double weight, int depth) {
        const int splits = take(2);
        if (!ok) return;
        if (splits == 0) {  // a painted leaf: 2-bit state, or 3 followed by a 4-bit extension
            int state = take(2);
            if (state == 3) {
                const int n = take(4);
                state = n == 15 ? take(4) + 18 : n + 3;
            }
            if (ok) area[state] += weight;
            return;
        }
        take(2);  // which side was split
        const int children = splits + 1;
        for (int i = 0; i < children && ok && depth < 24; ++i) node(weight / children, depth + 1);
    };
    node(1.0, 0);
    int best = 0;
    double bestArea = -1;
    for (const auto& kv : area)
        if (kv.second > bestArea) { best = kv.first; bestArea = kv.second; }
    return best;
}

std::string formatDuration(double seconds) {
    const int total = int(seconds + 0.5), h = total / 3600, m = (total % 3600) / 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof buf, "%d h %02d m", h, m);
    else std::snprintf(buf, sizeof buf, "%d m", m);
    return buf;
}

std::string formatGrams(double grams) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f g", grams);
    return buf;
}

// The 3MF archive. Only the parts that are used are read from disk (projects with thumbnails and G-code can be big).
struct Package {
    ZipFile zip;
    const std::vector<ZipEntry>& entries;
    explicit Package(const std::string& path) : zip(path, "The 3MF file is not a valid zip archive."), entries(zip.entries) {}
    static std::string norm(std::string s) {
        for (char& c : s) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
        while (!s.empty() && s.front() == '/') s.erase(0, 1);
        return s;
    }
    const ZipEntry* find(const std::string& name) const {
        const std::string want = norm(name);
        for (const ZipEntry& e : entries)
            if (norm(e.name) == want) return &e;
        return nullptr;
    }
};

}  // namespace

Scene read3mf(const std::string& path) {
    Package pkg(path);

    const ZipEntry* rootEntry = pkg.find("3D/3dmodel.model");
    if (!rootEntry) {
        for (const ZipEntry& e : pkg.entries) {
            const std::string lower = Package::norm(e.name);
            if (lower.size() > 6 && lower.compare(lower.size() - 6, 6, ".model") == 0) { rootEntry = &e; break; }
        }
    }
    if (!rootEntry) throw std::runtime_error(tr8("The 3MF file contains no 3D model."));
    const std::string rootKey = Package::norm(rootEntry->name);

    // Model files are parsed on first use: Bambu/Orca keep each object in its own 3D/Objects/object_N.model file.
    std::map<std::string, ModelFile> files;
    auto getFile = [&](const std::string& name) -> const ModelFile& {
        const std::string key = name.empty() ? rootKey : Package::norm(name);
        const auto it = files.find(key);
        if (it != files.end()) return it->second;
        const ZipEntry* e = pkg.find(key);
        if (!e) throw std::runtime_error(tr8f("The 3MF file refers to \"%1\", which is missing from the archive.", {name}));
        return files.emplace(key, parseModelXml(pkg.zip.read(*e))).first->second;
    };
    const ModelFile& root = getFile("");
    const double scale = root.unit;

    Scene scene;

    // Which mesh a triangle belongs to (one mesh per `key`) and how it is coloured.
    struct ColorChoice {
        int key = -1;
        bool hasColor = false;
        std::array<float, 3> color{};
        int filament = 0;  // Bambu/Orca filament number, 0 = none
    };
    using ColorFn = std::function<ColorChoice(const ModelFile&, const Obj3mf&, const Obj3mf::Tri&)>;

    // Turns the triangles first..last of `o` into one mesh per colour; returns the new mesh indices.
    auto emitMeshes = [&](const ModelFile& f, const Obj3mf& o, const std::string& baseName, const Mat& m, size_t first,
                          size_t last, const ColorFn& colorOf, std::vector<int>& outIdx) {
        const bool mirrored = determinant(m) < 0;
        std::map<int, SceneMesh> byColor;  // key -1 = no colour
        std::map<int, std::vector<int>> remap;
        int lastKey = INT_MIN;
        SceneMesh* sm = nullptr;
        std::vector<int>* rm = nullptr;
        const size_t end = last >= o.tris.size() ? o.tris.size() : last + 1;
        for (size_t ti = first; ti < end; ++ti) {
            const Obj3mf::Tri& t = o.tris[ti];
            const ColorChoice choice = colorOf(f, o, t);
            const int key = choice.key;
            if (key != lastKey) {
                sm = &byColor[key];
                rm = &remap[key];
                if (rm->empty()) rm->assign(o.verts.size(), -1);
                if (sm->name.empty()) {
                    sm->name = baseName;
                    sm->hasColor = choice.hasColor;
                    if (choice.hasColor) sm->color = choice.color;
                    sm->filament = choice.filament;
                }
                lastKey = key;
            }
            int out[3];
            for (int k = 0; k < 3; ++k) {
                const int v = t.v[k];
                if (v < 0 || size_t(v) >= o.verts.size())
                    throw std::runtime_error(tr8("The 3MF file has an invalid vertex index."));
                int& slot = (*rm)[size_t(v)];
                if (slot < 0) {
                    slot = int(sm->points.size());
                    Vec3 p = apply(m, o.verts[size_t(v)]);
                    for (double& c : p) c *= scale;
                    sm->points.push_back(p);
                }
                out[k] = slot;
            }
            if (mirrored) std::swap(out[1], out[2]);
            sm->triangles.push_back({out[0], out[1], out[2]});
        }
        const bool several = byColor.size() > 1;
        for (auto& kv : byColor) {
            if (several && (kv.second.filament > 0 || kv.second.hasColor)) {
                char buf[32];
                if (kv.second.filament > 0) std::snprintf(buf, sizeof buf, "%s", tr8f(" (filament %1)", {std::to_string(kv.second.filament)}).c_str());
                else std::snprintf(buf, sizeof buf, " (#%06X)", unsigned(packColor(kv.second.color)));
                kv.second.name += buf;
            }
            outIdx.push_back(int(scene.meshes.size()));
            scene.meshes.push_back(std::move(kv.second));
        }
    };

    const bool isBambu = pkg.find("Metadata/model_settings.config") != nullptr;

    // ---- Plain 3MF: flat list, colours from basematerials / colorgroup.
    if (!isBambu) {
        const ColorFn plainColor = [](const ModelFile& f, const Obj3mf& o, const Obj3mf::Tri& t) {
            ColorChoice choice;
            const int pid = t.pid >= 0 ? t.pid : o.pid;
            const int idx = t.p1 >= 0 ? t.p1 : o.pindex;
            const auto g = f.colorGroups.find(pid);
            if (g != f.colorGroups.end() && idx >= 0 && size_t(idx) < g->second.size()) {
                choice.color = g->second[size_t(idx)];
                choice.hasColor = true;
                choice.key = packColor(choice.color);
            }
            return choice;
        };
        std::function<void(const std::string&, int, const Mat&, int)> addPlain = [&](const std::string& fileName,
                                                                                    int id, const Mat& m, int depth) {
            const ModelFile& f = getFile(fileName);
            const auto it = f.objects.find(id);
            if (it == f.objects.end() || depth > 16) return;
            const Obj3mf& o = it->second;
            if (!o.tris.empty()) {
                std::vector<int> idx;
                emitMeshes(f, o, o.name.empty() ? tr8f("Object %1", {std::to_string(id)}) : o.name, m, 0, SIZE_MAX, plainColor,
                           idx);
            }
            for (const Obj3mf::Comp& c : o.comps)
                addPlain(c.path.empty() ? fileName : c.path, c.object, compose(c.transform, m), depth + 1);
        };
        if (!root.items.empty()) {
            for (const Item& it : root.items) addPlain("", it.object, it.transform, 0);
        } else {
            std::set<int> referenced;
            for (const auto& kv : root.objects)
                for (const Obj3mf::Comp& c : kv.second.comps)
                    if (c.path.empty()) referenced.insert(c.object);
            for (const auto& kv : root.objects)
                if (!referenced.count(kv.first)) addPlain("", kv.first, kIdentity, 0);
        }
        if (scene.meshes.empty()) throw std::runtime_error(tr8("The 3MF file contains no meshes."));
        InfoSection fileSection = describeFile(root);
        if (!fileSection.rows.empty()) scene.sections.push_back(std::move(fileSection));
        return scene;
    }

    // ---- Bambu Studio / Orca Slicer project: plates -> objects -> parts, filament colours, print info.
    BambuCfg cfg;
    if (const ZipEntry* e = pkg.find("Metadata/model_settings.config")) cfg = parseModelSettings(pkg.zip.read(*e));
    ProjectInfo project;
    if (const ZipEntry* e = pkg.find("Metadata/project_settings.config")) project = parseProjectSettings(pkg.zip.read(*e));
    std::vector<SlicePlate> sliced;
    if (const ZipEntry* e = pkg.find("Metadata/slice_info.config")) sliced = parseSliceInfo(pkg.zip.read(*e));

    std::map<int, std::vector<const Item*>> itemsOf;  // build items per object, in order = instance 0, 1, ...
    for (const Item& it : root.items) itemsOf[it.object].push_back(&it);

    std::vector<PlateCfg> plates = cfg.plates;
    if (plates.empty()) {  // no plate information: everything goes on one plate
        PlateCfg only;
        only.id = 1;
        std::map<int, int> counter;
        for (const Item& it : root.items) only.instances.push_back({it.object, counter[it.object]++});
        plates.push_back(std::move(only));
    }
    for (size_t i = 0; i < plates.size(); ++i)
        if (plates[i].id <= 0) plates[i].id = int(i) + 1;
    std::stable_sort(plates.begin(), plates.end(), [](const PlateCfg& a, const PlateCfg& b) { return a.id < b.id; });

    // Print plates: all plates share one world space, laid out in a grid with a 20 % gap, rows going towards -Y
    // (as in Bambu Studio / Orca Slicer, PartPlateList::compute_origin). The bed outline comes from `printable_area`.
    if (project.bed.size() >= 3) {
        double minX = project.bed[0][0], maxX = minX, minY = project.bed[0][1], maxY = minY;
        for (const auto& p : project.bed) {
            minX = std::min(minX, p[0]); maxX = std::max(maxX, p[0]);
            minY = std::min(minY, p[1]); maxY = std::max(maxY, p[1]);
        }
        const double width = maxX - minX, depth = maxY - minY;
        const double root = std::sqrt(double(plates.size())), rounded = std::round(root);
        const int cols = std::max(1, int(root > rounded ? rounded + 1 : rounded));
        for (size_t i = 0; i < plates.size(); ++i) {
            const double ox = double(int(i) % cols) * width * 1.2, oy = -double(int(i) / cols) * depth * 1.2;
            ScenePlate plate;
            for (const auto& p : project.bed) plate.outline.push_back({p[0] + ox, p[1] + oy});
            scene.plates.push_back(std::move(plate));
        }
    }

    std::set<int> usedFilaments;

    // Filament of each paint string, decoded once per model file (all parts of a file share its strings).
    std::map<const ModelFile*, std::vector<int>> paintStates;
    auto emitPart = [&](const ModelFile& f, const Obj3mf& o, const std::string& name, const Mat& m, size_t first,
                        size_t last, int extruder, SceneNode& parent) {
        auto [cached, isNew] = paintStates.try_emplace(&f);
        if (isNew)
            for (const std::string& s : f.paintStrings) cached->second.push_back(dominantPaintState(s));
        const std::vector<int>& states = cached->second;
        const ColorFn colorOf = [&, extruder](const ModelFile&, const Obj3mf&, const Obj3mf::Tri& t) {
            int filament = extruder > 0 ? extruder : 1;
            if (t.paint >= 0 && size_t(t.paint) < states.size() && states[size_t(t.paint)] > 0)
                filament = states[size_t(t.paint)];
            usedFilaments.insert(filament);
            ColorChoice choice;
            choice.key = filament;  // one mesh per filament, even when two filaments share a colour
            choice.filament = filament;
            if (size_t(filament) <= project.filamentColors.size()) {
                choice.color = project.filamentColors[size_t(filament) - 1];
                choice.hasColor = true;
            }
            return choice;
        };
        std::vector<int> idx;
        emitMeshes(f, o, name, m, first, last, colorOf, idx);
        for (int i : idx) {
            SceneMesh& sm = scene.meshes[size_t(i)];
            sm.objectName = parent.name;  // `parent` is the object's node
            if (sm.filament >= 1 && size_t(sm.filament) <= project.filamentTypes.size())
                sm.filamentType = project.filamentTypes[size_t(sm.filament) - 1];
            if (sm.filament >= 1 && size_t(sm.filament) <= project.filamentVendors.size())
                sm.filamentVendor = project.filamentVendors[size_t(sm.filament) - 1];
            parent.children.push_back({sm.name, {}, i});
        }
    };

    auto buildObject = [&](const Item& item) -> SceneNode {
        SceneNode node;
        const auto rit = root.objects.find(item.object);
        const auto cit = cfg.objects.find(item.object);
        const Obj3mf* ro = rit != root.objects.end() ? &rit->second : nullptr;
        const ObjCfg* oc = cit != cfg.objects.end() ? &cit->second : nullptr;
        node.name = oc && !oc->name.empty() ? oc->name
                    : ro && !ro->name.empty() ? ro->name
                                              : tr8f("Object %1", {std::to_string(item.object)});
        if (!ro) return node;

        std::function<void(const std::string&, const Obj3mf&, int, const Mat&, int)> addParts =
            [&](const std::string& fileName, const Obj3mf& o, int objId, const Mat& m, int depth) {
                const ModelFile& f = getFile(fileName);
                if (!o.tris.empty()) {
                    // A part's id is the objectid of its component; parts with firstid/lastid slice one shared mesh.
                    const PartCfg* byId = nullptr;
                    bool ranged = false;
                    if (oc) {
                        for (const PartCfg& p : oc->parts) {
                            if (p.firstId >= 0 && p.lastId >= p.firstId) {
                                ranged = true;
                                emitPart(f, o, p.name.empty() ? node.name : p.name, m, size_t(p.firstId),
                                         size_t(p.lastId), p.extruder > 0 ? p.extruder : oc->extruder, node);
                            } else if (p.id == objId && depth > 0) {
                                byId = &p;
                            }
                        }
                    }
                    if (!ranged) {
                        const std::string name = byId && !byId->name.empty() ? byId->name
                                                 : depth > 0 && !o.name.empty() ? o.name
                                                                                : node.name;
                        const int extruder = byId && byId->extruder > 0 ? byId->extruder : oc ? oc->extruder : 0;
                        emitPart(f, o, name, m, 0, SIZE_MAX, extruder, node);
                    }
                }
                if (depth > 8) return;
                for (const Obj3mf::Comp& c : o.comps) {
                    const std::string& subName = c.path.empty() ? fileName : c.path;
                    const ModelFile& sf = getFile(subName);
                    const auto sit = sf.objects.find(c.object);
                    if (sit != sf.objects.end()) addParts(subName, sit->second, c.object, compose(c.transform, m), depth + 1);
                }
            };
        addParts(rootKey, *ro, item.object, item.transform, 0);

        if (node.children.size() == 1 && node.children[0].mesh >= 0) {  // one part: show the object itself
            node.mesh = node.children[0].mesh;
            scene.meshes[size_t(node.mesh)].name = node.name;
            node.children.clear();
        }
        return node;
    };

    for (size_t plateIndex = 0; plateIndex < plates.size(); ++plateIndex) {
        const PlateCfg& plate = plates[plateIndex];
        std::string plateTitle = tr8f("Plate %1", {std::to_string(plate.id)});
        if (!plate.name.empty()) plateTitle += ": " + plate.name;
        double plateSeconds = -1;
        for (const SlicePlate& sp : sliced)
            if (sp.index == plate.id && sp.seconds >= 0) plateSeconds = sp.seconds;
        if (plateIndex < scene.plates.size()) {  // details shown when the plate is clicked
            scene.plates[plateIndex].name = plateTitle;
            scene.plates[plateIndex].printSeconds = plateSeconds;
        }
        SceneNode pn;
        for (const PlateInst& inst : plate.instances) {
            const auto it = itemsOf.find(inst.object);
            if (it == itemsOf.end() || inst.instance < 0 || size_t(inst.instance) >= it->second.size()) continue;
            pn.children.push_back(buildObject(*it->second[size_t(inst.instance)]));
        }
        if (pn.children.empty()) continue;
        pn.name = plateTitle;
        pn.name += " · " + plural8((long long)pn.children.size(), "%1 object", "%1 objects");
        for (const SlicePlate& sp : sliced) {
            if (sp.index != plate.id) continue;
            if (sp.seconds >= 0) pn.name += " · " + formatDuration(sp.seconds);
            if (sp.grams >= 0) pn.name += " · " + formatGrams(sp.grams);
        }
        scene.tree.push_back(std::move(pn));
    }
    if (scene.meshes.empty()) throw std::runtime_error(tr8("The 3MF file contains no meshes."));

    // Status-bar summary: printer, nozzle size and number of filaments used (plates, objects and print time are in the tree).
    std::string info;
    auto add = [&](const std::string& part) {
        if (!part.empty()) info += (info.empty() ? "" : "  ·  ") + part;
    };
    add(project.printer);
    if (project.nozzle > 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", project.nozzle);
        add(tr8f("%1 mm nozzle", {buf}));
    }
    add(plural8((long long)scene.tree.size(), "%1 plate", "%1 plates"));
    if (!usedFilaments.empty())
        add(plural8((long long)usedFilaments.size(), "%1 filament", "%1 filaments"));
    scene.info = info;

    // ---- Everything else the file says, for the info panel (sections in the order they are shown).
    {
        std::vector<InfoSection>& sections = scene.sections;
        char buf[96];

        InfoSection fileSection = describeFile(root);
        if (!fileSection.rows.empty()) sections.push_back(std::move(fileSection));
        for (InfoSection& s : project.sections) sections.push_back(std::move(s));  // Printer, Print profile

        if (!project.filaments.empty()) {
            InfoSection fs{tr8("Filaments"), {}};
            for (size_t i = 0; i < project.filaments.size(); ++i) {
                const FilamentDetail& d = project.filaments[i];
                std::string value = d.vendor;  // "Bambu Lab PLA"
                if (!d.type.empty()) value += (value.empty() ? "" : " ") + d.type;
                if (!d.profile.empty()) value += (value.empty() ? "" : "\n") + d.profile;
                if (!d.details.empty()) value += (value.empty() ? "" : "\n") + d.details;
                value += (value.empty() ? "" : "\n") + std::string(tr8(usedFilaments.count(int(i) + 1) ? "Used in this project" : "Not used"));
                fs.rows.push_back({tr8f("Filament %1", {std::to_string(i + 1)}), value, d.colour});
            }
            sections.push_back(std::move(fs));
        }

        InfoSection plateSection{tr8("Plates"), {}}, objectSection{tr8("Objects"), {}};
        size_t objectRows = 0, objectsTotal = 0;
        for (const PlateCfg& plate : plates) {
            std::string title = tr8f("Plate %1", {std::to_string(plate.id)});
            if (!plate.name.empty()) title += ": " + plate.name;
            std::string value = plural8((long long)plate.instances.size(), "%1 object", "%1 objects");
            for (const SlicePlate& sp : sliced) {
                if (sp.index != plate.id) continue;
                if (sp.seconds >= 0) value += " · " + formatDuration(sp.seconds);
                if (sp.grams >= 0) value += " · " + formatGrams(sp.grams);
            }
            plateSection.rows.push_back({title, value, ""});
            for (const PlateInst& inst : plate.instances) {
                const auto oc = cfg.objects.find(inst.object);
                if (oc == cfg.objects.end()) continue;
                ++objectsTotal;
                if (objectRows >= 300) continue;
                int filament = oc->second.extruder;
                if (filament <= 0 && !oc->second.parts.empty()) filament = oc->second.parts[0].extruder;
                if (filament <= 0) filament = 1;
                std::string v = tr8f("Plate %1 · filament %2", {std::to_string(plate.id), std::to_string(filament)});
                std::string swatch;
                if (size_t(filament) <= project.filaments.size()) {
                    const FilamentDetail& d = project.filaments[size_t(filament) - 1];
                    if (!d.type.empty()) v += " (" + d.type + ")";
                    swatch = d.colour;
                }
                objectSection.rows.push_back({oc->second.name.empty() ? tr8f("Object %1", {std::to_string(inst.object)}) : oc->second.name, v, swatch});
                ++objectRows;
            }
        }
        if (objectsTotal > objectRows)
            objectSection.rows.push_back({"...", tr8f("%1 more objects", {std::to_string(objectsTotal - objectRows)}), ""});
        if (!plateSection.rows.empty()) sections.push_back(std::move(plateSection));
        if (!objectSection.rows.empty()) sections.push_back(std::move(objectSection));

        // Result of slicing: only a sliced project has it.
        InfoSection sliceSection{tr8("Slicing result"), {}};
        double totalSeconds = 0, totalGrams = 0;
        int slicedPlates = 0;
        for (const SlicePlate& sp : sliced) {
            if (sp.seconds < 0 && sp.filaments.empty()) continue;
            ++slicedPlates;
            std::string v;
            if (sp.seconds >= 0) { v = formatDuration(sp.seconds); totalSeconds += sp.seconds; }
            if (sp.grams >= 0) { v += (v.empty() ? "" : " · ") + formatGrams(sp.grams); totalGrams += sp.grams; }
            if (sp.skippedObjects > 0) v += (v.empty() ? "" : " · ") + tr8f("%1 skipped", {std::to_string(sp.skippedObjects)});
            sliceSection.rows.push_back({tr8f("Plate %1", {std::to_string(sp.index)}), v, ""});
            for (const SliceFilament& f : sp.filaments) {
                std::string fv = f.type;
                if (f.meters >= 0) {
                    std::snprintf(buf, sizeof buf, "%.2f m", f.meters);
                    fv += (fv.empty() ? "" : " · ") + std::string(buf);
                }
                if (f.grams >= 0) fv += (fv.empty() ? "" : " · ") + formatGrams(f.grams);
                sliceSection.rows.push_back({tr8f("Plate %1, filament %2", {std::to_string(sp.index), std::to_string(f.id)}), fv, f.color});
            }
            for (const std::string& w : sp.warnings) sliceSection.rows.push_back({tr8("Warning"), w, ""});
        }
        if (slicedPlates > 1)
            sliceSection.rows.insert(sliceSection.rows.begin(), {tr8("All plates"), formatDuration(totalSeconds) + " · " + formatGrams(totalGrams), ""});
        if (slicedPlates == 0)
            sliceSection.rows.push_back({tr8("Status"), tr8("Not sliced. Slice the plates in the slicer and save the project to see print time and filament use here."), ""});
        sections.push_back(std::move(sliceSection));
    }
    return scene;
}

// ---------------------------------------------------------------- DXF (ASCII)

namespace {

struct DxfGroup {
    int code = 0;
    std::string value;
};

struct DxfRecord {
    std::string type;
    std::vector<DxfGroup> data;  // groups after the leading "0 TYPE"
};

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

void hsvToRgb(float h, float s, float v, float out[3]) {
    const float c = v * s, hp = h / 60.f;
    const float x = c * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    const float m = v - c;
    out[0] = r + m; out[1] = g + m; out[2] = b + m;
}

// AutoCAD colour index -> RGB. Returns false for "white/black" (7) and unknown values, which keep the default colour.
bool aciColor(int aci, std::array<float, 3>& c) {
    static const float basic[7][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}};
    if (aci >= 1 && aci <= 6) { c = {basic[aci][0], basic[aci][1], basic[aci][2]}; return true; }
    if (aci == 8) { c = {0.5f, 0.5f, 0.5f}; return true; }
    if (aci == 9) { c = {0.75f, 0.75f, 0.75f}; return true; }
    if (aci >= 10 && aci <= 249) {
        static const float values[5] = {1.f, 0.65f, 0.5f, 0.4f, 0.25f};
        const int n = aci - 10, k = n % 10;
        float rgb[3];
        hsvToRgb(float(n / 10) * 15.f, k % 2 == 0 ? 1.f : 0.5f, values[k / 2], rgb);
        c = {rgb[0], rgb[1], rgb[2]};
        return true;
    }
    if (aci >= 250 && aci <= 255) {
        const float g = (51.f + 40.8f * float(aci - 250)) / 255.f;
        c = {g, g, g};
        return true;
    }
    return false;
}

// DXF stores circles, arcs and 2D polylines in an "object coordinate system" given by the extrusion direction
// (groups 210/220/230); this is the standard arbitrary-axis rule to get back to world coordinates.
struct Ocs {
    Vec3 ax{1, 0, 0}, ay{0, 1, 0}, n{0, 0, 1};
    bool identity = true;
};

Ocs makeOcs(double nx, double ny, double nz) {
    Ocs o;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) return o;
    nx /= len; ny /= len; nz /= len;
    if (std::fabs(nx) < 1e-9 && std::fabs(ny) < 1e-9 && nz > 0) return o;
    o.identity = false;
    o.n = {nx, ny, nz};
    // Ax = (N nearly along Z) ? Y x N : Z x N
    Vec3 a = (std::fabs(nx) < 1.0 / 64 && std::fabs(ny) < 1.0 / 64) ? Vec3{nz, 0, -nx} : Vec3{-ny, nx, 0};
    const double al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    a = {a[0] / al, a[1] / al, a[2] / al};
    o.ax = a;
    o.ay = {ny * a[2] - nz * a[1], nz * a[0] - nx * a[2], nx * a[1] - ny * a[0]};  // N x Ax
    return o;
}

Vec3 toWorld(const Ocs& o, const Vec3& p) {
    if (o.identity) return p;
    return {o.ax[0] * p[0] + o.ay[0] * p[1] + o.n[0] * p[2], o.ax[1] * p[0] + o.ay[1] * p[1] + o.n[1] * p[2],
            o.ax[2] * p[0] + o.ay[2] * p[1] + o.n[2] * p[2]};
}

// Points of a polyline in which vertex i has bulge `bulge[i]` (tangent of a quarter of the arc angle, positive =
// counter-clockwise) towards the next vertex; arcs are replaced by short straight pieces.
std::vector<Vec3> expandBulges(const std::vector<Vec3>& pts, const std::vector<double>& bulge, bool closed) {
    std::vector<Vec3> out;
    const size_t n = pts.size();
    if (n == 0) return out;
    const size_t segments = closed ? n : n - 1;
    for (size_t i = 0; i < segments; ++i) {
        const Vec3& p0 = pts[i];
        const Vec3& p1 = pts[(i + 1) % n];
        out.push_back(p0);
        const double b = i < bulge.size() ? bulge[i] : 0.0;
        const double dx = p1[0] - p0[0], dy = p1[1] - p0[1], d = std::sqrt(dx * dx + dy * dy);
        if (std::fabs(b) < 1e-9 || d < 1e-12) continue;
        const double theta = 4.0 * std::atan(b);  // signed sweep
        const double offset = (d / 2.0) * (1.0 - b * b) / (2.0 * b);  // centre distance from the chord, to its left
        const double cx = (p0[0] + p1[0]) / 2.0 - dy / d * offset, cy = (p0[1] + p1[1]) / 2.0 + dx / d * offset;
        const double r = std::sqrt((p0[0] - cx) * (p0[0] - cx) + (p0[1] - cy) * (p0[1] - cy));
        const double a0 = std::atan2(p0[1] - cy, p0[0] - cx);
        const int steps = std::max(2, int(std::ceil(std::fabs(theta) / (3.14159265358979323846 / 16.0))));
        for (int k = 1; k < steps; ++k) {
            const double a = a0 + theta * k / steps;
            out.push_back({cx + r * std::cos(a), cy + r * std::sin(a), p0[2] + (p1[2] - p0[2]) * k / steps});
        }
    }
    out.push_back(closed ? pts.front() : pts.back());  // a closed outline ends where it started
    return out;
}

struct DxfMeshAcc {
    SceneMesh mesh;
    std::map<Vec3, int> weld;
};

}  // namespace

Scene readDxf(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) throw std::runtime_error(tr8f("Could not read %1 file.", {"DXF"}));
    char magic[18] = {};
    in.read(magic, 18);
    if (std::string(magic, size_t(in.gcount())) == "AutoCAD Binary DXF")
        throw std::runtime_error(tr8("Binary DXF files are not supported. Save the DXF as ASCII text."));
    in.clear();
    in.seekg(0);

    std::vector<DxfRecord> records;
    std::string codeLine, valueLine;
    while (std::getline(in, codeLine) && std::getline(in, valueLine)) {
        const std::string cs = trimmed(codeLine);
        if (cs.empty()) continue;
        char* end = nullptr;
        const long code = std::strtol(cs.c_str(), &end, 10);
        if (end == cs.c_str()) throw std::runtime_error(tr8f("Not a valid %1 file.", {"DXF"}));
        std::string value = trimmed(valueLine);
        if (code == 0) records.push_back({value, {}});
        else if (!records.empty()) records.back().data.push_back({int(code), std::move(value)});
    }
    if (records.empty()) throw std::runtime_error(tr8f("Not a valid %1 file.", {"DXF"}));

    auto num = [](const DxfRecord& r, int code, double def = 0.0) {
        for (const DxfGroup& g : r.data)
            if (g.code == code) return std::strtod(g.value.c_str(), nullptr);
        return def;
    };
    auto text = [](const DxfRecord& r, int code) {
        for (const DxfGroup& g : r.data)
            if (g.code == code) return g.value;
        return std::string();
    };
    auto point = [&](const DxfRecord& r, int cx, int cy, int cz) { return Vec3{num(r, cx), num(r, cy), num(r, cz)}; };

    Scene scene;
    SceneLines lines;
    lines.name = tr8("Lines");
    std::map<int, DxfMeshAcc> meshes;  // by colour index (0 = default colour)
    std::map<std::string, int> layerAci;
    std::map<std::string, int> skipped;

    auto colorKey = [&](const DxfRecord& r) {
        int aci = 256;
        for (const DxfGroup& g : r.data)
            if (g.code == 62) aci = std::atoi(g.value.c_str());
        if (aci == 0 || aci == 256) {
            const auto it = layerAci.find(text(r, 8));
            aci = it == layerAci.end() ? 7 : it->second;
        }
        aci = std::abs(aci);
        std::array<float, 3> c;
        return aciColor(aci, c) ? aci : 0;
    };
    auto addTri = [&](int key, const Vec3& a, const Vec3& b, const Vec3& c) {
        DxfMeshAcc& acc = meshes[key];
        auto index = [&](const Vec3& p) {
            const auto ins = acc.weld.try_emplace(p, int(acc.mesh.points.size()));
            if (ins.second) acc.mesh.points.push_back(p);
            return ins.first->second;
        };
        const int ia = index(a), ib = index(b), ic = index(c);
        if (ia != ib && ib != ic && ia != ic) acc.mesh.triangles.push_back({ia, ib, ic});
    };
    auto addSeg = [&](const Vec3& a, const Vec3& b) {
        if (a != b) lines.segments.push_back({a, b});
    };
    auto addPolyline = [&](const std::vector<Vec3>& pts, bool closed) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) addSeg(pts[i], pts[i + 1]);
        if (closed && pts.size() > 2) addSeg(pts.back(), pts.front());
    };

    // POLYLINE entities span several records (POLYLINE, VERTEX..., SEQEND).
    struct Poly {
        bool active = false;
        int flags = 0, m = 0, n = 0, key = 0;
        Ocs ocs;
        std::vector<Vec3> pts;
        std::vector<double> bulges;  // per vertex, 0 for straight segments
        std::vector<std::array<int, 4>> faces;
    } poly;
    auto finishPoly = [&]() {
        if (!poly.active) return;
        poly.active = false;
        if (poly.flags & 64) {  // polyface mesh
            for (const auto& f : poly.faces) {
                std::vector<int> idx;
                for (int v : f)
                    if (v != 0 && size_t(std::abs(v)) <= poly.pts.size()) idx.push_back(std::abs(v) - 1);
                for (size_t k = 1; k + 1 < idx.size(); ++k)
                    addTri(poly.key, poly.pts[size_t(idx[0])], poly.pts[size_t(idx[k])], poly.pts[size_t(idx[k + 1])]);
            }
        } else if (poly.flags & 16) {  // polygon mesh, M x N grid
            const int M = poly.m, N = poly.n;
            if (M > 1 && N > 1 && poly.pts.size() >= size_t(M) * size_t(N)) {
                const int mEnd = (poly.flags & 1) ? M : M - 1, nEnd = (poly.flags & 32) ? N : N - 1;
                auto at = [&](int i, int j) { return poly.pts[size_t((i % M) * N + (j % N))]; };
                for (int i = 0; i < mEnd; ++i)
                    for (int j = 0; j < nEnd; ++j) {
                        addTri(poly.key, at(i, j), at(i + 1, j), at(i + 1, j + 1));
                        addTri(poly.key, at(i, j), at(i + 1, j + 1), at(i, j + 1));
                    }
            }
        } else {
            std::vector<Vec3> pts = expandBulges(poly.pts, poly.bulges, (poly.flags & 1) != 0);
            if (!(poly.flags & 8)) for (Vec3& p : pts) p = toWorld(poly.ocs, p);  // 2D polylines live in the OCS
            addPolyline(pts, false);
        }
    };

    std::string section;
    for (const DxfRecord& r : records) {
        const std::string& t = r.type;
        if (t == "SECTION") { section = text(r, 2); continue; }
        if (t == "ENDSEC") { finishPoly(); section.clear(); continue; }
        if (section == "TABLES" && t == "LAYER") {
            if (r.data.size() && std::any_of(r.data.begin(), r.data.end(), [](const DxfGroup& g) { return g.code == 62; }))
                layerAci[text(r, 2)] = int(num(r, 62, 7));
            continue;
        }
        if (section != "ENTITIES") continue;

        if (t == "VERTEX" && poly.active) {
            const int vf = int(num(r, 70));
            if (poly.flags & 64) {
                if ((vf & 128) && !(vf & 64))
                    poly.faces.push_back({int(num(r, 71)), int(num(r, 72)), int(num(r, 73)), int(num(r, 74))});
                else
                    poly.pts.push_back(point(r, 10, 20, 30));
            } else {
                poly.pts.push_back(point(r, 10, 20, 30));
                poly.bulges.push_back(num(r, 42));
            }
            continue;
        }
        if (t == "SEQEND") { finishPoly(); continue; }
        finishPoly();

        if (t == "3DFACE") {
            const Vec3 p0 = point(r, 10, 20, 30), p1 = point(r, 11, 21, 31), p2 = point(r, 12, 22, 32),
                       p3 = point(r, 13, 23, 33);
            const int key = colorKey(r);
            addTri(key, p0, p1, p2);
            if (p3 != p2) addTri(key, p0, p2, p3);
        } else if (t == "MESH") {
            std::vector<Vec3> verts;
            std::vector<int> faceItems;
            double px = 0, py = 0;
            int faceRemaining = 0;
            bool inFaces = false;
            for (const DxfGroup& g : r.data) {
                if (g.code == 10) px = std::strtod(g.value.c_str(), nullptr);
                else if (g.code == 20) py = std::strtod(g.value.c_str(), nullptr);
                else if (g.code == 30 && !inFaces) verts.push_back({px, py, std::strtod(g.value.c_str(), nullptr)});
                else if (g.code == 93) { faceRemaining = std::atoi(g.value.c_str()); inFaces = true; }
                else if (g.code == 94 || g.code == 95) inFaces = false;
                else if (g.code == 90 && inFaces && faceRemaining > 0) {
                    faceItems.push_back(std::atoi(g.value.c_str()));
                    --faceRemaining;
                }
            }
            const int key = colorKey(r);
            for (size_t i = 0; i < faceItems.size();) {
                const int n = faceItems[i++];
                if (n < 3 || i + size_t(n) > faceItems.size()) break;
                for (int k = 1; k + 1 < n; ++k) {
                    const int a = faceItems[i], b = faceItems[i + size_t(k)], c = faceItems[i + size_t(k) + 1];
                    if (a >= 0 && b >= 0 && c >= 0 && size_t(a) < verts.size() && size_t(b) < verts.size() &&
                        size_t(c) < verts.size())
                        addTri(key, verts[size_t(a)], verts[size_t(b)], verts[size_t(c)]);
                }
                i += size_t(n);
            }
        } else if (t == "POLYLINE") {
            poly = Poly();
            poly.active = true;
            poly.flags = int(num(r, 70));
            poly.m = int(num(r, 71));
            poly.n = int(num(r, 72));
            poly.key = colorKey(r);
            poly.ocs = makeOcs(num(r, 210), num(r, 220), num(r, 230, 1.0));
        } else if (t == "LINE") {
            addSeg(point(r, 10, 20, 30), point(r, 11, 21, 31));
        } else if (t == "LWPOLYLINE") {
            std::vector<Vec3> pts;
            std::vector<double> bulges;
            const double elevation = num(r, 38);
            double px = 0;
            for (const DxfGroup& g : r.data) {
                if (g.code == 10) px = std::strtod(g.value.c_str(), nullptr);
                else if (g.code == 20) { pts.push_back({px, std::strtod(g.value.c_str(), nullptr), elevation}); bulges.push_back(0.0); }
                else if (g.code == 42 && !bulges.empty()) bulges.back() = std::strtod(g.value.c_str(), nullptr);
            }
            const bool closed = (int(num(r, 70)) & 1) != 0;
            const Ocs ocs = makeOcs(num(r, 210), num(r, 220), num(r, 230, 1.0));
            std::vector<Vec3> flat = expandBulges(pts, bulges, closed);
            for (Vec3& p : flat) p = toWorld(ocs, p);
            addPolyline(flat, false);
        } else if (t == "CIRCLE" || t == "ARC") {
            const Vec3 c = point(r, 10, 20, 30);
            const Ocs ocs = makeOcs(num(r, 210), num(r, 220), num(r, 230, 1.0));
            const double radius = num(r, 40);
            double a0 = 0, a1 = 360;
            if (t == "ARC") {
                a0 = num(r, 50);
                a1 = num(r, 51);
                if (a1 <= a0) a1 += 360;
            }
            const int steps = std::max(8, int(std::ceil((a1 - a0) / 360.0 * 64.0)));
            std::vector<Vec3> pts;
            for (int i = 0; i <= steps; ++i) {
                const double a = (a0 + (a1 - a0) * i / steps) * 3.14159265358979323846 / 180.0;
                pts.push_back(toWorld(ocs, {c[0] + radius * std::cos(a), c[1] + radius * std::sin(a), c[2]}));
            }
            addPolyline(pts, false);
        } else {
            ++skipped[t];
        }
    }
    finishPoly();

    for (auto& kv : meshes) {
        DxfMeshAcc& acc = kv.second;
        if (acc.mesh.triangles.empty()) continue;
        acc.mesh.name = kv.first == 0 ? tr8("DXF mesh") : tr8f("DXF mesh (colour %1)", {std::to_string(kv.first)});
        acc.mesh.hasColor = aciColor(kv.first, acc.mesh.color);
        scene.meshes.push_back(std::move(acc.mesh));
    }
    if (!lines.segments.empty()) scene.lines.push_back(std::move(lines));

    if (scene.meshes.empty() && scene.lines.empty()) {
        std::string msg = tr8("No drawable geometry found in this DXF.");
        if (!skipped.empty()) {
            msg += std::string(" ") + tr8("Unsupported entities:");
            int shown = 0;
            for (const auto& kv : skipped) {
                if (shown++ == 5) { msg += " ..."; break; }
                msg += (shown > 1 ? ", " : " ") + kv.first + " (" + std::to_string(kv.second) + ")";
            }
            msg += ".";
        }
        throw std::runtime_error(msg);
    }
    return scene;
}

std::vector<std::string> zipModelEntries(const std::string& zipPath, const std::vector<std::string>& extensions) {
    ZipFile zip(zipPath);
    std::vector<std::string> out;
    for (const ZipEntry& e : zip.entries) {
        if (isJunkEntry(e.name)) continue;
        if (std::find(extensions.begin(), extensions.end(), lowerExtension(e.name)) != extensions.end()) out.push_back(e.name);
    }
    return out;
}

std::string extractZipModel(const std::string& zipPath, const std::string& entry, const std::string& destDir,
                            const std::vector<std::string>& modelExtensions, int* skipped) {
    namespace fs = std::filesystem;
    if (skipped) *skipped = 0;
    ZipFile zip(zipPath);
    const ZipEntry* model = nullptr;
    for (const ZipEntry& e : zip.entries)
        if (e.name == entry) model = &e;
    if (!model) throw std::runtime_error(tr8("The model is no longer in the zip file."));

    const fs::path dest = fs::u8path(destDir);
    fs::create_directories(dest);
    auto write = [&](const ZipEntry& e, const std::string& relative) {
        const fs::path rel = fs::u8path(relative).lexically_normal();
        if (rel.empty() || rel.is_absolute() || rel.has_root_name() || *rel.begin() == "..") return fs::path();  // unsafe name
        const Bytes data = zip.read(e);
        const fs::path target = dest / rel;
        fs::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) throw std::runtime_error(tr8("The model could not be unpacked from the zip file."));
        return target;
    };

    const size_t slash = entry.find_last_of('/');
    const std::string folder = slash == std::string::npos ? std::string() : entry.substr(0, slash + 1);
    const fs::path written = write(*model, entry.substr(folder.size()));
    if (written.empty()) throw zip.invalid();

    // OBJ, glTF and VRML files point to other files (materials, textures, buffers): unpack the files next to the model
    // too, but not other models.
    const std::string ext = lowerExtension(entry);
    if (ext == "obj" || ext == "gltf" || ext == "wrl" || ext == "vrml") {
        uint64_t total = 0;
        for (const ZipEntry& e : zip.entries) {
            if (&e == model || isJunkEntry(e.name) || e.name.compare(0, folder.size(), folder) != 0) continue;
            if (std::find(modelExtensions.begin(), modelExtensions.end(), lowerExtension(e.name)) != modelExtensions.end())
                continue;
            total += e.usize;
            if (total > (uint64_t(2) << 30)) break;  // 2 GB of extra files is more than any model needs
            // A side file that cannot be written (a name Windows does not allow, a full disk, a damaged entry) only loses
            // that texture/material; the caller says how many were skipped.
            bool ok = false;
            try {
                ok = !write(e, e.name.substr(folder.size())).empty();
            } catch (const std::exception&) {
            }
            if (!ok && skipped) ++*skipped;
        }
    }
    return written.u8string();
}
