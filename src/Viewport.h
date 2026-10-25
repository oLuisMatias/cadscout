#pragma once
#include <memory>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QQuaternion>
#include <QVector2D>
#include <QVector3D>
#include "Controls.h"
#include "PlateSettings.h"
#include "StepLoader.h"

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setModel(std::shared_ptr<Model> model);
    void setControls(const Controls& c) { controls_ = c; }
    const Controls& controls() const { return controls_; }
    void setPlateSettings(const PlateSettings& p);
    const PlateSettings& plateSettings() const { return plateSettings_; }
    bool edgesVisible() const { return showEdges_; }
    bool perspectiveEnabled() const { return perspective_; }
    bool hasModel() const { return model_ != nullptr; }
    void selectAt(const QPoint& pos);
    void selectPart(int part);

public slots:
    void fitToView();
    void goHome();
    void setViewDirection(const QVector3D& towardsCamera);
    void setEdgesVisible(bool v);
    void setPerspectiveEnabled(bool on);
    void setDarkTheme(bool dark);

signals:
    void pressed();  // any mouse button went down in the view
    void partClicked(int part, const QPoint& pos);   // a click (not a drag) on a part; pos in view coordinates
    void plateClicked(int plate, const QPoint& pos);  // a click on a file print plate (index into Model::plates)
    void orientationChanged(const QQuaternion& camToWorld);
    void partSelected(int part);  // -1 when the selection is cleared

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    struct GpuBatch {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
        int indexCount = 0;
        QVector3D color;
    };

    void uploadModel();
    void releaseGpu();
    void releasePlateGpu();
    void rebuildPlates();  // decides which print plates are shown (file / custom / none) and updates the bounds
    void updateBounds();   // centre and radius of the model plus the shown plates
    void uploadPlates();
    void drawPlates(const QMatrix4x4& mvp);
    int plateAt(const QPoint& pos) const;  // file plate under the cursor, -1 if none
    void drawMeshes(const QMatrix4x4& mvp, const QMatrix4x4& view);

    QMatrix4x4 projMatrix() const;
    QMatrix4x4 viewMatrix() const;
    QVector3D eyePosition() const;
    QVector3D cameraAxis(int i) const;  // 0 right, 1 up, 2 back (world space)
    void clipPlanes(float& nearP, float& farP) const;

    // Returns true and the world position of the surface under the widget position.
    bool pickPoint(const QPoint& pos, QVector3D& world, int* part = nullptr);
    QVector3D unproject(const QPoint& pos, float ndcZ) const;
    void scaleAbout(const QVector3D& p, float factor);
    void rotateAbout(const QVector3D& p, const QQuaternion& r);
    bool matches(const Binding& b, QMouseEvent* e) const;

    Controls controls_ = Controls::load();
    std::shared_ptr<Model> model_;
    bool pendingUpload_ = false;
    bool showEdges_ = true;
    bool perspective_ = false;
    bool dark_ = false;

    QOpenGLShaderProgram meshProg_, lineProg_, bgProg_;
    std::vector<std::unique_ptr<GpuBatch>> gpu_;
    QOpenGLVertexArrayObject edgeVao_, bgVao_;
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    int edgeVertexCount_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> pickFbo_;

    // Print plates (virtual bed under the model)
    struct PlateOutline {
        std::vector<QVector2D> pts;
        float z = 0.f;
        int fileIndex = -1;  // index into Model::plates for plates that come from the file, -1 for a custom plate
    };
    PlateSettings plateSettings_ = PlateSettings::load();
    std::vector<PlateOutline> plates_;
    bool platesDirty_ = false;
    QOpenGLVertexArrayObject plateFillVao_, plateLineVao_;
    QOpenGLBuffer plateFillVbo_{QOpenGLBuffer::VertexBuffer}, plateLineVbo_{QOpenGLBuffer::VertexBuffer};
    int plateFillCount_ = 0, plateGridCount_ = 0, plateOutlineCount_ = 0;

    // Camera: orientation_ maps camera axes to world; the eye sits at target_ + orientation_ * (0,0,distance_)
    QQuaternion orientation_;
    QVector3D target_{0, 0, 0};
    float distance_ = 10.f;
    QVector3D center_{0, 0, 0};
    float radius_ = 1.f;
    static constexpr float kFov = 45.f;

    enum class Drag { None, Orbit, Pan, Zoom } drag_ = Drag::None;
    Qt::MouseButton dragButton_ = Qt::NoButton;
    QPoint last_;
    QVector3D pivot_;
    float panDepth_ = 1.f;
    int selected_ = -1;
    QPoint pressPos_;
    bool leftPressed_ = false;
};
