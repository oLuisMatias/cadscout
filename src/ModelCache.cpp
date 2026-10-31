#include "ModelCache.h"

#include "Translate.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <iterator>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <appmodel.h>

#ifndef CADSCOUT_VERSION
#define CADSCOUT_VERSION "dev"
#endif

namespace ModelCache {
namespace {

constexpr char kMagic[4] = {'C', 'S', 'C', '4'};  // change the last character when the stored layout changes

QString folder() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/models";
    QDir().mkpath(dir);
    return dir;
}

// File name of the entry for this file: <app version>_<hash of path, size, modification time and quality>.cache
QString entryName(const QString& path, int quality) {
    const QFileInfo info(path);
    const QString key = QString("%1|%2|%3|%4|%5")
                            .arg(QDir::toNativeSeparators(info.absoluteFilePath()).toLower())
                            .arg(info.size())
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(quality)
                            .arg(int(currentLanguage()));  // part names and info are stored translated
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return QString("%1_%2.cache").arg(CADSCOUT_VERSION, hash);
}

class Writer {
public:
    explicit Writer(QFile& f) : f_(f) {}
    bool ok() const { return ok_; }
    void raw(const void* p, qint64 n) {
        if (ok_ && n > 0 && f_.write(static_cast<const char*>(p), n) != n) ok_ = false;
    }
    template <class T> void pod(const T& v) { raw(&v, sizeof(T)); }
    void str(const std::string& s) {
        pod<uint64_t>(s.size());
        raw(s.data(), qint64(s.size()));
    }
    template <class T> void vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        pod<uint64_t>(v.size());
        raw(v.data(), qint64(v.size() * sizeof(T)));
    }

private:
    QFile& f_;
    bool ok_ = true;
};

// Reads what Writer wrote. Counts are checked against the bytes left in the file, so a damaged entry fails instead of
// allocating huge amounts of memory.
class Reader {
public:
    explicit Reader(QFile& f) : f_(f) {}
    bool ok() const { return ok_; }
    void raw(void* p, qint64 n) {
        if (ok_ && n > 0 && f_.read(static_cast<char*>(p), n) != n) ok_ = false;
    }
    template <class T> T pod() {
        T v{};
        raw(&v, sizeof(T));
        return v;
    }
    uint64_t count(size_t elementBytes) {
        const uint64_t n = pod<uint64_t>();
        if (ok_ && n > uint64_t(f_.size() - f_.pos()) / std::max<size_t>(elementBytes, 1)) ok_ = false;
        return ok_ ? n : 0;
    }
    std::string str() {
        std::string s(size_t(count(1)), '\0');
        raw(s.data(), qint64(s.size()));
        return ok_ ? s : std::string();
    }
    template <class T> std::vector<T> vec() {
        static_assert(std::is_trivially_copyable_v<T>);
        std::vector<T> v(size_t(count(sizeof(T))));
        raw(v.data(), qint64(v.size() * sizeof(T)));
        return v;
    }

private:
    QFile& f_;
    bool ok_ = true;
};

void writeModel(Writer& w, const Model& m) {
    w.pod<uint64_t>(m.batches.size());
    for (const MeshBatch& b : m.batches) {
        w.pod(b.color);
        w.vec(b.vertices);
        w.vec(b.indices);
    }
    w.pod<uint64_t>(m.parts.size());
    for (const Part& p : m.parts) {
        w.str(p.name);
        w.vec(p.ranges);
        w.str(p.object);
        w.pod<uint64_t>(p.filaments.size());
        for (const FilamentUse& f : p.filaments) {
            w.pod(f.number);
            w.pod(f.hasColor);
            w.pod(f.color);
            w.str(f.type);
            w.str(f.vendor);
        }
        w.pod(p.edgeFirst);
        w.pod(p.edgeCount);
    }
    w.pod<uint64_t>(m.nodes.size());
    for (const Node& n : m.nodes) {
        w.str(n.name);
        w.pod(n.parent);
        w.pod(n.part);
    }
    w.vec(m.edges);
    w.pod(m.boundsMin);
    w.pod(m.boundsMax);
    w.pod<uint64_t>(m.triangleCount);
    w.pod<uint64_t>(m.faceCount);
    w.str(m.formatName);
    w.pod<uint64_t>(m.plates.size());
    for (const PlateShape& p : m.plates) {
        w.vec(p.outline);
        w.str(p.name);
        w.pod(p.printSeconds);
    }
    w.str(m.extraInfo);
    w.pod(m.singleObject);
    w.pod<uint64_t>(m.sections.size());
    for (const InfoSection& s : m.sections) {
        w.str(s.title);
        w.pod<uint64_t>(s.rows.size());
        for (const InfoRow& r : s.rows) {
            w.str(r.label);
            w.str(r.value);
            w.str(r.swatch);
        }
    }
}

std::shared_ptr<Model> readModel(Reader& r) {
    auto m = std::make_shared<Model>();
    m->batches.resize(size_t(r.count(sizeof(float) * 3)));
    for (MeshBatch& b : m->batches) {
        b.color = r.pod<std::array<float, 3>>();
        b.vertices = r.vec<float>();
        b.indices = r.vec<uint32_t>();
    }
    m->parts.resize(size_t(r.count(8)));
    for (Part& p : m->parts) {
        p.name = r.str();
        p.ranges = r.vec<PartRange>();
        p.object = r.str();
        p.filaments.resize(size_t(r.count(8)));
        for (FilamentUse& f : p.filaments) {
            f.number = r.pod<int>();
            f.hasColor = r.pod<bool>();
            f.color = r.pod<std::array<float, 3>>();
            f.type = r.str();
            f.vendor = r.str();
        }
        p.edgeFirst = r.pod<uint32_t>();
        p.edgeCount = r.pod<uint32_t>();
    }
    m->nodes.resize(size_t(r.count(8)));
    for (Node& n : m->nodes) {
        n.name = r.str();
        n.parent = r.pod<int>();
        n.part = r.pod<int>();
    }
    m->edges = r.vec<float>();
    m->boundsMin = r.pod<std::array<float, 3>>();
    m->boundsMax = r.pod<std::array<float, 3>>();
    m->triangleCount = size_t(r.pod<uint64_t>());
    m->faceCount = size_t(r.pod<uint64_t>());
    m->formatName = r.str();
    m->plates.resize(size_t(r.count(8)));
    for (PlateShape& p : m->plates) {
        p.outline = r.vec<std::array<float, 2>>();
        p.name = r.str();
        p.printSeconds = r.pod<double>();
    }
    m->extraInfo = r.str();
    m->singleObject = r.pod<bool>();
    m->sections.resize(size_t(r.count(16)));
    for (InfoSection& s : m->sections) {
        s.title = r.str();
        s.rows.resize(size_t(r.count(24)));
        for (InfoRow& row : s.rows) {
            row.label = r.str();
            row.value = r.str();
            row.swatch = r.str();
        }
    }
    if (!r.ok()) return nullptr;
    // Indices must point inside their batch, nodes and parts inside their lists: a damaged entry must never reach the GPU.
    for (const MeshBatch& b : m->batches) {
        const uint32_t vertices = uint32_t(b.vertices.size() / 6);
        if (b.vertices.size() % 6 != 0) return nullptr;
        for (uint32_t i : b.indices)
            if (i >= vertices) return nullptr;
    }
    for (const Part& p : m->parts)
        if (uint64_t(p.edgeFirst) + p.edgeCount > m->edges.size() / 3) return nullptr;
    for (const Part& p : m->parts)
        for (const PartRange& pr : p.ranges)
            if (pr.batch < 0 || size_t(pr.batch) >= m->batches.size() ||
                uint64_t(pr.start) + pr.count > m->batches[size_t(pr.batch)].indices.size())
                return nullptr;
    for (const Node& n : m->nodes)
        if (n.part >= int(m->parts.size()) || n.parent >= int(m->nodes.size())) return nullptr;
    return m;
}

}  // namespace

bool enabled() { return QSettings().value("cache/enabled", true).toBool(); }
int limitMegabytes() { return std::clamp(QSettings().value("cache/limitMb", 2048).toInt(), 100, 20000); }
double minLoadSeconds() {
    return std::clamp(QSettings().value("cache/minSeconds", kDefaultMinLoadSeconds).toDouble(), 0.0, 600.0);
}
void setEnabled(bool on) { QSettings().setValue("cache/enabled", on); }
void setLimitMegabytes(int mb) { QSettings().setValue("cache/limitMb", mb); }
void setMinLoadSeconds(double seconds) { QSettings().setValue("cache/minSeconds", seconds); }

std::shared_ptr<Model> load(const QString& path, int quality) {
    const auto start = std::chrono::steady_clock::now();
    const QString file = folder() + "/" + entryName(path, quality);
    QFile f(file);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return nullptr;
    char magic[4] = {};
    std::shared_ptr<Model> model;
    if (f.read(magic, 4) == 4 && std::memcmp(magic, kMagic, 4) == 0) {
        Reader r(f);
        model = readModel(r);
    }
    f.close();
    if (!model) {
        QFile::remove(file);  // damaged or from another layout: forget it
        return nullptr;
    }
    // Mark as recently used (the oldest entries are deleted first).
    QFile touch(file);
    if (touch.open(QIODevice::ReadWrite)) touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    model->loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    model->fromCache = true;
    return model;
}

void store(const QString& path, int quality, const Model& model, int limitMb, double minSeconds) {
    if (model.loadSeconds < minSeconds) return;
    const QString dir = folder();
    const QString file = dir + "/" + entryName(path, quality);
    // Own name per program window (process), so two windows saving at once never write into the same file.
    const QString temp = file + "." + QString::number(QCoreApplication::applicationPid()) + ".tmp";
    {
        QFile f(temp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        f.write(kMagic, 4);
        Writer w(f);
        writeModel(w, model);
        const bool ok = w.ok();
        f.close();
        if (!ok) {
            QFile::remove(temp);
            return;
        }
    }
    QFile::remove(file);
    if (!QFile::rename(temp, file)) {
        QFile::remove(temp);
        return;
    }

    // Keep the cache small: entries of other app versions go first, then the least recently used until it fits.
    const QString prefix = QString("%1_").arg(CADSCOUT_VERSION);
    QFileInfoList entries = QDir(dir).entryInfoList({"*.cache", "*.tmp"}, QDir::Files, QDir::Time);  // newest first
    qint64 total = 0;
    const QDateTime staleBefore = QDateTime::currentDateTime().addSecs(-15 * 60);
    for (const QFileInfo& e : entries) {
        if (e.suffix() == "tmp") {
            // Left behind by a program that was closed while saving. A recent one may still be written by another window.
            if (e.lastModified() < staleBefore) QFile::remove(e.absoluteFilePath());
        } else if (!e.fileName().startsWith(prefix)) {
            QFile::remove(e.absoluteFilePath());
        } else {
            total += e.size();
        }
    }
    const qint64 limit = qint64(std::max(limitMb, 100)) * 1024 * 1024;
    for (int i = int(entries.size()) - 1; i >= 0 && total > limit; --i) {  // oldest last
        const QFileInfo& e = entries[i];
        if (!e.fileName().startsWith(prefix) || e.suffix() == "tmp" || e.absoluteFilePath() == file) continue;
        if (QFile::remove(e.absoluteFilePath())) total -= e.size();
    }
}

qint64 sizeBytes() {
    qint64 total = 0;
    for (const QFileInfo& e : QDir(folder()).entryInfoList(QDir::Files)) total += e.size();
    return total;
}

void clear() {
    for (const QFileInfo& e : QDir(folder()).entryInfoList(QDir::Files)) QFile::remove(e.absoluteFilePath());
}

bool openFolder() {
    QString path = QDir::toNativeSeparators(folder());
    // Opening the folder from inside the app gives its real place on disk (in the Store package: under
    // %LOCALAPPDATA%\Packages\...\LocalCache), which is the one Explorer can show.
    const HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        wchar_t buffer[1024];
        const DWORD n = GetFinalPathNameByHandleW(h, buffer, DWORD(std::size(buffer)), FILE_NAME_NORMALIZED);
        CloseHandle(h);
        if (n > 0 && n < std::size(buffer)) {
            QString real = QString::fromWCharArray(buffer, int(n));
            if (real.startsWith("\\\\?\\UNC\\")) real = "\\\\" + real.mid(8);
            else if (real.startsWith("\\\\?\\")) real = real.mid(4);
            path = real;
        }
    }
    // Backup for the Store package when the look-up above still gives the normal AppData path: Windows keeps a packaged
    // app's AppData files in %LOCALAPPDATA%\Packages\<package family name>\LocalCache\Local\...
    wchar_t family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
    UINT32 familyLength = UINT32(std::size(family));
    const QString local = QDir::toNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
    if (GetCurrentPackageFamilyName(&familyLength, family) == ERROR_SUCCESS && !local.isEmpty() &&
        path.startsWith(local + "\\", Qt::CaseInsensitive) && !path.contains("\\Packages\\", Qt::CaseInsensitive)) {
        const QString packaged = local + "\\Packages\\" + QString::fromWCharArray(family) + "\\LocalCache\\Local" +
                                 path.mid(local.size());
        if (QFileInfo::exists(packaged)) path = packaged;
    }
    return QProcess::startDetached("explorer.exe", {path});
}

}  // namespace ModelCache
