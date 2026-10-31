#pragma once
#include <QImage>

// Puts a picture on the Windows clipboard so the background stays transparent where the image is transparent.
//
// Qt in this build has no PNG support, so the PNG is written by hand (zlib from QtCore plus a CRC table). The clipboard gets
// two formats: "PNG" (read by Word, PowerPoint, Teams, Slack, browsers, GIMP, Paint.NET) and CF_DIBV5 (32 bit with alpha,
// read by other programs; Windows also derives a plain bitmap from it). Programs that only read a plain bitmap show the
// transparent parts in their own background colour.
namespace ClipboardImage {

QByteArray encodePng(const QImage& image);       // RGBA PNG, empty on failure
bool copy(const QImage& image, void* ownerWindow);  // ownerWindow: HWND of the main window (may be null)

}  // namespace ClipboardImage
