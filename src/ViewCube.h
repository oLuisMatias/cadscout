#pragma once
#include <QQuaternion>
#include <QVector3D>
#include <QWidget>

// Clickable orientation cube with an axis triad. Z is up in world space.
class ViewCube : public QWidget {
    Q_OBJECT
public:
    explicit ViewCube(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {150, 140}; }

public slots:
    void setOrientation(const QQuaternion& camToWorld);

signals:
    // Direction from the model towards the camera.
    void viewRequested(const QVector3D& direction);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    struct Hit {
        int face = -1;
        int zoneA = 1, zoneB = 1;  // 0, 1, 2 along the face axes (1 = centre)
    };
    QPointF project(const QVector3D& worldPoint) const;
    QPointF facePoint(int face, float a, float b) const;
    bool faceVisible(int face) const;
    Hit hitTest(const QPointF& p) const;
    QVector3D hitDirection(const Hit& h) const;

    QQuaternion orientation_;
    QPointF center_{88, 58};
    float scale_ = 32.f;
    Hit hover_;
};
