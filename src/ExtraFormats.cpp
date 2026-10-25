#include "ExtraFormats.h"

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

Bytes readWholeFile(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) throw std::runtime_error("Could not read the file.");
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

uint32_t rd16(const Bytes& b, size_t o) { return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8); }
uint32_t rd32(const Bytes& b, size_t o) { return rd16(b, o) | (rd16(b, o + 2) << 16); }

uint32_t quantize(float v) { return uint32_t(std::lround(std::fmin(std::fmax(v, 0.f), 1.f) * 255.f)); }

// ---------------------------------------------------------------- zip (just enough for 3MF)

struct ZipEntry {
    std::string name;
    uint32_t method = 0, csize = 0, usize = 0, offset = 0;
};

std::runtime_error badZip() { return std::runtime_error("The 3MF file is not a valid zip archive."); }

std::vector<ZipEntry> zipEntries(const Bytes& z) {
    if (z.size() < 22) throw badZip();
    const size_t lowest = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0;
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22;; --i) {
        if (rd32(z, i) == 0x06054b50) { eocd = i; break; }
        if (i == lowest) break;
    }
    if (eocd == std::string::npos) throw badZip();
    const uint32_t count = rd16(z, eocd + 10);
    size_t p = rd32(z, eocd + 16);
    std::vector<ZipEntry> out;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 46 > z.size() || rd32(z, p) != 0x02014b50) throw badZip();
        ZipEntry e;
        e.method = rd16(z, p + 10);
        e.csize = rd32(z, p + 20);
        e.usize = rd32(z, p + 24);
        e.offset = rd32(z, p + 42);
        if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu || e.offset == 0xFFFFFFFFu)
            throw std::runtime_error("This 3MF file is larger than 4 GB (zip64), which is not supported.");
        const size_t nameLen = rd16(z, p + 28), extraLen = rd16(z, p + 30), commentLen = rd16(z, p + 32);
        if (p + 46 + nameLen > z.size()) throw badZip();
        e.name.assign(reinterpret_cast<const char*>(z.data() + p + 46), nameLen);
        out.push_back(std::move(e));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return out;
}

Bytes zipRead(const Bytes& z, const ZipEntry& e) {
    if (size_t(e.offset) + 30 > z.size() || rd32(z, e.offset) != 0x04034b50) throw badZip();
    const size_t start = size_t(e.offset) + 30 + rd16(z, e.offset + 26) + rd16(z, e.offset + 28);
    if (start + e.csize > z.size()) throw badZip();
    if (e.method == 0) return Bytes(z.begin() + std::ptrdiff_t(start), z.begin() + std::ptrdiff_t(start + e.csize));
    if (e.method != 8) throw std::runtime_error("The 3MF file uses an unsupported zip compression.");
    // Deflate cannot shrink data by more than about 1000:1: a bigger claim in the header means a damaged file.
    if (e.usize > uint64_t(e.csize) * 1100 + 4096) throw badZip();
    Bytes out(e.usize);
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) throw badZip();
    zs.next_in = const_cast<Bytef*>(z.data() + start);
    zs.avail_in = e.csize;
    zs.next_out = out.data();
    zs.avail_out = e.usize;
    const int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END && !(rc == Z_OK && zs.avail_out == 0)) throw badZip();
    return out;
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

std::string unescapeXml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const size_t semi = s.find(';', i);
            if (semi != std::string_view::npos) {
                const std::string_view ent = s.substr(i + 1, semi - i - 1);
                char rep = 0;
                if (ent == "amp") rep = '&';
                else if (ent == "lt") rep = '<';
                else if (ent == "gt") rep = '>';
                else if (ent == "quot") rep = '"';
                else if (ent == "apos") rep = '\'';
                if (rep) { out += rep; i = semi; continue; }
            }
        }
        out += s[i];
    }
    return out;
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
            if (t.get("name") == "Application" && !t.selfClosing)
                f.application = unescapeXml(trimView(tk.textAfterTag()));
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

struct ProjectInfo {
    std::vector<std::array<float, 3>> filamentColors;
    std::vector<std::string> filamentTypes;
    std::string printer;
    double nozzle = 0;
    std::vector<std::array<double, 2>> bed;  // printable_area polygon, plate-local millimetres
};

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
    info.printer = obj.value("printer_model").toString().toStdString();
    for (const QJsonValue& v : obj.value("printable_area").toArray()) {  // entries look like "256x0"
        const std::string s = v.toString().toStdString();
        const size_t x = s.find('x');
        if (x == std::string::npos) continue;
        info.bed.push_back({toDouble(std::string_view(s).substr(0, x)), toDouble(std::string_view(s).substr(x + 1))});
    }
    const QJsonArray nozzles = obj.value("nozzle_diameter").toArray();
    if (!nozzles.isEmpty()) info.nozzle = nozzles.first().toString().toDouble();
    return info;
}

struct SlicePlate {
    int index = 0;
    double seconds = -1, grams = -1;
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

struct Package {
    Bytes zip;
    std::vector<ZipEntry> entries;
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
    Package pkg;
    pkg.zip = readWholeFile(path);
    pkg.entries = zipEntries(pkg.zip);

    const ZipEntry* rootEntry = pkg.find("3D/3dmodel.model");
    if (!rootEntry) {
        for (const ZipEntry& e : pkg.entries) {
            const std::string lower = Package::norm(e.name);
            if (lower.size() > 6 && lower.compare(lower.size() - 6, 6, ".model") == 0) { rootEntry = &e; break; }
        }
    }
    if (!rootEntry) throw std::runtime_error("The 3MF file contains no 3D model.");
    const std::string rootKey = Package::norm(rootEntry->name);

    // Model files are parsed on first use: Bambu/Orca keep each object in its own 3D/Objects/object_N.model file.
    std::map<std::string, ModelFile> files;
    auto getFile = [&](const std::string& name) -> const ModelFile& {
        const std::string key = name.empty() ? rootKey : Package::norm(name);
        const auto it = files.find(key);
        if (it != files.end()) return it->second;
        const ZipEntry* e = pkg.find(key);
        if (!e) throw std::runtime_error("The 3MF file refers to \"" + name + "\", which is missing from the archive.");
        return files.emplace(key, parseModelXml(zipRead(pkg.zip, *e))).first->second;
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
                    throw std::runtime_error("The 3MF file has an invalid vertex index.");
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
                if (kv.second.filament > 0) std::snprintf(buf, sizeof buf, " (filament %d)", kv.second.filament);
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
                emitMeshes(f, o, o.name.empty() ? "Object " + std::to_string(id) : o.name, m, 0, SIZE_MAX, plainColor,
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
        if (scene.meshes.empty()) throw std::runtime_error("The 3MF file contains no meshes.");
        return scene;
    }

    // ---- Bambu Studio / Orca Slicer project: plates -> objects -> parts, filament colours, print info.
    BambuCfg cfg;
    if (const ZipEntry* e = pkg.find("Metadata/model_settings.config")) cfg = parseModelSettings(zipRead(pkg.zip, *e));
    ProjectInfo project;
    if (const ZipEntry* e = pkg.find("Metadata/project_settings.config")) project = parseProjectSettings(zipRead(pkg.zip, *e));
    std::vector<SlicePlate> sliced;
    if (const ZipEntry* e = pkg.find("Metadata/slice_info.config")) sliced = parseSliceInfo(zipRead(pkg.zip, *e));

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

    auto emitPart = [&](const ModelFile& f, const Obj3mf& o, const std::string& name, const Mat& m, size_t first,
                        size_t last, int extruder, SceneNode& parent) {
        std::vector<int> states;
        for (const std::string& s : f.paintStrings) states.push_back(dominantPaintState(s));
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
                                              : "Object " + std::to_string(item.object);
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
        std::string plateTitle = "Plate " + std::to_string(plate.id);
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
        pn.name += " · " + std::to_string(pn.children.size()) + (pn.children.size() == 1 ? " object" : " objects");
        for (const SlicePlate& sp : sliced) {
            if (sp.index != plate.id) continue;
            if (sp.seconds >= 0) pn.name += " · " + formatDuration(sp.seconds);
            if (sp.grams >= 0) pn.name += " · " + formatGrams(sp.grams);
        }
        scene.tree.push_back(std::move(pn));
    }
    if (scene.meshes.empty()) throw std::runtime_error("The 3MF file contains no meshes.");

    // Status-bar summary: printer, nozzle size and number of filaments used (plates, objects and print time are in the tree).
    std::string info;
    auto add = [&](const std::string& part) {
        if (!part.empty()) info += (info.empty() ? "" : "  ·  ") + part;
    };
    add(project.printer);
    if (project.nozzle > 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g mm nozzle", project.nozzle);
        add(buf);
    }
    add(std::to_string(scene.tree.size()) + (scene.tree.size() == 1 ? " plate" : " plates"));
    if (!usedFilaments.empty())
        add(std::to_string(usedFilaments.size()) + (usedFilaments.size() == 1 ? " filament" : " filaments"));
    scene.info = info;
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
    if (!in) throw std::runtime_error("Could not read DXF file.");
    char magic[18] = {};
    in.read(magic, 18);
    if (std::string(magic, size_t(in.gcount())) == "AutoCAD Binary DXF")
        throw std::runtime_error("Binary DXF files are not supported. Save the DXF as ASCII text.");
    in.clear();
    in.seekg(0);

    std::vector<DxfRecord> records;
    std::string codeLine, valueLine;
    while (std::getline(in, codeLine) && std::getline(in, valueLine)) {
        const std::string cs = trimmed(codeLine);
        if (cs.empty()) continue;
        char* end = nullptr;
        const long code = std::strtol(cs.c_str(), &end, 10);
        if (end == cs.c_str()) throw std::runtime_error("Not a valid DXF file.");
        std::string value = trimmed(valueLine);
        if (code == 0) records.push_back({value, {}});
        else if (!records.empty()) records.back().data.push_back({int(code), std::move(value)});
    }
    if (records.empty()) throw std::runtime_error("Not a valid DXF file.");

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
    lines.name = "Lines";
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
        acc.mesh.name = kv.first == 0 ? "DXF mesh" : "DXF mesh (colour " + std::to_string(kv.first) + ")";
        acc.mesh.hasColor = aciColor(kv.first, acc.mesh.color);
        scene.meshes.push_back(std::move(acc.mesh));
    }
    if (!lines.segments.empty()) scene.lines.push_back(std::move(lines));

    if (scene.meshes.empty() && scene.lines.empty()) {
        std::string msg = "No drawable geometry found in this DXF.";
        if (!skipped.empty()) {
            msg += " Unsupported entities:";
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
