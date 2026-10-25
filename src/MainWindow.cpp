#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <algorithm>
#include <stdexcept>

#include "AboutDialog.h"
#include "SettingsDialog.h"
#include "Icons.h"
#include "ViewCube.h"
#include "Viewport.h"

namespace {

// Rounded, slightly translucent floating panel that follows the palette (light/dark).
class Panel : public QWidget {
public:
    explicit Panel(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor bg = palette().color(QPalette::Window);
        bg.setAlpha(230);
        QColor border = palette().color(QPalette::Mid);
        border.setAlpha(110);
        p.setBrush(bg);
        p.setPen(border);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }
};

QString iconFontFamily() {
    const QStringList families = QFontDatabase::families();
    if (families.contains("Segoe Fluent Icons")) return "Segoe Fluent Icons";
    return "Segoe MDL2 Assets";
}

#ifdef CADSCOUT_LITE
// Invisible strip along a window edge/corner that starts a native resize.
class ResizeGrip : public QWidget {
public:
    ResizeGrip(QWidget* parent, Qt::Edges edges, Qt::CursorShape cursor) : QWidget(parent), edges_(edges) {
        setCursor(cursor);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && window()->windowHandle()) window()->windowHandle()->startSystemResize(edges_);
    }

private:
    Qt::Edges edges_;
};
#endif

}  // namespace

MainWindow::MainWindow() {
#ifdef CADSCOUT_LITE
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowTitle("CADscout Lite");
#else
    setWindowTitle(QString());  // empty: Qt shows just the application display name, "CADscout Full"
#endif
    resize(1100, 750);
    setAcceptDrops(true);

    auto splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setMinimumWidth(180);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setIndentation(16);
    tree_->setUniformRowHeights(true);
    tree_->setStyleSheet(
        "QTreeWidget{background:palette(window);color:palette(window-text);font-size:13px;outline:0;padding:6px;}"
        "QTreeWidget::item{height:26px;border-radius:6px;padding-left:2px;}"
        "QTreeWidget::item:hover{background:palette(midlight);}"
        "QTreeWidget::item:selected{background:palette(highlight);color:palette(highlighted-text);}");
    viewport_ = new Viewport;
    splitter->addWidget(tree_);
    splitter->addWidget(viewport_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({270, 830});
    auto* central = new QWidget;
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    toolbar_ = new QWidget;
    toolbar_->setAttribute(Qt::WA_StyledBackground, true);
    toolbar_->setObjectName("topBar");
    toolbar_->setStyleSheet("#topBar{background:palette(window);border-bottom:1px solid palette(mid);}");
    vbox->addWidget(toolbar_);
    vbox->addWidget(splitter, 1);
    setCentralWidget(central);
    viewport_->installEventFilter(this);

    const QString iconFont = iconFontFamily();
    const QString btnStyle = QString(
        "QToolButton{border:none;border-radius:8px;padding:0 12px;min-height:38px;"
        "font-size:13px;color:palette(window-text);background:transparent;}"
        "QToolButton:hover{background:palette(midlight);}"
        "QToolButton:pressed{background:palette(mid);}"
        "QToolButton:checked{background:palette(highlight);color:palette(highlighted-text);}"
        "QToolButton:disabled{color:palette(mid);}");

    auto* row = new QHBoxLayout(toolbar_);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(2);

    auto makeButton = [&](const QString& label, const QString& icon, const QString& tip, bool checkable = false) {
        auto* b = new QToolButton(toolbar_);
        b->setText(label);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(20, 20));
        if (!icon.isEmpty()) setBtnIcon(b, icon);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(btnStyle);
        
        row->addWidget(b);
        return b;
    };

    openBtn_ = makeButton("Open", "folder-open", "Open a 3D model (Ctrl+O)");
    treeBtn_ = new QToolButton(viewport_);
    treeBtn_->setCheckable(true);
    treeBtn_->setChecked(true);
    treeBtn_->setIconSize(QSize(18, 18));
    treeBtn_->setToolTip("Show/hide assembly tree (T)");
    treeBtn_->setCursor(Qt::PointingHandCursor);
    treeBtn_->setFocusPolicy(Qt::NoFocus);
    treeBtn_->setFixedSize(26, 56);
    treeBtn_->setStyleSheet(
        "QToolButton{border:1px solid palette(mid);border-left:none;border-top-right-radius:8px;"
        "border-bottom-right-radius:8px;background:palette(window);}"
        "QToolButton:hover{background:palette(midlight);}");
    auto* fitBtn = makeButton("Fit", "arrows-maximize", "Fit to view (F)");
    auto* edgesBtn = makeButton("Edges", "cube", "Show edges (E)", true);
    edgesBtn->setChecked(true);
    auto* settingsBtn = makeButton("Settings", "settings", "Settings");
    auto* aboutBtn = makeButton("About", "info-circle", "About CADscout (F1)");
#ifdef CADSCOUT_LITE
    auto* fullBtn = makeButton("Open in Full", "external-link", "Open this model in the Full edition (coming soon)");
#endif

    connect(openBtn_, &QToolButton::clicked, this, &MainWindow::openDialog);
#ifdef CADSCOUT_LITE
    connect(fullBtn, &QToolButton::clicked, this, &MainWindow::openInFull);
#endif
    connect(treeBtn_, &QToolButton::toggled, this, [this](bool open) {
        tree_->setVisible(open);
        refreshIcons();
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (!it) return;
        const int part = it->data(0, Qt::UserRole).toInt();
        if (part >= 0) viewport_->selectPart(part);
    });
    connect(fitBtn, &QToolButton::clicked, viewport_, &Viewport::fitToView);
    connect(edgesBtn, &QToolButton::toggled, viewport_, &Viewport::setEdgesVisible);
    connect(settingsBtn, &QToolButton::clicked, this, &MainWindow::openSettings);
    connect(aboutBtn, &QToolButton::clicked, this, &MainWindow::openAbout);

    // Keyboard shortcuts
    auto addShortcut = [this](const QKeySequence& k, auto slot) {
        auto* a = new QAction(this);
        a->setShortcut(k);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
    };
    addShortcut(QKeySequence::Open, [this] { openDialog(); });
    addShortcut(Qt::Key_F1, [this] { openAbout(); });
    addShortcut(Qt::Key_Escape, [this] {
        if (popup_) popup_->hide();
    });
    addShortcut(Qt::Key_F, [this] { viewport_->fitToView(); });
#ifndef CADSCOUT_LITE
    addShortcut(Qt::Key_E, [edgesBtn] { edgesBtn->toggle(); });
    addShortcut(Qt::Key_H, [this] { viewport_->goHome(); });
    addShortcut(Qt::Key_T, [this] {
        if (treeBtn_->isEnabled()) treeBtn_->toggle();
    });
#endif

    cube_ = new ViewCube(viewport_);
    connect(viewport_, &Viewport::orientationChanged, cube_, &ViewCube::setOrientation);
    connect(cube_, &ViewCube::viewRequested, viewport_, &Viewport::setViewDirection);
    viewport_->goHome();

    homePanel_ = new Panel(viewport_);
    auto* homeRow = new QHBoxLayout(homePanel_);
    homeRow->setContentsMargins(6, 6, 6, 6);
    auto* homeBtn = new QToolButton(homePanel_);
#ifndef CADSCOUT_LITE
    homeBtn->setText("Home");
    homeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
#endif
    homeBtn->setIconSize(QSize(20, 20));
    setBtnIcon(homeBtn, "home");
#ifdef CADSCOUT_LITE
    homeBtn->setToolTip("Isometric view, fit to screen");
#else
    homeBtn->setToolTip("Isometric view, fit to screen (H)");
#endif
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setFocusPolicy(Qt::NoFocus);
    homeBtn->setStyleSheet(btnStyle);
    homeRow->addWidget(homeBtn);
    connect(homeBtn, &QToolButton::clicked, viewport_, &Viewport::goHome);
#ifdef CADSCOUT_LITE
    tree_->hide();
    treeBtn_->hide();
    fitBtn->hide();
    edgesBtn->hide();
    cube_->hide();

    // Perspective toggle (checked = perspective), placed before Home in the corner panel
    perspBtn_ = new QToolButton(homePanel_);
    perspBtn_->setCheckable(true);
    perspBtn_->setChecked(false);  // must match Viewport's default (perspective off)
    perspBtn_->setIconSize(QSize(20, 20));
    perspBtn_->setToolTip("Perspective on/off");
    perspBtn_->setCursor(Qt::PointingHandCursor);
    perspBtn_->setFocusPolicy(Qt::NoFocus);
    perspBtn_->setStyleSheet(btnStyle);
    homeRow->setSpacing(4);
    homeRow->insertWidget(0, perspBtn_);
    connect(perspBtn_, &QToolButton::toggled, this, [this](bool on) {
        viewport_->setPerspectiveEnabled(on);
        refreshIcons();
    });
#endif

    // Normal status bar along the bottom of the window (under the tree and the 3D view).
    status_ = new QLabel(central);
    status_->setStyleSheet("QLabel{background:palette(window);color:palette(window-text);"
                           "border-top:1px solid palette(mid);padding:0 12px;font-size:12px;}");
    status_->setFixedHeight(26);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // long text is shortened, not wider than the window
    vbox->addWidget(status_);
    setStatus("Ready");
    connect(viewport_, &Viewport::partSelected, this, [this](int part) {
        if (!model_) return;
        const QString name = part >= 0 ? QString::fromStdString(model_->parts[size_t(part)].name) : QString();
        setStatus(name.isEmpty() ? info_ : name + "  ·  " + info_);

        QSignalBlocker block(tree_);
        QTreeWidgetItem* item = part >= 0 && size_t(part) < partItems_.size() ? partItems_[size_t(part)] : nullptr;
        if (item) {
            tree_->setCurrentItem(item);
            tree_->scrollToItem(item);
        } else {
            tree_->clearSelection();
            tree_->setCurrentItem(nullptr);
        }
    });

    // Click popups for Bambu/Orca files: a plate shows its name and print time, a part its object and filament colour.
    popup_ = new QLabel(viewport_);
    popup_->setTextFormat(Qt::RichText);
    popup_->setAttribute(Qt::WA_TransparentForMouseEvents);  // clicks go through to the view
    popup_->setStyleSheet("QLabel{background:palette(window);color:palette(window-text);border:1px solid palette(mid);"
                          "border-radius:10px;padding:10px 14px;font-size:13px;}");
    popup_->hide();
    auto showPopup = [this](const QString& html, const QPoint& pos) {
        popup_->setText(html);
        popup_->adjustSize();
        const QPoint p(std::clamp(pos.x() + 14, 8, std::max(8, viewport_->width() - popup_->width() - 8)),
                       std::clamp(pos.y() + 14, 8, std::max(8, viewport_->height() - popup_->height() - 8)));
        popup_->move(p);
        popup_->show();
        popup_->raise();
    };
    connect(viewport_, &Viewport::pressed, this, [this] { popup_->hide(); });
    connect(viewport_, &Viewport::partClicked, this, [this, showPopup](int part, const QPoint& pos) {
        if (!model_ || part < 0 || size_t(part) >= model_->parts.size()) return;
        const Part& p = model_->parts[size_t(part)];
        if (p.object.empty() && p.filaments.empty()) return;  // not a Bambu/Orca part
        QString html = "<b>" + QString::fromStdString(p.object.empty() ? p.name : p.object).toHtmlEscaped() + "</b>";
        for (const FilamentUse& f : p.filaments) {
            html += "<br>";
            QString line = QString("Filament %1").arg(f.number);
            if (!f.type.empty()) line += "  ·  " + QString::fromStdString(f.type);
            if (f.hasColor) {
                const QColor c = QColor::fromRgbF(f.color[0], f.color[1], f.color[2]);
                html += QString("<span style=\"color:%1\">&#9632;</span> ").arg(c.name());
                line += "  ·  " + c.name().toUpper();
            } else {
                line += "  ·  colour not set";
            }
            html += line.toHtmlEscaped();
        }
        showPopup(html, pos);
    });
    connect(viewport_, &Viewport::plateClicked, this, [this, showPopup](int plate, const QPoint& pos) {
        if (!model_ || plate < 0 || size_t(plate) >= model_->plates.size()) return;
        const PlateShape& pl = model_->plates[size_t(plate)];
        QString time = "not available (file is not sliced)";
        if (pl.printSeconds >= 0) {
            const int total = int(pl.printSeconds + 0.5), h = total / 3600, m = (total % 3600) / 60;
            time = h > 0 ? QString("%1 h %2 m").arg(h).arg(m, 2, 10, QLatin1Char('0')) : QString("%1 m").arg(m);
        }
        showPopup("<b>" + QString::fromStdString(pl.name.empty() ? "Plate" : pl.name).toHtmlEscaped() +
                      "</b><br>Print time: " + time.toHtmlEscaped(),
                  pos);
    });

    hint_ = new QLabel("Drop a 3D model here\nor press Ctrl+O", viewport_);
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hint_->setStyleSheet("QLabel{color:palette(placeholder-text);font-size:18px;background:transparent;}");

    // Follow the Windows light/dark setting
    auto applyScheme = [this](Qt::ColorScheme s) {
        dark_ = s == Qt::ColorScheme::Dark;
        viewport_->setDarkTheme(dark_);
        refreshIcons();
    };
    applyScheme(QGuiApplication::styleHints()->colorScheme());
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, applyScheme);

    row->addStretch(1);
#ifdef CADSCOUT_LITE
    row->setContentsMargins(8, 0, 0, 0);
    // Icon-only buttons: Open on the left; Open in Full and Settings go right, before the window controls.
    row->removeWidget(settingsBtn);
    row->removeWidget(fullBtn);
    row->removeWidget(aboutBtn);
    row->addWidget(fullBtn);
    row->addWidget(aboutBtn);
    row->addWidget(settingsBtn);
    for (QToolButton* b : {openBtn_, fullBtn, aboutBtn, settingsBtn}) {
        b->setText(QString());
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setFixedSize(46, 46);
        row->setAlignment(b, Qt::AlignVCenter);
    }
    buildWindowControls(row, btnStyle);
#endif
    layoutOverlays();
}

MainWindow::~MainWindow() {
    // Pending onLoaded() events are discarded when this QObject is destroyed.
    if (loader_) loader_->wait();
}

void MainWindow::setBtnIcon(QToolButton* b, const QString& name) {
    iconButtons_.emplace_back(b, name);
    refreshIcons();
}

void MainWindow::refreshIcons() {
    const QColor fg = dark_ ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x22, 0x22, 0x22);
    for (auto& [b, name] : iconButtons_) b->setIcon(tablerIcon(name, fg, Qt::white, 20, devicePixelRatioF()));
    if (treeBtn_) treeBtn_->setIcon(tablerIcon(treeBtn_->isChecked() ? "arrow-bar-left" : "arrow-bar-right", fg, fg, 18,
                                               devicePixelRatioF()));
#ifdef CADSCOUT_LITE
    if (perspBtn_)
        perspBtn_->setIcon(tablerIcon(perspBtn_->isChecked() ? "perspective" : "perspective-off", fg, fg, 20,
                                      devicePixelRatioF()));
    updateWindowControls();
#endif
}

void MainWindow::setStatus(const QString& text) {
    statusText_ = text;
    status_->setToolTip(text);
    status_->setText(QFontMetrics(status_->font()).elidedText(statusText_, Qt::ElideRight, std::max(50, width() - 40)));
}

void MainWindow::showTree() {
    if (treeBtn_->isEnabled()) treeBtn_->setChecked(true);
}

#ifndef CADSCOUT_LITE
void MainWindow::updateTreeAvailability() {
    const bool single = model_ && model_->singleObject;
    if (single) {
        if (treeBtn_->isEnabled()) treeWasOpen_ = treeBtn_->isChecked();
        treeBtn_->setChecked(false);  // hides the tree
        treeBtn_->setEnabled(false);
        treeBtn_->setCursor(Qt::ArrowCursor);
        treeBtn_->setToolTip(QString("This format (%1) has only one object, so there is no assembly tree.")
                                 .arg(QString::fromStdString(model_->formatName)));
    } else if (!treeBtn_->isEnabled()) {
        treeBtn_->setEnabled(true);
        treeBtn_->setCursor(Qt::PointingHandCursor);
        treeBtn_->setToolTip("Show/hide assembly tree (T)");
        treeBtn_->setChecked(treeWasOpen_);
    }
}
#endif


void MainWindow::buildTree() {
    QSignalBlocker block(tree_);
    tree_->clear();
    partItems_.assign(model_->parts.size(), nullptr);
    std::vector<QTreeWidgetItem*> items(model_->nodes.size(), nullptr);
    for (size_t i = 0; i < model_->nodes.size(); ++i) {
        const Node& n = model_->nodes[i];
        auto* it = n.parent >= 0 ? new QTreeWidgetItem(items[size_t(n.parent)]) : new QTreeWidgetItem(tree_);
        it->setText(0, QString::fromStdString(n.name));
        it->setToolTip(0, QString::fromStdString(n.name));  // long names (plate summaries) are elided in the tree
        it->setData(0, Qt::UserRole, n.part);
        items[i] = it;
        if (n.part >= 0) partItems_[size_t(n.part)] = it;
    }
    if (model_->nodes.size() <= 500) tree_->expandAll();
    else tree_->expandToDepth(0);
}

void MainWindow::layoutOverlays() {
    const int w = viewport_->width(), h = viewport_->height();

    treeBtn_->move(0, (h - treeBtn_->height()) / 2);
    treeBtn_->raise();

    cube_->move(w - cube_->width() - 10, h - cube_->height() - 10);
    cube_->raise();
    homePanel_->adjustSize();
#ifdef CADSCOUT_LITE
    homePanel_->move(w - homePanel_->width() - 10, h - homePanel_->height() - 10);
#else
    homePanel_->move(cube_->x() - homePanel_->width() - 6, h - homePanel_->height() - 14);
#endif
    homePanel_->raise();

    // Shorten the status text with "..." when it is wider than the window (the full text is in the tooltip).
    status_->setText(QFontMetrics(status_->font()).elidedText(statusText_, Qt::ElideRight, std::max(50, width() - 40)));

    hint_->setGeometry(0, 0, w, h);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == viewport_ && e->type() == QEvent::Resize) layoutOverlays();
#ifdef CADSCOUT_LITE
    if (obj == closeBtn_ && (e->type() == QEvent::Enter || e->type() == QEvent::Leave)) {
        closeHover_ = e->type() == QEvent::Enter;
        updateWindowControls();
    } else if (obj == toolbar_) {
        // The empty part of the toolbar acts as the title bar.
        auto* m = static_cast<QMouseEvent*>(e);
        switch (e->type()) {
        case QEvent::MouseButtonPress:
            dragPending_ = m->button() == Qt::LeftButton;
            break;
        case QEvent::MouseMove:
            if (dragPending_ && (m->buttons() & Qt::LeftButton) && windowHandle()) {
                dragPending_ = false;
                windowHandle()->startSystemMove();
            }
            break;
        case QEvent::MouseButtonRelease:
            dragPending_ = false;
            break;
        case QEvent::MouseButtonDblClick:
            dragPending_ = false;
            if (m->button() == Qt::LeftButton) toggleMaximized();
            return true;
        default:
            break;
        }
    }
#endif
    return QMainWindow::eventFilter(obj, e);
}

#ifdef CADSCOUT_LITE
void MainWindow::buildWindowControls(QHBoxLayout* row, const QString& btnStyle) {
    // Same look and size as the Open / Open in Full / Settings buttons.
    auto make = [&](const QString& tip) {
        auto* b = new QToolButton(toolbar_);
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(20, 20));
        b->setFixedSize(46, 46);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(btnStyle);
        row->addWidget(b);
        return b;
    };
    minBtn_ = make("Minimize");
    maxBtn_ = make("Maximize");
    closeBtn_ = make("Close");
    closeBtn_->setStyleSheet(btnStyle + "QToolButton:hover{background:#e81123;}QToolButton:pressed{background:#b50e1c;}");
    minBtn_->setIconSize(QSize(16, 16));
    maxBtn_->setIconSize(QSize(16, 16));
    closeBtn_->installEventFilter(this);
    connect(minBtn_, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maxBtn_, &QToolButton::clicked, this, &MainWindow::toggleMaximized);
    connect(closeBtn_, &QToolButton::clicked, this, &QWidget::close);
    toolbar_->installEventFilter(this);

    // Edges first, then corners so the corners win where they overlap.
    const struct { Qt::Edges edges; Qt::CursorShape cursor; } defs[] = {
        {Qt::LeftEdge, Qt::SizeHorCursor},
        {Qt::RightEdge, Qt::SizeHorCursor},
        {Qt::TopEdge, Qt::SizeVerCursor},
        {Qt::BottomEdge, Qt::SizeVerCursor},
        {Qt::TopEdge | Qt::LeftEdge, Qt::SizeFDiagCursor},
        {Qt::BottomEdge | Qt::RightEdge, Qt::SizeFDiagCursor},
        {Qt::TopEdge | Qt::RightEdge, Qt::SizeBDiagCursor},
        {Qt::BottomEdge | Qt::LeftEdge, Qt::SizeBDiagCursor},
    };
    for (const auto& d : defs) grips_.push_back(new ResizeGrip(this, d.edges, d.cursor));
    updateWindowControls();
    layoutResizeGrips();
}

void MainWindow::updateWindowControls() {
    if (!closeBtn_) return;
    const QColor fg = dark_ ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x22, 0x22, 0x22);
    const qreal dpr = devicePixelRatioF();
    minBtn_->setIcon(tablerIcon("minus", fg, fg, 16, dpr));
    maxBtn_->setIcon(tablerIcon(isMaximized() ? "copy" : "square", fg, fg, 16, dpr));
    maxBtn_->setToolTip(isMaximized() ? "Restore" : "Maximize");
    const QColor closeFg = closeHover_ ? QColor(Qt::white) : fg;
    closeBtn_->setIcon(tablerIcon("x", closeFg, closeFg, 20, dpr));
}

void MainWindow::layoutResizeGrips() {
    if (grips_.size() != 8) return;
    const int t = 5, c = 10, w = width(), h = height();
    const bool active = !isMaximized() && !isFullScreen();
    const QRect rects[8] = {
        {0, c, t, h - 2 * c}, {w - t, c, t, h - 2 * c}, {c, 0, w - 2 * c, t}, {c, h - t, w - 2 * c, t},
        {0, 0, c, c}, {w - c, h - c, c, c}, {w - c, 0, c, c}, {0, h - c, c, c},
    };
    for (size_t i = 0; i < 8; ++i) {
        grips_[i]->setGeometry(rects[i]);
        grips_[i]->setVisible(active);
        if (active) grips_[i]->raise();
    }
}

void MainWindow::toggleMaximized() {
    if (isMaximized()) showNormal();
    else showMaximized();
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    layoutResizeGrips();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        updateWindowControls();
        layoutResizeGrips();
    }
}
#endif

void MainWindow::openDialog() {
    QSettings s;
    const QString dir = s.value("lastDir").toString();
    const QString path = QFileDialog::getOpenFileName(
        this, "Open 3D model", dir,
        "3D models (*.stp *.step *.igs *.iges *.brep *.stl *.obj *.ply *.wrl *.vrml *.gltf *.glb *.3mf *.dxf);;"
        "STEP (*.stp *.step);;IGES (*.igs *.iges);;BREP (*.brep);;STL (*.stl);;OBJ (*.obj);;PLY (*.ply);;"
        "VRML (*.wrl *.vrml);;glTF (*.gltf *.glb);;3MF (*.3mf);;DXF (*.dxf);;All files (*.*)");
    if (path.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    openFile(path);
}

void MainWindow::openAbout() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::openSettings() {
    SettingsDialog dlg(viewport_->controls(), QSettings().value("mesh/quality", 1).toInt(), viewport_->plateSettings(),
                       this);
    if (dlg.exec() == QDialog::Accepted) {
        const Controls c = dlg.controls();
        c.save();
        viewport_->setControls(c);
        QSettings().setValue("mesh/quality", dlg.meshQuality());
        const PlateSettings plate = dlg.plateSettings();
        plate.save();
        viewport_->setPlateSettings(plate);
    }
}

#ifdef CADSCOUT_LITE
void MainWindow::openInFull() {
    const QString fullPath = QDir(QCoreApplication::applicationDirPath()).filePath("CADscoutFull.exe");
    if (!QFileInfo::exists(fullPath)) {
        QMessageBox::information(this, "CADscout Full is coming soon",
                                 "CADscout Full will be available soon. Once it is released, install it to open this "
                                 "model in it.");
        return;
    }

    const QStringList args = currentFile_.isEmpty() ? QStringList{} : QStringList{currentFile_};
    if (!QProcess::startDetached(fullPath, args)) {
        QMessageBox::warning(this, "Unable to open Full edition",
                             "The Full edition was found but could not be started.");
        return;
    }
    close();
}
#endif

void MainWindow::openFile(const QString& path) {
    if (loading_) return;
    loading_ = true;
    if (popup_) popup_->hide();
    currentFile_ = path;
    openBtn_->setEnabled(false);
    hint_->hide();
    setStatus("Loading " + QFileInfo(path).fileName() + " ...");
    layoutOverlays();
    QApplication::setOverrideCursor(Qt::BusyCursor);

    const std::string utf8 = path.toUtf8().toStdString();
    const auto quality = MeshQuality(std::clamp(QSettings().value("mesh/quality", 1).toInt(), 0, 2));
    QThread* t = QThread::create([this, utf8, path, quality] {
        std::shared_ptr<Model> model;
        QString error;
        try {
            model = loadModelFile(utf8, quality);
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
            error = "Unknown error while reading the file.";
        }
        QMetaObject::invokeMethod(this, [this, model, path, error] { onLoaded(model, path, error); },
                                  Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    loader_ = t;
    t->start();
}

void MainWindow::onLoaded(std::shared_ptr<Model> model, const QString& path, const QString& error) {
    QApplication::restoreOverrideCursor();
    loading_ = false;
    openBtn_->setEnabled(true);
    if (!model) {
        setStatus("Failed to load " + QFileInfo(path).fileName());
        hint_->setVisible(!viewport_->hasModel());
        layoutOverlays();
        QMessageBox::warning(this, "CADscout", error);
        return;
    }
    model_ = model;
#ifdef CADSCOUT_LITE
    setWindowTitle(QFileInfo(path).fileName() + " - CADscout Lite");
#else
    buildTree();
    updateTreeAvailability();
    setWindowTitle(QFileInfo(path).fileName());  // Qt adds " - CADscout Full" (the application display name)
#endif
    // Status bar text: CAD files show loading time, faces, triangles; Bambu/Orca projects show loading time,
    // printer, nozzle size and number of filaments (Model::extraInfo).
    const QString loadTime = QString("Loaded in %1 s").arg(model->loadSeconds, 0, 'f', 2);
    if (!model->extraInfo.empty())
        info_ = loadTime + "  ·  " + QString::fromStdString(model->extraInfo);
    else
        info_ = QString("%1  ·  %2 faces  ·  %3 triangles").arg(loadTime).arg(model->faceCount).arg(model->triangleCount);
    viewport_->setModel(model);
    setStatus(info_);

    if (hasClick_) QTimer::singleShot(200, this, [this] { viewport_->selectAt(clickPoint_); });

    if (!screenshotPath_.isEmpty()) {
        QTimer::singleShot(500, this, [this] {
            const QPixmap px = screen()->grabWindow(winId());
            QFile log(screenshotPath_ + ".log");
            if (log.open(QIODevice::WriteOnly)) log.write(QString("%1x%2 saved=%3").arg(px.width()).arg(px.height()).arg(px.save(screenshotPath_, "BMP")).toUtf8());
            QApplication::quit();
        });
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) openFile(urls.first().toLocalFile());
}
