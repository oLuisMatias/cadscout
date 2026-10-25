#include "Viewport.h"

#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
const char* kMeshVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat3 uNormalMat;
out vec3 vNormal;
void main() {
    vNormal = uNormalMat * aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kMeshFrag = R"(#version 330 core
in vec3 vNormal;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    vec3 l1 = normalize(vec3(0.35, 0.45, 1.0));
    vec3 l2 = normalize(vec3(-0.6, -0.2, 0.5));
    float d = abs(dot(n, l1)) * 0.65 + abs(dot(n, l2)) * 0.25;
    float s = pow(max(abs(n.z), 0.0), 24.0) * 0.12;
    fragColor = vec4(uColor * (0.22 + d) + vec3(s), 1.0);
}
)";

const char* kLineVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kLineFrag = R"(#version 330 core
uniform vec3 uColor;
uniform float uAlpha = 1.0;
out vec4 fragColor;
void main() { fragColor = vec4(uColor, uAlpha); }
)";

const char* kBgVert = R"(#version 330 core
out float vT;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vT = 1.0 - p.y * 0.5;
    gl_Position = vec4(p * 2.0 - 1.0, 0.999, 1.0);
}
)";

const char* kBgFrag = R"(#version 330 core
in float vT;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 fragColor;
void main() { fragColor = vec4(mix(uTop, uBottom, clamp(vT, 0.0, 1.0)), 1.0); }
)";

constexpr Qt::KeyboardModifiers kModMask = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier;

QQuaternion isoOrientation() {
    const QVector3D back = QVector3D(0.612f, -0.612f, 0.5f).normalized();
    const QVector3D right = QVector3D::crossProduct(QVector3D(0, 0, 1), back).normalized();
    const QVector3D up = QVector3D::crossProduct(back, right);
    return QQuaternion::fromAxes(right, up, back);
}
}  // namespace

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent), orientation_(isoOrientation()) {
    setFocusPolicy(Qt::StrongFocus);
}

Viewport::~Viewport() {
    makeCurrent();
    releaseGpu();
    releasePlateGpu();
    pickFbo_.reset();
    doneCurrent();
}

void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    auto build = [](QOpenGLShaderProgram& prog, const char* vertex, const char* fragment) {
        return prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) &&
               prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) && prog.link();
    };
    if (!build(meshProg_, kMeshVert, kMeshFrag) || !build(lineProg_, kLineVert, kLineFrag) ||
        !build(bgProg_, kBgVert, kBgFrag)) {
        const QString log = meshProg_.log() + lineProg_.log() + bgProg_.log();
        QTimer::singleShot(0, this, [this, log] {
            emit graphicsFailed("The graphics driver could not prepare the 3D view, so models cannot be shown. Please "
                                "update your graphics driver.\n\n" + log.left(400));
        });
    }
    bgVao_.create();

    if (pendingUpload_) uploadModel();
}

void Viewport::releaseGpu() {
    gpu_.clear();
    if (edgeVao_.isCreated()) edgeVao_.destroy();
    if (edgeVbo_.isCreated()) edgeVbo_.destroy();
    edgeVertexCount_ = 0;
}

void Viewport::setEdgesVisible(bool v) {
    showEdges_ = v;
    update();
}

void Viewport::setDarkTheme(bool dark) {
    dark_ = dark;
    update();
}

void Viewport::setModel(std::shared_ptr<Model> model) {
    model_ = std::move(model);
    wheelValid_ = false;
    selected_ = -1;
    emit partSelected(-1);
    pendingUpload_ = true;
    rebuildPlates();  // also sets the centre and radius used by "Fit"
    orientation_ = isoOrientation();
    if (isValid()) {
        makeCurrent();
        uploadModel();
        doneCurrent();
    }
    fitToView();
}

void Viewport::setPlateSettings(const PlateSettings& p) {
    plateSettings_ = p;
    rebuildPlates();
    update();
}

void Viewport::updateBounds() {
    if (!model_) return;
    const auto& a = model_->boundsMin;
    const auto& b = model_->boundsMax;
    QVector3D mn(a[0], a[1], a[2]), mx(b[0], b[1], b[2]);
    for (const PlateOutline& pl : plates_) {
        for (const QVector2D& p : pl.pts) {
            mn = QVector3D(std::min(mn.x(), p.x()), std::min(mn.y(), p.y()), std::min(mn.z(), pl.z));
            mx = QVector3D(std::max(mx.x(), p.x()), std::max(mx.y(), p.y()), std::max(mx.z(), pl.z));
        }
    }
    center_ = (mn + mx) * 0.5f;
    radius_ = std::max((mx - mn).length() * 0.5f, 1e-6f);
}

void Viewport::rebuildPlates() {
    plates_.clear();
    if (model_) {
        if (plateSettings_.mode == PlateSettings::Mode::Custom) {
            // One plate of the chosen size, centred under the model's footprint, at its lowest point.
            const float cx = (model_->boundsMin[0] + model_->boundsMax[0]) * 0.5f;
            const float cy = (model_->boundsMin[1] + model_->boundsMax[1]) * 0.5f;
            const float hw = plateSettings_.width * 0.5f, hd = plateSettings_.depth * 0.5f;
            PlateOutline p;
            p.z = model_->boundsMin[2];
            p.pts = {QVector2D(cx - hw, cy - hd), QVector2D(cx + hw, cy - hd), QVector2D(cx + hw, cy + hd),
                     QVector2D(cx - hw, cy + hd)};
            plates_.push_back(std::move(p));
        } else if (plateSettings_.mode == PlateSettings::Mode::Auto) {
            int fileIndex = 0;
            for (const PlateShape& shape : model_->plates) {  // the printer bed(s) stored in the file
                PlateOutline p;
                p.fileIndex = fileIndex++;
                for (const auto& v : shape.outline) p.pts.emplace_back(v[0], v[1]);
                plates_.push_back(std::move(p));
            }
        }
    }
    platesDirty_ = true;
    updateBounds();
}

void Viewport::releasePlateGpu() {
    if (plateFillVao_.isCreated()) plateFillVao_.destroy();
    if (plateFillVbo_.isCreated()) plateFillVbo_.destroy();
    if (plateLineVao_.isCreated()) plateLineVao_.destroy();
    if (plateLineVbo_.isCreated()) plateLineVbo_.destroy();
    plateFillCount_ = plateGridCount_ = plateOutlineCount_ = 0;
}

// Fill (triangle fan, the beds are convex), a 10 mm grid clipped to the bed, and the outline.
void Viewport::uploadPlates() {
    platesDirty_ = false;
    releasePlateGpu();
    std::vector<float> fill, grid, outline;
    auto push = [](std::vector<float>& v, float x, float y, float z) {
        v.push_back(x);
        v.push_back(y);
        v.push_back(z);
    };
    for (const PlateOutline& pl : plates_) {
        const auto& p = pl.pts;
        if (p.size() < 3) continue;
        QVector2D centre(0.f, 0.f);
        float minX = p[0].x(), maxX = minX, minY = p[0].y(), maxY = minY;
        for (const QVector2D& v : p) {
            centre += v;
            minX = std::min(minX, v.x());
            maxX = std::max(maxX, v.x());
            minY = std::min(minY, v.y());
            maxY = std::max(maxY, v.y());
        }
        centre /= float(p.size());
        for (size_t i = 0; i < p.size(); ++i) {
            const QVector2D& a = p[i];
            const QVector2D& b = p[(i + 1) % p.size()];
            push(fill, centre.x(), centre.y(), pl.z);
            push(fill, a.x(), a.y(), pl.z);
            push(fill, b.x(), b.y(), pl.z);
            push(outline, a.x(), a.y(), pl.z);
            push(outline, b.x(), b.y(), pl.z);
        }
        float step = 10.f;
        while (std::max(maxX - minX, maxY - minY) / step > 60.f) step *= 2.f;  // keep huge plates light
        for (float x = minX + step; x < maxX - 0.01f; x += step) {
            float lo = 1e30f, hi = -1e30f;
            for (size_t i = 0; i < p.size(); ++i) {
                const QVector2D& a = p[i];
                const QVector2D& b = p[(i + 1) % p.size()];
                if ((a.x() - x) * (b.x() - x) > 0.f || a.x() == b.x()) continue;
                const float y = a.y() + (x - a.x()) / (b.x() - a.x()) * (b.y() - a.y());
                lo = std::min(lo, y);
                hi = std::max(hi, y);
            }
            if (hi > lo) { push(grid, x, lo, pl.z); push(grid, x, hi, pl.z); }
        }
        for (float y = minY + step; y < maxY - 0.01f; y += step) {
            float lo = 1e30f, hi = -1e30f;
            for (size_t i = 0; i < p.size(); ++i) {
                const QVector2D& a = p[i];
                const QVector2D& b = p[(i + 1) % p.size()];
                if ((a.y() - y) * (b.y() - y) > 0.f || a.y() == b.y()) continue;
                const float x = a.x() + (y - a.y()) / (b.y() - a.y()) * (b.x() - a.x());
                lo = std::min(lo, x);
                hi = std::max(hi, x);
            }
            if (hi > lo) { push(grid, lo, y, pl.z); push(grid, hi, y, pl.z); }
        }
    }
    plateFillCount_ = int(fill.size() / 3);
    plateGridCount_ = int(grid.size() / 3);
    plateOutlineCount_ = int(outline.size() / 3);
    if (plateFillCount_ == 0) return;

    plateFillVao_.create();
    plateFillVao_.bind();
    plateFillVbo_.create();
    plateFillVbo_.bind();
    plateFillVbo_.allocate(fill.data(), int(fill.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    plateFillVao_.release();

    std::vector<float> lines = grid;  // grid first, then the outline
    lines.insert(lines.end(), outline.begin(), outline.end());
    plateLineVao_.create();
    plateLineVao_.bind();
    plateLineVbo_.create();
    plateLineVbo_.bind();
    plateLineVbo_.allocate(lines.data(), int(lines.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    plateLineVao_.release();
}

// Translucent bed: it never writes depth, so it cannot hide the model, and it is not part of picking.
void Viewport::drawPlates(const QMatrix4x4& mvp) {
    if (plateFillCount_ == 0) return;
    const QVector3D fillColor = dark_ ? QVector3D(0.55f, 0.62f, 0.75f) : QVector3D(0.30f, 0.38f, 0.50f);
    const QVector3D gridColor = dark_ ? QVector3D(0.75f, 0.80f, 0.90f) : QVector3D(0.20f, 0.25f, 0.35f);
    const QVector3D outlineColor = dark_ ? QVector3D(0.80f, 0.85f, 0.95f) : QVector3D(0.25f, 0.32f, 0.45f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    lineProg_.bind();
    lineProg_.setUniformValue("uMvp", mvp);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.f, 2.f);
    lineProg_.setUniformValue("uColor", fillColor);
    lineProg_.setUniformValue("uAlpha", dark_ ? 0.22f : 0.18f);
    plateFillVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, plateFillCount_);
    plateFillVao_.release();
    glDisable(GL_POLYGON_OFFSET_FILL);

    plateLineVao_.bind();
    lineProg_.setUniformValue("uColor", gridColor);
    lineProg_.setUniformValue("uAlpha", dark_ ? 0.18f : 0.15f);
    if (plateGridCount_ > 0) glDrawArrays(GL_LINES, 0, plateGridCount_);
    lineProg_.setUniformValue("uColor", outlineColor);
    lineProg_.setUniformValue("uAlpha", 0.85f);
    glDrawArrays(GL_LINES, plateGridCount_, plateOutlineCount_);
    plateLineVao_.release();

    lineProg_.setUniformValue("uAlpha", 1.0f);  // the edge and picking passes expect an opaque line shader
    lineProg_.release();
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}

void Viewport::uploadModel() {
    pendingUpload_ = false;
    releaseGpu();
    if (!model_) return;

    for (const MeshBatch& mb : model_->batches) {
        auto g = std::make_unique<GpuBatch>();
        g->color = QVector3D(mb.color[0], mb.color[1], mb.color[2]);
        g->indexCount = int(mb.indices.size());
        g->vao.create();
        g->vao.bind();
        g->vbo.create();
        g->vbo.bind();
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mb.vertices.size() * sizeof(float)), mb.vertices.data(), GL_STATIC_DRAW);
        g->ibo.create();
        g->ibo.bind();
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mb.indices.size() * sizeof(uint32_t)), mb.indices.data(),
                     GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        g->vao.release();
        gpu_.push_back(std::move(g));
    }

    if (!model_->edges.empty()) {
        edgeVao_.create();
        edgeVao_.bind();
        edgeVbo_.create();
        edgeVbo_.bind();
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(model_->edges.size() * sizeof(float)), model_->edges.data(),
                     GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        edgeVao_.release();
        edgeVertexCount_ = int(model_->edges.size() / 3);
    }
}

QVector3D Viewport::cameraAxis(int i) const {
    static const QVector3D axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    return orientation_.rotatedVector(axes[i]);
}

QVector3D Viewport::eyePosition() const { return target_ + cameraAxis(2) * distance_; }

QMatrix4x4 Viewport::viewMatrix() const {
    QMatrix4x4 v;
    v.lookAt(eyePosition(), target_, cameraAxis(1));
    return v;
}

void Viewport::clipPlanes(float& nearP, float& farP) const {
    if (!perspective_) {
        // Orthographic: depth of the model centre along the view axis; the near plane may sit behind the eye.
        const float depth = QVector3D::dotProduct(center_ - eyePosition(), -cameraAxis(2));
        nearP = depth - radius_ * 1.5f;
        farP = depth + radius_ * 1.5f;
        return;
    }
    const float eyeToCenter = (eyePosition() - center_).length();
    farP = eyeToCenter + radius_ * 1.5f;
    nearP = std::max(farP * 1e-4f, eyeToCenter - radius_ * 1.5f);
}

QMatrix4x4 Viewport::projMatrix() const {
    float n, f;
    clipPlanes(n, f);
    QMatrix4x4 p;
    const float aspect = float(width()) / float(std::max(height(), 1));
    if (perspective_) {
        p.perspective(kFov, aspect, n, f);
    } else {
        // Same visible height at the target plane as the perspective view, so toggling keeps the framing.
        const float halfH = distance_ * std::tan(qDegreesToRadians(kFov * 0.5f));
        p.ortho(-halfH * aspect, halfH * aspect, -halfH, halfH, n, f);
    }
    return p;
}

void Viewport::setPerspectiveEnabled(bool on) {
    if (perspective_ == on) return;
    perspective_ = on;
    wheelValid_ = false;
    update();
}

void Viewport::fitToView() {
    wheelValid_ = false;
    target_ = center_;
    distance_ = radius_ / std::sin(qDegreesToRadians(kFov * 0.5f)) * 1.05f;
    emit orientationChanged(orientation_);
    update();
}

void Viewport::goHome() {
    orientation_ = isoOrientation();
    fitToView();
}

void Viewport::setViewDirection(const QVector3D& towardsCamera) {
    const QVector3D back = towardsCamera.normalized();
    // Looking straight along Z, Y is the up hint
    const QVector3D hint = std::abs(back.z()) > 0.999f ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
    const QVector3D right = QVector3D::crossProduct(hint, back).normalized();
    const QVector3D up = QVector3D::crossProduct(back, right);
    orientation_ = QQuaternion::fromAxes(right, up, back);
    fitToView();
}

void Viewport::drawMeshes(const QMatrix4x4& mvp, const QMatrix4x4& view) {
    meshProg_.bind();
    meshProg_.setUniformValue("uMvp", mvp);
    meshProg_.setUniformValue("uNormalMat", view.normalMatrix());
    for (auto& g : gpu_) {
        meshProg_.setUniformValue("uColor", g->color);
        g->vao.bind();
        glDrawElements(GL_TRIANGLES, g->indexCount, GL_UNSIGNED_INT, nullptr);
        g->vao.release();
    }
    meshProg_.release();
}

void Viewport::paintGL() {
    if (pendingUpload_) uploadModel();
    if (platesDirty_) uploadPlates();

    const QVector3D top = dark_ ? QVector3D(0.17f, 0.19f, 0.23f) : QVector3D(0.96f, 0.97f, 0.99f);
    const QVector3D bottom = dark_ ? QVector3D(0.07f, 0.08f, 0.10f) : QVector3D(0.80f, 0.83f, 0.88f);
    glClearColor(bottom.x(), bottom.y(), bottom.z(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    bgProg_.bind();
    bgProg_.setUniformValue("uTop", top);
    bgProg_.setUniformValue("uBottom", bottom);
    bgVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    bgVao_.release();
    bgProg_.release();
    glEnable(GL_DEPTH_TEST);

    // Line-only models (a 2D DXF drawing) have no triangles but must still be drawn.
    const bool linesOnly = gpu_.empty();
    if (linesOnly && edgeVertexCount_ == 0) return;

    const QMatrix4x4 view = viewMatrix();
    const QMatrix4x4 mvp = projMatrix() * view;

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.f, 1.f);
    drawMeshes(mvp, view);
    if (selected_ >= 0 && model_ && size_t(selected_) < model_->parts.size()) {
        glDepthFunc(GL_LEQUAL);
        meshProg_.bind();
        meshProg_.setUniformValue("uMvp", mvp);
        meshProg_.setUniformValue("uNormalMat", view.normalMatrix());
        const QVector3D hl(1.f, 0.55f, 0.1f);
        for (const PartRange& r : model_->parts[size_t(selected_)].ranges) {
            GpuBatch& g = *gpu_[size_t(r.batch)];
            meshProg_.setUniformValue("uColor", g.color * 0.3f + hl * 0.7f);
            g.vao.bind();
            glDrawElements(GL_TRIANGLES, int(r.count), GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(uintptr_t(r.start) * sizeof(uint32_t)));
            g.vao.release();
        }
        meshProg_.release();
        glDepthFunc(GL_LESS);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    if ((showEdges_ || linesOnly) && edgeVertexCount_ > 0) {
        lineProg_.bind();
        lineProg_.setUniformValue("uMvp", mvp);
        // Edges are dark outlines on the surfaces; line-only drawings have no surfaces, so they take the contrasting colour.
        const QVector3D edgeColor = linesOnly ? (dark_ ? QVector3D(0.86f, 0.89f, 0.95f) : QVector3D(0.10f, 0.12f, 0.16f))
                                              : (dark_ ? QVector3D(0.02f, 0.02f, 0.03f) : QVector3D(0.08f, 0.08f, 0.10f));
        lineProg_.setUniformValue("uColor", edgeColor);
        edgeVao_.bind();
        glDrawArrays(GL_LINES, 0, edgeVertexCount_);
        edgeVao_.release();
        lineProg_.release();
    }

    drawPlates(mvp);
}

QVector3D Viewport::unproject(const QPoint& pos, float ndcZ) const {
    const float x = 2.f * (float(pos.x()) + 0.5f) / float(width()) - 1.f;
    const float y = 1.f - 2.f * (float(pos.y()) + 0.5f) / float(height());
    bool ok = false;
    const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted(&ok);
    return ok ? inv.map(QVector3D(x, y, ndcZ)) : target_;
}

bool Viewport::pickPoint(const QPoint& pos, QVector3D& world, int* part) {
    if (gpu_.empty()) return false;
    if (part) *part = -1;
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    const QSize px(int(width() * dpr), int(height() * dpr));
    if (!pickFbo_ || pickFbo_->size() != px) {
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        pickFbo_ = std::make_unique<QOpenGLFramebufferObject>(px, f);
    }
    const int x = std::clamp(int(pos.x() * dpr), 0, px.width() - 1);
    const int y = std::clamp(px.height() - 1 - int(pos.y() * dpr), 0, px.height() - 1);

    pickFbo_->bind();
    glViewport(0, 0, px.width(), px.height());
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, 1, 1);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    const QMatrix4x4 view = viewMatrix();
    const QMatrix4x4 mvp = projMatrix() * view;
    int id = 0;
    if (part && model_) {
        // Flat-colour pass where each part's colour encodes its index
        lineProg_.bind();
        lineProg_.setUniformValue("uMvp", mvp);
        for (size_t i = 0; i < model_->parts.size(); ++i) {
            const uint32_t k = uint32_t(i) + 1;
            lineProg_.setUniformValue("uColor", QVector3D(float(k & 255) / 255.f, float((k >> 8) & 255) / 255.f,
                                                          float((k >> 16) & 255) / 255.f));
            for (const PartRange& r : model_->parts[i].ranges) {
                GpuBatch& g = *gpu_[size_t(r.batch)];
                g.vao.bind();
                glDrawElements(GL_TRIANGLES, int(r.count), GL_UNSIGNED_INT,
                               reinterpret_cast<void*>(uintptr_t(r.start) * sizeof(uint32_t)));
                g.vao.release();
            }
        }
        lineProg_.release();
        unsigned char rgba[4] = {0, 0, 0, 0};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        id = rgba[0] | (rgba[1] << 8) | (rgba[2] << 16);
    } else {
        drawMeshes(mvp, view);
    }
    float depth = 1.f;
    glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glDisable(GL_SCISSOR_TEST);
    pickFbo_->release();
    doneCurrent();

    if (depth >= 1.f) return false;
    if (part && id > 0) *part = id - 1;
    world = unproject(pos, depth * 2.f - 1.f);
    return true;
}

// The file plate under the cursor: intersect the cursor ray with each plate's plane and test the point against its outline.
int Viewport::plateAt(const QPoint& pos) const {
    const QVector3D a = unproject(pos, -1.f), b = unproject(pos, 1.f);
    for (const PlateOutline& pl : plates_) {
        if (pl.fileIndex < 0 || pl.pts.size() < 3 || std::abs(b.z() - a.z()) < 1e-9f) continue;
        const float t = (pl.z - a.z()) / (b.z() - a.z());
        const float x = a.x() + t * (b.x() - a.x()), y = a.y() + t * (b.y() - a.y());
        bool inside = false;
        for (size_t i = 0, j = pl.pts.size() - 1; i < pl.pts.size(); j = i++) {
            const QVector2D& pi = pl.pts[i];
            const QVector2D& pj = pl.pts[j];
            if (((pi.y() > y) != (pj.y() > y)) && (x < (pj.x() - pi.x()) * (y - pi.y()) / (pj.y() - pi.y()) + pi.x()))
                inside = !inside;
        }
        if (inside) return pl.fileIndex;
    }
    return -1;
}

void Viewport::selectAt(const QPoint& pos) {
    QVector3D w;
    int part = -1;
    pickPoint(pos, w, &part);
    selectPart(part);
    if (part >= 0) {
        emit partClicked(part, pos);
    } else {  // nothing solid under the cursor: maybe a print plate
        const int plate = plateAt(pos);
        if (plate >= 0) emit plateClicked(plate, pos);
    }
}

void Viewport::selectPart(int part) {
    if (!model_ || part >= int(model_->parts.size())) part = -1;
    selected_ = part;
    emit partSelected(part);
    update();
}

bool Viewport::matches(const Binding& b, QMouseEvent* e) const {
    return e->button() == b.button && (e->modifiers() & kModMask) == Qt::KeyboardModifiers(b.modifier);
}

void Viewport::rotateAbout(const QVector3D& p, const QQuaternion& r) {
    orientation_ = (r * orientation_).normalized();
    target_ = p + r.rotatedVector(target_ - p);
    emit orientationChanged(orientation_);
}

void Viewport::scaleAbout(const QVector3D& p, float factor) {
    const float newDist = std::clamp(distance_ * factor, radius_ * 1e-3f, radius_ * 1e3f);
    const float f = newDist / distance_;
    target_ = p + (target_ - p) * f;
    distance_ = newDist;
    update();
}

void Viewport::mousePressEvent(QMouseEvent* e) {
    emit pressed();
    wheelValid_ = false;
    if (e->button() == Qt::LeftButton && (e->modifiers() & kModMask) == 0) {
        pressPos_ = e->pos();
        leftPressed_ = true;
    }
    if (drag_ != Drag::None) return;
    if (matches(controls_.pan, e)) drag_ = Drag::Pan;
    else if (matches(controls_.orbit, e)) drag_ = Drag::Orbit;
    else if (matches(controls_.zoom, e)) drag_ = Drag::Zoom;
    else return;
    dragButton_ = e->button();
    last_ = e->pos();

    QVector3D hit;
    const bool found = drag_ != Drag::Zoom && pickPoint(e->pos(), hit);
    if (drag_ == Drag::Orbit) {
        pivot_ = found ? hit : center_;
    } else if (drag_ == Drag::Pan) {
        panDepth_ = found ? std::max(QVector3D::dotProduct(hit - eyePosition(), -cameraAxis(2)), distance_ * 1e-3f)
                          : distance_;
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    const QPoint d = e->pos() - last_;
    last_ = e->pos();
    switch (drag_) {
    case Drag::Orbit: {
        const float k = 0.4f;
        const QQuaternion r = QQuaternion::fromAxisAndAngle(cameraAxis(1), -d.x() * k) *
                              QQuaternion::fromAxisAndAngle(cameraAxis(0), -d.y() * k);
        rotateAbout(pivot_, r);
        break;
    }
    case Drag::Pan: {
        const float wpp = 2.f * (perspective_ ? panDepth_ : distance_) * std::tan(qDegreesToRadians(kFov * 0.5f)) /
                          float(std::max(height(), 1));
        target_ += (-cameraAxis(0) * float(d.x()) + cameraAxis(1) * float(d.y())) * wpp;
        break;
    }
    case Drag::Zoom:
        scaleAbout(target_, std::exp(d.y() * 0.005f));
        return;
    default:
        break;
    }
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && leftPressed_) {
        leftPressed_ = false;
        if ((e->pos() - pressPos_).manhattanLength() < 4) selectAt(e->pos());
    }
    if (e->button() == dragButton_) {
        drag_ = Drag::None;
        dragButton_ = Qt::NoButton;
    }
}

void Viewport::wheelEvent(QWheelEvent* e) {
    float steps = float(e->angleDelta().y()) / 120.f;
    if (controls_.invertWheel) steps = -steps;
    const QPoint pos = e->position().toPoint();

    QVector3D p;
    if (wheelValid_ && pos == wheelPos_ && wheelTimer_.elapsed() < 1500) {
        p = wheelPoint_;  // same cursor position as the last wheel step: the point stayed under the cursor
    } else {
        if (!pickPoint(pos, p)) {
            // No surface under the cursor: use the cursor ray at the depth of the view target
            const QVector3D a = unproject(pos, -1.f), b = unproject(pos, 1.f);
            const QVector3D n = cameraAxis(2);
            const float denom = QVector3D::dotProduct(b - a, n);
            const float t = std::abs(denom) > 1e-9f ? QVector3D::dotProduct(target_ - a, n) / denom : 0.f;
            p = a + (b - a) * t;
        }
        wheelPos_ = pos;
        wheelPoint_ = p;
        wheelValid_ = true;
    }
    wheelTimer_.restart();
    scaleAbout(p, std::pow(0.88f, steps));
    e->accept();
}
