#include "ClipboardImage.h"

#include <QByteArray>
#include <array>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ClipboardImage {
namespace {

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void appendBigEndian(QByteArray& out, uint32_t v) {
    const char b[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
    out.append(b, 4);
}

void appendChunk(QByteArray& out, const char type[4], const QByteArray& data) {
    appendBigEndian(out, uint32_t(data.size()));
    QByteArray body(type, 4);
    body.append(data);
    out.append(body);
    appendBigEndian(out, crc32(reinterpret_cast<const uint8_t*>(body.constData()), size_t(body.size())));
}

}  // namespace

QByteArray encodePng(const QImage& source) {
    if (source.isNull()) return {};
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);  // straight (not premultiplied) alpha
    const int w = image.width(), h = image.height();

    QByteArray raw;
    raw.reserve(qsizetype(w * 4 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw.append('\0');  // filter type 0: none
        raw.append(reinterpret_cast<const char*>(image.constScanLine(y)), w * 4);
    }
    // Light compression: several times faster than the default for big pictures, and the file is only a little bigger.
    QByteArray zlibData = qCompress(raw, 2);  // 4 bytes with the original size, then a plain zlib stream
    if (zlibData.size() <= 4) return {};
    zlibData.remove(0, 4);

    QByteArray png("\x89PNG\r\n\x1a\n", 8);
    QByteArray header;
    appendBigEndian(header, uint32_t(w));
    appendBigEndian(header, uint32_t(h));
    header.append(char(8));  // bit depth
    header.append(char(6));  // colour type: RGBA
    header.append(char(0));  // compression
    header.append(char(0));  // filter
    header.append(char(0));  // no interlacing
    appendChunk(png, "IHDR", header);
    appendChunk(png, "IDAT", zlibData);
    appendChunk(png, "IEND", QByteArray());
    return png;
}

bool copy(const QImage& image, void* ownerWindow) {
    if (image.isNull()) return false;
    const QByteArray png = encodePng(image);
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);  // BGRA bytes, straight alpha
    const int w = argb.width(), h = argb.height();

    // CF_DIBV5: header followed by the pixels, bottom row first.
    const SIZE_T pixelBytes = SIZE_T(w) * h * 4;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + pixelBytes);
    HGLOBAL pngMemory = png.isEmpty() ? nullptr : GlobalAlloc(GMEM_MOVEABLE, SIZE_T(png.size()));
    if (!dib) {
        if (pngMemory) GlobalFree(pngMemory);
        return false;
    }
    if (auto* p = static_cast<char*>(GlobalLock(dib))) {
        auto* head = reinterpret_cast<BITMAPV5HEADER*>(p);
        std::memset(head, 0, sizeof(BITMAPV5HEADER));
        head->bV5Size = sizeof(BITMAPV5HEADER);
        head->bV5Width = w;
        head->bV5Height = h;
        head->bV5Planes = 1;
        head->bV5BitCount = 32;
        head->bV5Compression = BI_BITFIELDS;
        head->bV5SizeImage = DWORD(pixelBytes);
        head->bV5RedMask = 0x00FF0000;
        head->bV5GreenMask = 0x0000FF00;
        head->bV5BlueMask = 0x000000FF;
        head->bV5AlphaMask = 0xFF000000;
        head->bV5CSType = LCS_sRGB;
        head->bV5Intent = LCS_GM_IMAGES;
        char* pixels = p + sizeof(BITMAPV5HEADER);
        for (int y = 0; y < h; ++y) std::memcpy(pixels + SIZE_T(y) * w * 4, argb.constScanLine(h - 1 - y), SIZE_T(w) * 4);
        GlobalUnlock(dib);
    }
    if (pngMemory) {
        if (void* p = GlobalLock(pngMemory)) {
            std::memcpy(p, png.constData(), size_t(png.size()));
            GlobalUnlock(pngMemory);
        }
    }

    bool ok = false;
    if (OpenClipboard(static_cast<HWND>(ownerWindow))) {
        EmptyClipboard();
        const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        // The clipboard owns a block once SetClipboardData succeeded; free it ourselves only when that failed.
        if (pngMemory && pngFormat && SetClipboardData(pngFormat, pngMemory)) pngMemory = nullptr;
        if (SetClipboardData(CF_DIBV5, dib)) {
            dib = nullptr;
            ok = true;
        }
        CloseClipboard();
    }
    if (dib) GlobalFree(dib);
    if (pngMemory) GlobalFree(pngMemory);
    return ok;
}

}  // namespace ClipboardImage
