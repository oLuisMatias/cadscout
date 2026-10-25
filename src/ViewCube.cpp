#include "ViewCube.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace {
struct FaceDef {
    QVector3D n, u, v;
    const char* label;
};

// Right-handed (u x v = n) so text is never mirrored.
const FaceDef kFaces[6] = {
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "RIGHT"},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, "LEFT"},
    {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, "BACK"},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, "FRONT"},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "TOP"},
    {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, "BOTTOM"},
};

constexpr float kZone = 0.6f;

float zoneLo(int z) { return z == 0 ? -1.f : (z == 1 ? -kZone : kZone); }
float zoneHi(int z) { return z == 0 ? -kZone : (z == 1 ? kZone : 1.f); }
}  // namespace

ViewCube::ViewCube(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
}

void ViewCube::setOrientation(const QQuaternion& q) {
    orientation_ = q;
    update();
}

QPointF ViewCube::project(const QVector3D& w) const {
    const QVector3D c = orientation_.conjugated().rotatedVector(w);
    return center_ + QPointF(c.x(), -c.y()) * scale_;
}

QPointF ViewCube::facePoint(int f, float a, float b) const {
    return project(kFaces[f].n + kFaces[f].u * a + kFaces[f].v * b);
}

bool ViewCube::faceVisible(int f) const {
    return orientation_.conjugated().rotatedVector(kFaces[f].n).z() > 0.f;
}

ViewCube::Hit ViewCube::hitTest(const QPointF& p) const {
    for (int f = 0; f < 6; ++f) {
        if (!faceVisible(f)) continue;
        const QPointF o = facePoint(f, 0, 0);
        const QPointF pu = facePoint(f, 1, 0) - o;
        const QPointF pv = facePoint(f, 0, 1) - o;
        const double det = pu.x() * pv.y() - pu.y() * pv.x();
        if (std::abs(det) < 1e-6) continue;
        const QPointF d = p - o;
        const double a = (d.x() * pv.y() - d.y() * pv.x()) / det;
        const double b = (pu.x() * d.y() - pu.y() * d.x()) / det;
        if (std::abs(a) > 1.0 || std::abs(b) > 1.0) continue;
        Hit h;
        h.face = f;
        h.zoneA = a < -kZone ? 0 : (a > kZone ? 2 : 1);
        h.zoneB = b < -kZone ? 0 : (b > kZone ? 2 : 1);
        return h;
    }
    return {};
}

QVector3D ViewCube::hitDirection(const Hit& h) const {
    const FaceDef& d = kFaces[h.face];
    return (d.n + d.u * float(h.zoneA - 1) + d.v * float(h.zoneB - 1)).normalized();
}

void ViewCube::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor base = palette().color(QPalette::Window);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor accent = palette().color(QPalette::Highlight);
    QColor edge = text;
    edge.setAlpha(150);

    // Draw back to front by view depth
    int order[6] = {0, 1, 2, 3, 4, 5};
    auto depth = [&](int f) { return orientation_.conjugated().rotatedVector(kFaces[f].n).z(); };
    std::sort(order, order + 6, [&](int a, int b) { return depth(a) < depth(b); });

    QFont font = p.font();
    font.setPixelSize(20);
    font.setBold(true);

    for (int f : order) {
        if (!faceVisible(f)) continue;
        QPolygonF poly;
        poly << facePoint(f, -1, -1) << facePoint(f, 1, -1) << facePoint(f, 1, 1) << facePoint(f, -1, 1);
        // Light, SolidWorks-like faces shaded by orientation
        const float lit = 0.80f + 0.20f * std::max(0.f, QVector3D::dotProduct(kFaces[f].n, QVector3D(0.3f, -0.5f, 0.8f).normalized()));
        QColor fill(int(238 * lit), int(240 * lit), int(244 * lit), 200);
        p.setPen(QPen(QColor(140, 146, 158), 1.2));
        p.setBrush(fill);
        p.drawPolygon(poly);

        const QPointF o = facePoint(f, 0, 0);
        const QPointF pu = facePoint(f, 1, 0) - o;
        const QPointF pv = facePoint(f, 0, 1) - o;
        p.save();
        p.setTransform(QTransform(pu.x(), pu.y(), -pv.x(), -pv.y(), o.x(), o.y()), true);
        p.scale(0.02, 0.02);
        p.setFont(font);
        p.setPen(QColor(70, 76, 90));
        p.drawText(QRectF(-50, -50, 100, 100), Qt::AlignCenter, kFaces[f].label);
        p.restore();
    }

    if (hover_.face >= 0 && faceVisible(hover_.face)) {
        const int f = hover_.face;
        const float a0 = zoneLo(hover_.zoneA), a1 = zoneHi(hover_.zoneA);
        const float b0 = zoneLo(hover_.zoneB), b1 = zoneHi(hover_.zoneB);
        QPolygonF z;
        z << facePoint(f, a0, b0) << facePoint(f, a1, b0) << facePoint(f, a1, b1) << facePoint(f, a0, b1);
        QColor hl = accent;
        hl.setAlpha(140);
        p.setPen(Qt::NoPen);
        p.setBrush(hl);
        p.drawPolygon(z);
    }

    // Axis triad inside the cube
    struct Axis { QVector3D dir; QColor color; const char* name; };
    const Axis axes[3] = {{{1, 0, 0}, QColor(230, 60, 60), "X"},
                          {{0, 1, 0}, QColor(60, 180, 80), "Y"},
                          {{0, 0, 1}, QColor(60, 110, 235), "Z"}};
    QFont small = p.font();
    small.setPixelSize(11);
    small.setBold(true);
    p.setFont(small);
    const QVector3D corner(0, 0, 0);
    const QPointF origin = project(corner);
    for (const Axis& a : axes) {
        const QPointF tip = project(corner + a.dir * 0.95f);
        p.setPen(QPen(a.color, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(origin, tip);
        p.setPen(a.color);
        const QPointF label = project(corner + a.dir * 1.3f);
        p.drawText(QRectF(label.x() - 8, label.y() - 8, 16, 16), Qt::AlignCenter, a.name);
    }
}


void ViewCube::mouseMoveEvent(QMouseEvent* e) {
    const Hit h = hitTest(e->position());
    if (h.face != hover_.face || h.zoneA != hover_.zoneA || h.zoneB != hover_.zoneB) {
        hover_ = h;
        update();
    }
}

void ViewCube::leaveEvent(QEvent*) {
    hover_ = {};
    update();
}

void ViewCube::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const Hit h = hitTest(e->position());
    if (h.face >= 0) emit viewRequested(hitDirection(h));
}
