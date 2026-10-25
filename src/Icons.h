#pragma once
#include <QByteArray>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>

// Tabler icons (MIT) bundled as Qt resources, recoloured by replacing currentColor.
inline QPixmap renderTablerIcon(const QString& name, const QColor& color, int size, qreal dpr) {
    QFile f(":/icons/" + name + ".svg");
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = f.readAll();
    svg.replace("currentColor", color.name().toUtf8());
    QSvgRenderer r(svg);
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    r.render(&p, QRectF(0, 0, size, size));
    return pm;
}

// Normal icon uses `normal`; checked buttons use `onColor` (text colour on the highlight).
inline QIcon tablerIcon(const QString& name, const QColor& normal, const QColor& onColor, int size = 20,
                        qreal dpr = 2.0) {
    QIcon icon;
    icon.addPixmap(renderTablerIcon(name, normal, size, dpr), QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderTablerIcon(name, onColor, size, dpr), QIcon::Normal, QIcon::On);
    return icon;
}
