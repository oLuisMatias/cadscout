#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QCursor>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
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
#include <limits>
#include <stdexcept>

#include "AboutDialog.h"
#include "ClipboardImage.h"
#include "ExtraFormats.h"
#include "SettingsDialog.h"
#include "Translate.h"
#include "Formats.h"
#include "Icons.h"
#include "InfoPanel.h"
#include "ModelCache.h"
#include "ModernStyle.h"
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

// Name of the open file in the middle of the top bar. It is shortened in the middle ("long_na...ame.step") when the window is
// narrow and ignores the mouse, so the bar under it still drags and maximizes the window.
class TitleLabel : public QLabel {
public:
    explicit TitleLabel(QWidget* parent) : QLabel(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont f = font();
        f.setPixelSize(13);
        setFont(f);
    }

    // As wide as the text, but it may shrink to nothing (the text is then shortened) so a long name never widens the window.
    QSize sizeHint() const override { return QSize(QLabel::sizeHint().width() + 8, QLabel::sizeHint().height()); }
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        p.setFont(font());
        p.drawText(rect().adjusted(8, 0, 0, 0), Qt::AlignCenter, fontMetrics().elidedText(text(), Qt::ElideMiddle, width() - 8));
    }
};

const QString kCancelled = QStringLiteral("\x01" "cancelled");  // error text of a load stopped with Cancel

// Thin rounded progress bar. A value below 0 shows a moving piece instead (a step without a percentage).
class ProgressStrip : public QWidget {
public:
    explicit ProgressStrip(QWidget* parent) : QWidget(parent) { setFixedHeight(6); }
    void setValue(float v) {
        value_ = v;
        ++phase_;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        QColor track = palette().color(QPalette::WindowText);
        track.setAlpha(40);
        p.setBrush(track);
        p.drawRoundedRect(QRectF(rect()), 3, 3);
        p.setBrush(QColor(0x2f, 0x9b, 0xff));
        if (value_ >= 0.f) {
            p.drawRoundedRect(QRectF(0, 0, width() * std::clamp(value_, 0.f, 1.f), height()), 3, 3);
        } else {
            const double piece = width() * 0.3, x = std::fmod(phase_ * 8.0, width() + piece) - piece;
            p.drawRoundedRect(QRectF(x, 0, piece, height()), 3, 3);
        }
    }

private:
    float value_ = -1.f;
    int phase_ = 0;
};

}  // namespace

MainWindow::MainWindow() {
    setWindowFlag(Qt::FramelessWindowHint);
#ifdef CADSCOUT_LITE
    setWindowTitle("CADscout Lite");
#else
    setWindowTitle(QString());  // empty: Qt shows just the application display name, "CADscout Full"
#endif
    resize(1100, 750);
#ifdef CADSCOUT_LITE
    restoreGeometry(QSettings().value("window/geometry").toByteArray());
    arrowKeys_ = QSettings().value("browse/arrowKeys", true).toBool();
    autoReload_ = QSettings().value("browse/autoReload", true).toBool();
    inches_ = QSettings().value("display/inches", false).toBool();
    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(800);
    connect(&reloadTimer_, &QTimer::timeout, this, &MainWindow::checkReload);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReload(); });
#endif
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

#ifdef CADSCOUT_LITE
    openBtn_ = makeButton(tx("Open"), "folder-open", tx("Open File"));
#else
    openBtn_ = makeButton(tx("Open"), "folder-open", tx("Open a 3D model (Ctrl+O)"));
#endif
    treeBtn_ = new QToolButton(viewport_);
    treeBtn_->setCheckable(true);
    treeBtn_->setChecked(true);
    treeBtn_->setIconSize(QSize(18, 18));
    treeBtn_->setToolTip(tx("Show/hide assembly tree (T)"));
    treeBtn_->setCursor(Qt::PointingHandCursor);
    treeBtn_->setFocusPolicy(Qt::NoFocus);
    treeBtn_->setFixedSize(26, 56);
    treeBtn_->setStyleSheet(
        "QToolButton{border:1px solid palette(mid);border-left:none;border-top-right-radius:8px;"
        "border-bottom-right-radius:8px;background:palette(window);}"
        "QToolButton:hover{background:palette(midlight);}");
    auto* fitBtn = makeButton(tx("Fit"), "arrows-maximize", tx("Fit to view (F)"));
#ifdef CADSCOUT_LITE
    auto* edgesBtn = makeButton(tx("Edges"), "cube", tx("Show Edges"), true);
#else
    auto* edgesBtn = makeButton(tx("Edges"), "cube", tx("Show edges (E)"), true);
#endif
    edgesBtn->setChecked(true);
    auto* settingsBtn = makeButton(tx("Settings"), "settings", tx("Settings"));
    auto* aboutBtn = makeButton(tx("About"), "info-circle", tx("About CADscout (F1)"));
#ifdef CADSCOUT_LITE
    auto* fullBtn = makeButton(tx("Open in Full"), "external-link", tx("Open Full Edition (coming soon)"));
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
#ifdef CADSCOUT_LITE
        if (loading_) cancelLoad();
#endif
        if (popup_) popup_->hide();
        if (infoTab_ && infoTab_->isChecked()) infoTab_->setChecked(false);
    });
    addShortcut(Qt::Key_F, [this] { viewport_->fitToView(); });
#ifdef CADSCOUT_LITE
    addShortcut(Qt::Key_Right, [this] { browseFolder(+1); });
    addShortcut(Qt::Key_Left, [this] { browseFolder(-1); });
    addShortcut(QKeySequence::Copy, [this] { copyImage(); });
    addShortcut(Qt::Key_F5, [this] { reloadFile(); });
    addShortcut(QKeySequence::New, [this] { newWindow(); });
#endif
#ifndef CADSCOUT_LITE
    addShortcut(Qt::Key_E, [edgesBtn] { edgesBtn->toggle(); });
    addShortcut(Qt::Key_H, [this] { viewport_->goHome(); });
    addShortcut(Qt::Key_T, [this] {
        if (treeBtn_->isEnabled()) treeBtn_->toggle();
    });
#endif

    cube_ = new ViewCube(viewport_);
#ifndef CADSCOUT_LITE  // Lite hides the cube, so it does not need to follow the camera
    connect(viewport_, &Viewport::orientationChanged, cube_, &ViewCube::setOrientation);
    connect(cube_, &ViewCube::viewRequested, viewport_, &Viewport::setViewDirection);
#endif
    viewport_->goHome();

    homePanel_ = new Panel(viewport_);
    auto* homeRow = new QHBoxLayout(homePanel_);
    homeRow->setContentsMargins(6, 6, 6, 6);
    auto* homeBtn = new QToolButton(homePanel_);
#ifndef CADSCOUT_LITE
    homeBtn->setText(tx("Home"));
    homeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
#endif
    homeBtn->setIconSize(QSize(20, 20));
    setBtnIcon(homeBtn, "home");
#ifdef CADSCOUT_LITE
    homeBtn->setToolTip(tx("Isometric View"));
#else
    homeBtn->setToolTip(tx("Isometric view, fit to screen (H)"));
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
    perspBtn_->setToolTip(tx("Perspective On/Off"));
    perspBtn_->setCursor(Qt::PointingHandCursor);
    perspBtn_->setFocusPolicy(Qt::NoFocus);
    perspBtn_->setStyleSheet(btnStyle);
    homeRow->setSpacing(4);
    homeRow->insertWidget(0, perspBtn_);
    connect(perspBtn_, &QToolButton::toggled, this, [this](bool on) {
        viewport_->setPerspectiveEnabled(on);
        refreshIcons();
    });

    // Copy the view as a picture with a transparent background, first in the corner panel
    copyBtn_ = new QToolButton(homePanel_);
    copyBtn_->setIconSize(QSize(20, 20));
    setBtnIcon(copyBtn_, "camera");
    copyBtn_->setToolTip(tx("Snapshot"));
    copyBtn_->setCursor(Qt::PointingHandCursor);
    copyBtn_->setFocusPolicy(Qt::NoFocus);
    copyBtn_->setStyleSheet(btnStyle);
    homeRow->insertWidget(0, copyBtn_);
    connect(copyBtn_, &QToolButton::clicked, this, &MainWindow::copyImage);

    // Right click in the view (without dragging): hide the part under the cursor, show all again.
    connect(viewport_, &Viewport::contextRequested, this, &MainWindow::showPartMenu);

    // Progress card at the bottom of the view while a file loads (shown only when loading takes a moment).
    loadCard_ = new Panel(viewport_);
    auto* cardBox = new QVBoxLayout(loadCard_);
    cardBox->setContentsMargins(16, 12, 16, 12);
    cardBox->setSpacing(10);
    loadText_ = new QLabel(loadCard_);
    loadText_->setStyleSheet("QLabel{background:transparent;font-size:13px;}");
    auto* barRow = new QHBoxLayout;
    barRow->setSpacing(14);
    loadBar_ = new ProgressStrip(loadCard_);
    cancelBtn_ = new QPushButton(tx("Cancel"), loadCard_);
    cancelBtn_->setCursor(Qt::PointingHandCursor);
    cancelBtn_->setFocusPolicy(Qt::NoFocus);
    cancelBtn_->setToolTip(tx("Stop loading (Esc)"));
    barRow->addWidget(loadBar_, 1);
    barRow->addWidget(cancelBtn_);
    cardBox->addWidget(loadText_);
    cardBox->addLayout(barRow);
    loadCard_->setFixedWidth(400);
    loadCard_->hide();
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancelLoad);
    progressTimer_.setInterval(100);
    connect(&progressTimer_, &QTimer::timeout, this, &MainWindow::updateLoadCard);
#endif

    // Normal status bar along the bottom of the window (under the tree and the 3D view).
    status_ = new QLabel(central);
    status_->setStyleSheet("QLabel{background:palette(window);color:palette(window-text);"
                           "border-top:1px solid palette(mid);padding:0 12px;font-size:12px;}");
    status_->setFixedHeight(26);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // long text is shortened, not wider than the window
    vbox->addWidget(status_);
    setStatus(tx("Ready"));
    connect(viewport_, &Viewport::partSelected, this, [this](int part) {
        if (!model_) return;
        const QString name = part >= 0 ? QString::fromStdString(model_->parts[size_t(part)].name) : QString();
        QString text = name.isEmpty() ? info_ : name + "  ·  " + info_;
#ifdef CADSCOUT_LITE
        if (part >= 0) {  // a clicked part shows its own size, not the size of the whole model
            const QString size = partSizeText(size_t(part));
            text = (name.isEmpty() ? QString() : name + "  ·  ") + infoBase_ + (size.isEmpty() ? QString() : "  ·  " + size);
        }
#endif
        setStatus(text);

#ifndef CADSCOUT_LITE  // Lite has no tree
        QSignalBlocker block(tree_);
        QTreeWidgetItem* item = part >= 0 && size_t(part) < partItems_.size() ? partItems_[size_t(part)] : nullptr;
        if (item) {
            tree_->setCurrentItem(item);
            tree_->scrollToItem(item);
        } else {
            tree_->clearSelection();
            tree_->setCurrentItem(nullptr);
        }
#endif
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
    connect(viewport_, &Viewport::graphicsFailed, this,
            [this](const QString& message) { QMessageBox::critical(this, "CADscout", message); });
    connect(viewport_, &Viewport::partClicked, this, [this, showPopup](int part, const QPoint& pos) {
        if (!model_ || part < 0 || size_t(part) >= model_->parts.size()) return;
        const Part& p = model_->parts[size_t(part)];
        if (p.object.empty() && p.filaments.empty()) return;  // not a Bambu/Orca part
        QString html = "<b>" + QString::fromStdString(p.object.empty() ? p.name : p.object).toHtmlEscaped() + "</b>";
        for (const FilamentUse& f : p.filaments) {
            html += "<br>";
            QString line = tx("Filament %1").arg(f.number);
            // Vendor and type read as one item: "Bambu Lab PLA"
            QString material = QString::fromStdString(f.vendor);
            if (!f.type.empty()) material += (material.isEmpty() ? "" : " ") + QString::fromStdString(f.type);
            if (!material.isEmpty()) line += "  ·  " + material;
            if (f.hasColor) {
                const QColor c = QColor::fromRgbF(f.color[0], f.color[1], f.color[2]);
                html += QString("<span style=\"color:%1\">&#9632;</span> ").arg(c.name());
                line += "  ·  " + c.name().toUpper();
            } else {
#ifdef CADSCOUT_LITE
                line += "  ·  " + tx("Color not set");
#else
                line += "  ·  " + tx("colour not set");
#endif
            }
            html += line.toHtmlEscaped();
        }
        showPopup(html, pos);
    });
    connect(viewport_, &Viewport::plateClicked, this, [this, showPopup](int plate, const QPoint& pos) {
        if (!model_ || plate < 0 || size_t(plate) >= model_->plates.size()) return;
        const PlateShape& pl = model_->plates[size_t(plate)];
#ifdef CADSCOUT_LITE
        QString time = tx("Not available (file is not sliced)");
#else
        QString time = tx("not available (file is not sliced)");
#endif
        if (pl.printSeconds >= 0) {
            const int total = int(pl.printSeconds + 0.5), h = total / 3600, m = (total % 3600) / 60;
            time = h > 0 ? QString("%1 h %2 m").arg(h).arg(m, 2, 10, QLatin1Char('0')) : QString("%1 m").arg(m);
        }
        showPopup("<b>" + (pl.name.empty() ? tx("Plate") : QString::fromStdString(pl.name)).toHtmlEscaped() + "</b><br>" +
                      tx("Print time: %1").arg(time).toHtmlEscaped(),
                  pos);
    });

    // Project information (3MF files): a small tab at the top right opens a panel with the file's own information.
    infoPanel_ = new InfoPanel(viewport_);
    infoPanel_->hide();
    infoTab_ = new QToolButton(viewport_);
    infoTab_->setCheckable(true);
    infoTab_->setIconSize(QSize(18, 18));
    setBtnIcon(infoTab_, "printer");
    infoTab_->setToolTip(tx("Project information (I)"));
    infoTab_->setCursor(Qt::PointingHandCursor);
    infoTab_->setFocusPolicy(Qt::NoFocus);
    infoTab_->setFixedSize(36, 56);  // wide enough that the icon has room on both sides of its rounded left edge
    infoTab_->setStyleSheet(
        "QToolButton{border:1px solid palette(mid);border-right:none;border-top-left-radius:10px;"
        "border-bottom-left-radius:10px;background:palette(window);padding-left:4px;}"
        "QToolButton:hover{background:palette(midlight);}"
        "QToolButton:checked{background:palette(midlight);}");
    infoTab_->hide();
    infoTab_->installEventFilter(this);
    connect(infoTab_, &QToolButton::toggled, this, [this](bool open) {
        infoPanel_->setVisible(open);
        layoutOverlays();
    });
    addShortcut(Qt::Key_I, [this] {
        if (infoTab_->isVisible()) infoTab_->toggle();
    });

    hint_ = new QLabel(tx("Drop a 3D model here\nor press Ctrl+O"), viewport_);
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

    brandLabel_ = new QLabel(toolbar_);
    brandLabel_->setWordWrap(false);  // always one line
    brandLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);  // the bar under it still drags the window
    brandLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    brandLabel_->setStyleSheet("QLabel{font-size:15px;font-weight:bold;background:transparent;padding:0 12px 0 6px;}");
    row->insertWidget(0, brandLabel_);
    row->setAlignment(brandLabel_, Qt::AlignVCenter);
    updateBrand();
    // The name of the open file sits in the middle of the bar; in Lite a small arrow next to it opens the same menu as a right click.
    nameBox_ = new QWidget(toolbar_);
    nameBox_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* nameRow = new QHBoxLayout(nameBox_);
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(0);
    titleLabel_ = new TitleLabel(nameBox_);
    nameRow->addStretch(1);
    nameRow->addWidget(titleLabel_);
    nameRow->addStretch(1);
    row->addWidget(nameBox_, 1);
    row->setContentsMargins(8, 0, 0, 0);
#ifndef CADSCOUT_LITE
    for (QToolButton* b : {openBtn_, fitBtn, edgesBtn, settingsBtn, aboutBtn}) row->setAlignment(b, Qt::AlignVCenter);
#endif
#ifdef CADSCOUT_LITE
    // Icon-only buttons: Open on the left; Open in Full and Settings go right, before the window controls.
    row->removeWidget(settingsBtn);
    row->removeWidget(fullBtn);
    row->removeWidget(aboutBtn);
    row->addWidget(fullBtn);
    row->addWidget(aboutBtn);
    row->addWidget(settingsBtn);
    // Small arrow right of Open: recent files.
    recentBtn_ = makeButton(QString(), "chevron-down", tx("Recent Files"));
    recentBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    recentBtn_->setIconSize(QSize(16, 16));
    recentBtn_->setFixedSize(24, 46);
    recentBtn_->setStyleSheet(btnStyle + "QToolButton{padding:0;}");
    row->removeWidget(recentBtn_);
    row->insertWidget(row->indexOf(openBtn_) + 1, recentBtn_);
    row->setAlignment(recentBtn_, Qt::AlignVCenter);
    connect(recentBtn_, &QToolButton::clicked, this, &MainWindow::showRecentMenu);
    // Arrow right of the file name: the same menu as a right click on the bar (Reload, Show in folder, ...), so it can be found.
    nameArrow_ = new QToolButton(nameBox_);
    nameArrow_->setIconSize(QSize(14, 14));
    setBtnIcon(nameArrow_, "chevron-down");
    nameArrow_->setToolTip(tx("Filename Menu"));
    nameArrow_->setCursor(Qt::PointingHandCursor);
    nameArrow_->setFocusPolicy(Qt::NoFocus);
    nameArrow_->setFixedSize(20, 30);
    // padding-top moves the arrow down a little: the arrow shape sits higher than the middle of the letters next to it
    nameArrow_->setStyleSheet(btnStyle + "QToolButton{padding:7px 0 0 0;min-height:0;}");
    nameArrow_->hide();  // shown once a file is open
    nameRow->insertWidget(nameRow->count() - 1, nameArrow_);  // between the name and the right-hand stretch
    connect(nameArrow_, &QToolButton::clicked, this, [this] {
        showBarMenu(nameArrow_->mapToGlobal(QPoint(0, nameArrow_->height())));
        clearStuckHover(nameArrow_);
    });
    for (QToolButton* b : {openBtn_, fullBtn, aboutBtn, settingsBtn}) {
        b->setText(QString());
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setFixedSize(46, 46);
        row->setAlignment(b, Qt::AlignVCenter);
    }
#endif
    buildWindowControls(row, btnStyle);
    layoutOverlays();
}

MainWindow::~MainWindow() {
    // Pending onLoaded() events are discarded when this QObject is destroyed. A load still running is cancelled first, so
    // closing the window does not leave the program running in the background until the file is read.
    if (progress_) progress_->cancel = true;
    if (loader_) loader_->wait();
}

void MainWindow::setBtnIcon(QToolButton* b, const QString& name) {
    iconButtons_.emplace_back(b, name);
    // Only this button's icon; refreshIcons redraws all of them when the theme changes.
    const QColor fg = dark_ ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x22, 0x22, 0x22);
    b->setIcon(tablerIcon(name, fg, Qt::white, 20, devicePixelRatioF()));
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
#endif
    updateWindowControls();
    updateBrand();
    if (infoPanel_) infoPanel_->refresh();  // its colours follow the theme
}

void MainWindow::updateBrand() {
    if (!brandLabel_) return;
    const QString blue = dark_ ? "#2f9bff" : "#0a7ae0";
    QString html = QString("CAD<span style=\"color:%1\">scout</span>").arg(blue);
#ifdef CADSCOUT_LITE
    html += QString(" <span style=\"font-weight:400;color:%1\">Lite</span>").arg(dark_ ? "#b8b8b8" : "#666666");
#endif
    brandLabel_->setText(html);
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
        treeBtn_->setToolTip(tx("This format (%1) has only one object, so there is no assembly tree.")
                                 .arg(QString::fromStdString(model_->formatName)));
    } else if (!treeBtn_->isEnabled()) {
        treeBtn_->setEnabled(true);
        treeBtn_->setCursor(Qt::PointingHandCursor);
        treeBtn_->setToolTip(tx("Show/hide assembly tree (T)"));
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
    // The open info panel covers the right side of the view: the corner buttons and the loading card move left of it.
    const int panelW = infoPanel_ && infoPanel_->isVisible() ? std::clamp(w - 60, 220, 400) : 0;
    homePanel_->move(std::max(10, w - panelW - homePanel_->width() - 10), h - homePanel_->height() - 10);
#else
    homePanel_->move(cube_->x() - homePanel_->width() - 6, h - homePanel_->height() - 14);
#endif
    homePanel_->raise();

#ifdef CADSCOUT_LITE
    if (loadCard_) {
        loadCard_->adjustSize();
        loadCard_->move(std::max(0, (w - panelW - loadCard_->width()) / 2), h - loadCard_->height() - 28);
        loadCard_->raise();
    }
#endif

    if (infoPanel_ && infoTab_) {  // the panel covers the right side of the view; its tab sits on its left edge
        const int panelW = std::clamp(w - 60, 220, 400);
        infoPanel_->setGeometry(w - panelW, 0, panelW, h);
        infoTab_->move(infoTab_->isChecked() ? w - panelW - infoTab_->width() : w - infoTab_->width(), 14);
        if (infoPanel_->isVisible()) infoPanel_->raise();
        infoTab_->raise();
    }

    // Shorten the status text with "..." when it is wider than the window (the full text is in the tooltip).
    status_->setText(QFontMetrics(status_->font()).elidedText(statusText_, Qt::ElideRight, std::max(50, width() - 40)));

    hint_->setGeometry(0, 0, w, h);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == viewport_ && e->type() == QEvent::Resize) layoutOverlays();
    if (obj == infoTab_ && e->type() == QEvent::Wheel) return true;  // the wheel over the info tab must not zoom the model
    if (obj == closeBtn_ && (e->type() == QEvent::Enter || e->type() == QEvent::Leave)) {
        closeHover_ = e->type() == QEvent::Enter;
        updateWindowControls();
    } else if (obj == toolbar_) {
#ifdef CADSCOUT_LITE
        if (e->type() == QEvent::ContextMenu) {  // right-click on the empty part of the bar
            showBarMenu(static_cast<QContextMenuEvent*>(e)->globalPos());
            return true;
        }
#endif
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
    return QMainWindow::eventFilter(obj, e);
}

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
    minBtn_ = make(tx("Minimize"));
    maxBtn_ = make(tx("Maximize"));
    closeBtn_ = make(tx("Close"));
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
    maxBtn_->setToolTip(isMaximized() ? tx("Restore") : tx("Maximize"));
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

bool MainWindow::event(QEvent* e) {
    // Moved to (or opened on) a monitor with another display scale: draw the icons again at the new sharpness.
    if (e->type() == QEvent::DevicePixelRatioChange) refreshIcons();
    return QMainWindow::event(e);
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        updateWindowControls();
        layoutResizeGrips();
    }
}

void MainWindow::openDialog() {
    if (loading_) {
        setStatus(tx("Still loading %1 - please wait until it has finished.").arg(QFileInfo(currentFile_).fileName()));
        return;
    }
    const QString path = askForModel();
    if (!path.isEmpty()) openFile(path);
}

QString MainWindow::askForModel() {
    QSettings s;
    const QString dir = s.value("lastDir").toString();
#ifdef CADSCOUT_LITE
    const QString zipPattern = " *.zip";  // Lite opens models inside zip files
    const QString zipFilter = tx("ZIP Files") + " (*.zip);;";
    const QString title = tx("Open 3D Model"), models = tx("3D Models"), all = tx("All Files");
#else
    const QString zipPattern, zipFilter;
    const QString title = tx("Open 3D model"), models = tx("3D models"), all = tx("All files");
#endif
    const QString path = QFileDialog::getOpenFileName(
        this, title, dir,
        models + " (" + modelFilterPatterns() + zipPattern + ");;"
        "STEP (*.stp *.step);;IGES (*.igs *.iges);;BREP (*.brep);;STL (*.stl);;OBJ (*.obj);;PLY (*.ply);;"
        "VRML (*.wrl *.vrml);;glTF (*.gltf *.glb);;3MF (*.3mf);;DXF (*.dxf);;" + zipFilter + all + " (*.*)");
    if (!path.isEmpty()) s.setValue("lastDir", QFileInfo(path).absolutePath());
    return path;
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
        ModelCache::setEnabled(dlg.cacheEnabled());
        ModelCache::setLimitMegabytes(dlg.cacheLimitMegabytes());
        ModelCache::setMinLoadSeconds(dlg.cacheMinSeconds());
#ifdef CADSCOUT_LITE
        arrowKeys_ = dlg.browseWithArrows();
        QSettings().setValue("browse/arrowKeys", arrowKeys_);
        autoReload_ = dlg.autoReload();
        QSettings().setValue("browse/autoReload", autoReload_);
        inches_ = dlg.useInches();
        QSettings().setValue("display/inches", inches_);
        const int language = dlg.languageSetting();
        if (language != QSettings().value("ui/language", 0).toInt()) {
            QSettings().setValue("ui/language", language);
            if (languageFromSetting(language) != currentLanguage())
                QMessageBox::information(this, "CADscout", tx("The new language is used the next time CADscout starts."));
        }
        if (model_ && !loading_) {  // the unit of the size in the status bar may have changed
            updateOverallInfo();
            viewport_->selectPart(viewport_->selectedPart());  // a selected part shows its own name and size again
        }
#endif
        const PlateSettings plate = dlg.plateSettings();
        plate.save();
        viewport_->setPlateSettings(plate);
    }
}

#ifdef CADSCOUT_LITE
void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings().setValue("window/geometry", saveGeometry());
    QMainWindow::closeEvent(e);
}

QString MainWindow::sizeText(float x, float y, float z) const {
    // Inches only where the file's unit is known to be millimetres; other files have no unit and are shown as they are.
    const bool inch = inches_ && !sizeUnit_.isEmpty();
    const float k = inch ? 1.f / 25.4f : 1.f;
    auto number = [inch](float v) {
        return QString::number(v, 'f', inch ? (v >= 100.f ? 1 : 2) : (v >= 100.f ? 0 : v >= 10.f ? 1 : 2));
    };
    return QString("%1 × %2 × %3%4").arg(number(x * k), number(y * k), number(z * k), inch ? QString(" in") : sizeUnit_);
}

void MainWindow::updateOverallInfo() {
    info_ = infoBase_;
    // With several print plates the box around everything belongs to no part, so only a clicked part shows a size.
    if (model_ && model_->plates.size() <= 1)
        info_ += "  ·  " + sizeText(model_->boundsMax[0] - model_->boundsMin[0], model_->boundsMax[1] - model_->boundsMin[1],
                                    model_->boundsMax[2] - model_->boundsMin[2]);
    if (!loading_) setStatus(info_);
}

void MainWindow::flashStatus(const QString& text) {
    const QString before = statusText_;
    setStatus(text);
    QTimer::singleShot(2500, this, [this, text, before] {
        if (statusText_ == text) setStatus(before);
    });
}

void MainWindow::copyImage() {
    if (!viewport_->hasModel()) {
        flashStatus(tx("Open a model first, then copy its view."));
        return;
    }
    if (model_ && !model_->parts.empty() && viewport_->hiddenCount() >= int(model_->parts.size())) {
        flashStatus(tx("All parts are hidden, so there is nothing to copy."));
        return;
    }
    QApplication::setOverrideCursor(Qt::BusyCursor);  // a big picture takes a moment to prepare
    bool nothingVisible = false;
    const QImage image = viewport_->grabTransparentImage(3, &nothingVisible);
    const bool copied = !image.isNull() && ClipboardImage::copy(image, reinterpret_cast<void*>(winId()));
    QApplication::restoreOverrideCursor();
    if (nothingVisible) {
        flashStatus(tx("Nothing of the model is in the view, so there is nothing to copy. Press F to fit the model."));
        return;
    }
    if (!copied) {
        flashStatus(tx("Could not copy the picture to the clipboard."));
        return;
    }
    flashStatus(tx("View copied as a picture (%1 × %2, transparent background)").arg(image.width()).arg(image.height()));
}

void MainWindow::reloadFile() {
    if (currentFile_.isEmpty() || loading_) return;
    keepViewNext_ = true;
    openFile(currentFile_);
}

void MainWindow::scheduleReload() {
    if (autoReload_) reloadTimer_.start(800);
}

// Called a moment after the file changed. A program that saves a big file writes it in pieces (or replaces it), so the
// size and time must stay the same between two checks before the file is read again.
void MainWindow::checkReload() {
    if (!autoReload_ || currentFile_.isEmpty()) return;
    const QFileInfo fi(currentFile_);
    if (!fi.exists()) {  // being replaced: wait for the new file (slowly after ~16 s, but never give up)
        reloadTimer_.start(++missingChecks_ < 20 ? 800 : 5000);
        return;
    }
    missingChecks_ = 0;
    reloadTimer_.setInterval(800);
    if (!watcher_.files().contains(currentFile_)) watcher_.addPath(currentFile_);  // replaced files lose their watch
    if (fi.size() == loadedSize_ && fi.lastModified() == loadedTime_) return;
    if (loading_ || fi.size() != seenSize_ || fi.lastModified() != seenTime_) {
        seenSize_ = fi.size();
        seenTime_ = fi.lastModified();
        reloadTimer_.start();
        return;
    }
    reloadFile();
}

void MainWindow::showInFolder() {
    if (currentFile_.isEmpty()) return;
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(currentFile_)});
}

// Menus with icons: the same space left of the icon as right of the text, and a short gap between icon and text.
void MainWindow::iconMenu(QMenu& menu) {
    roundMenu(menu);
    menu.setStyleSheet("QMenu::item{padding:8px 10px 8px 8px;}QMenu::icon{padding-left:14px;}");
}

QIcon MainWindow::menuIcon(const char* name) const {
    const QColor fg = dark_ ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x22, 0x22, 0x22);
    return tablerIcon(name, fg, fg, 16, devicePixelRatioF());
}

void MainWindow::showBarMenu(const QPoint& globalPos) {
    QMenu menu(this);
    iconMenu(menu);
    const bool hasFile = !currentFile_.isEmpty();
    auto add = [&](const char* icon, const QString& text, bool enabled, auto slot) {
        QAction* a = menu.addAction(menuIcon(icon), text);
        a->setEnabled(enabled);
        connect(a, &QAction::triggered, this, slot);
    };
    add("refresh", tx("Reload File"), hasFile && !loading_, [this] { reloadFile(); });
    add("folder", tx("Show in Folder"), hasFile, [this] { showInFolder(); });
    add("clipboard", tx("Copy File Path"), hasFile, [this] {
        QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(currentFile_));
        flashStatus(tx("File path copied"));
    });
    menu.addSeparator();
    add("camera", tx("Snapshot"), viewport_->hasModel(), [this] { copyImage(); });
    menu.addSeparator();
    add("app-window", tx("Open in New Window"), true, [this] { newWindow(); });
    menu.exec(globalPos);
}

void MainWindow::newWindow() {
    const QString path = askForModel();
    if (path.isEmpty()) return;
    // A second program window with its own model, slightly offset so it does not hide this one (see main.cpp).
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--new-window", path}))
        QMessageBox::warning(this, "CADscout", tx("The new window could not be started."));
}

QString MainWindow::partSizeText(size_t part) const {
    if (!model_ || part >= model_->parts.size()) return {};
    constexpr float kInf = std::numeric_limits<float>::infinity();
    float lo[3] = {kInf, kInf, kInf}, hi[3] = {-kInf, -kInf, -kInf};
    for (const PartRange& r : model_->parts[part].ranges) {
        if (r.batch < 0 || size_t(r.batch) >= model_->batches.size()) continue;
        const MeshBatch& b = model_->batches[size_t(r.batch)];
        for (uint32_t i = r.start; i < r.start + r.count && i < b.indices.size(); ++i) {
            const float* v = &b.vertices[size_t(b.indices[i]) * 6];
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], v[k]);
                hi[k] = std::max(hi[k], v[k]);
            }
        }
    }
    if (lo[0] > hi[0]) return {};  // no triangles (for example a line-only part)
    return sizeText(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
}

void MainWindow::showPartMenu(int part, const QPoint& pos) {
    if (!model_) return;
    if (popup_) popup_->hide();
    QMenu menu(this);
    iconMenu(menu);
    auto icon = [this](const char* name) { return menuIcon(name); };
    if (part >= 0 && size_t(part) < model_->parts.size()) {
        viewport_->selectPart(part);  // highlight the part the menu is about (as a left click would)
        viewport_->repaint();         // now, before the menu opens and blocks
        const QString name = QString::fromStdString(model_->parts[size_t(part)].name);
        connect(menu.addAction(icon("eye-off"), tx("Hide")), &QAction::triggered, this, [this, part, name] {
            viewport_->setPartHidden(part, true);
            flashStatus(tx("Hidden: %1. Right-click and choose \"Show All\" to bring it back.").arg(name));
        });
        if (model_->parts.size() > 1) {
            connect(menu.addAction(icon("target"), tx("Show Only")), &QAction::triggered, this,
                    [this, part] { viewport_->showOnlyPart(part); });
            // Transparent parts are only reached where nothing solid is behind them (see Viewport::mouseReleaseEvent).
            const bool on = !viewport_->partTransparent(part);
            connect(menu.addAction(icon(on ? "layers-intersect" : "cube"), on ? tx("Make Transparent") : tx("Make Solid")),
                    &QAction::triggered, this, [this, part, on] {
                        viewport_->setPartTransparent(part, on);
                        if (on)
                            flashStatus(tx("Transparent parts cannot be clicked: the parts behind them can. Right-click and "
                                           "choose \"Make All Solid\" to undo."));
                    });
        }
        menu.addSeparator();
    }
    const int hidden = viewport_->hiddenCount();
    QAction* all = menu.addAction(icon("eye"), hidden > 0 ? tx("Show All (%1 hidden)").arg(hidden) : tx("Show All"));
    all->setEnabled(hidden > 0);
    connect(all, &QAction::triggered, this, [this] { viewport_->showAllParts(); });
    const int clear = viewport_->transparentCount();
    if (clear > 0)
        connect(menu.addAction(icon("cube"), tx("Make All Solid (%1 transparent)").arg(clear)), &QAction::triggered, this,
                [this] { viewport_->makeAllSolid(); });
    menu.exec(viewport_->mapToGlobal(pos));
}

void MainWindow::updateLoadCard() {
    if (!loading_ || !progress_) return;
    if (loadCard_->isHidden() && loadClock_.elapsed() > 500) {  // small files load before the card would flash
        loadCard_->show();
        layoutOverlays();
    }
    static const char* const stages[] = {"Reading the file", "Translating the model", "Repairing the parts",
                                         "Meshing the surfaces", "Preparing the view"};
    const int stage = std::clamp(progress_->stage.load(), 0, 4);
    const float fraction = progress_->fraction.load();
    const bool known = fraction > 0.001f;
    QString text;
    if (progress_->cancel.load()) {
        text = tx("Cancelling...");
    } else {
        text = tx(stages[stage]) + "...";
        if (known) text += QString("   %1 %").arg(int(fraction * 100.f));
    }
    loadText_->setText(text);
    static_cast<ProgressStrip*>(loadBar_)->setValue(known ? fraction : -1.f);
}

void MainWindow::cancelLoad() {
    if (!loading_ || !progress_) return;
    progress_->cancel = true;
    cancelBtn_->setEnabled(false);
    updateLoadCard();
}

void MainWindow::browseFolder(int step) {
    const QString from = browseFrom_.isEmpty() ? currentFile_ : browseFrom_;  // after a failed open: the failed file
    if (loading_ || from.isEmpty() || !arrowKeys_) return;
    const QFileInfo current(from);
    QStringList filters;
    for (const QString& e : modelExtensions()) filters << "*." + e;
    QStringList names = current.dir().entryList(filters, QDir::Files);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), collator);
    if (names.isEmpty()) return;
    const int count = int(names.size());
    int target = -1;
    for (int i = 0; i < count; ++i)
        if (names[i].compare(current.fileName(), Qt::CaseInsensitive) == 0) target = i;
    if (target >= 0) {
        if (count < 2) return;
        target = (target + step + count) % count;
    } else {
        // The open file is not in the list (renamed, deleted or an unlisted type): continue from where its name would
        // sort, so no file is skipped.
        int after = 0;  // first name that sorts after the open file
        while (after < count && collator.compare(names[after], current.fileName()) < 0) ++after;
        target = step > 0 ? after % count : (after - 1 + count) % count;
    }
    openFile(current.dir().filePath(names[target]));
}

void MainWindow::rememberRecent(const QString& path) {
    const QString clean = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QSettings s;
    QStringList list = s.value("recentFiles").toStringList();
    list.removeIf([&](const QString& f) { return f.compare(clean, Qt::CaseInsensitive) == 0; });
    list.prepend(clean);
    while (list.size() > 8) list.removeLast();
    s.setValue("recentFiles", list);
}

// While a menu is open it holds the mouse, so the button that opened it never hears that the pointer left it: after a click
// elsewhere closes the menu the button would keep its "hover" colour. Forget the hover unless the pointer is still on it.
void MainWindow::clearStuckHover(QToolButton* b) {
    b->setDown(false);
    if (!b->rect().contains(b->mapFromGlobal(QCursor::pos()))) b->setAttribute(Qt::WA_UnderMouse, false);
    b->update();
}

void MainWindow::showRecentMenu() {
    // No disk access here (a recent file on an offline network drive would freeze the window): a missing file is
    // noticed when it is chosen (openRecent).
    const QStringList list = QSettings().value("recentFiles").toStringList();
    QMenu menu(this);
    roundMenu(menu);
    if (list.isEmpty()) menu.addAction(tx("No Recent Files"))->setEnabled(false);
    for (const QString& f : list) {
        QAction* a = menu.addAction(QFileInfo(f).fileName());
        a->setToolTip(f);
        connect(a, &QAction::triggered, this, [this, f] { openRecent(f); });
    }
    if (!list.isEmpty()) {
        menu.addSeparator();
        connect(menu.addAction(tx("Clear List")), &QAction::triggered, this, [] { QSettings().remove("recentFiles"); });
    }
    menu.setToolTipsVisible(true);
    menu.exec(recentBtn_->mapToGlobal(QPoint(0, recentBtn_->height())));
    clearStuckHover(recentBtn_);
}

void MainWindow::openRecent(const QString& path) {
    if (!QFileInfo::exists(path)) {
        QStringList list = QSettings().value("recentFiles").toStringList();
        list.removeAll(path);
        QSettings().setValue("recentFiles", list);
        QMessageBox::information(this, "CADscout", tx("This file no longer exists:\n%1").arg(path));
        return;
    }
    openFile(path);
}

void MainWindow::openInFull() {
    const QString fullPath = QDir(QCoreApplication::applicationDirPath()).filePath("CADscoutFull.exe");
    if (!QFileInfo::exists(fullPath)) {
        QMessageBox::information(this, tx("CADscout Full is coming soon"),
                                 tx("CADscout Full will be available soon. Once it is released, install it to open this "
                                    "model in it."));
        return;
    }

    // Full cannot read zip files: give it the copy unpacked when the model was opened (if it is still there; nothing is
    // unpacked here, so the window never waits).
    const QString file = zipEntry_.isEmpty() ? currentFile_ : sourceFile_;
    const QStringList args = file.isEmpty() || !QFileInfo::exists(file) ? QStringList{} : QStringList{file};
    if (!QProcess::startDetached(fullPath, args)) {
        QMessageBox::warning(this, tx("Unable to open Full edition"), tx("The Full edition was found but could not be started."));
        return;
    }
    close();
}
#endif

#ifdef CADSCOUT_LITE
QString MainWindow::chooseZipEntry(const QString& zipPath) {
    std::vector<std::string> exts;
    for (const QString& e : modelExtensions()) exts.push_back(e.toStdString());
    std::vector<std::string> names;
    try {
        names = zipModelEntries(zipPath.toUtf8().toStdString(), exts);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "CADscout", QString::fromUtf8(e.what()));
        return {};
    }
    if (names.empty()) {
        QMessageBox::information(this, "CADscout", tx("This zip file holds no model file that CADscout can open."));
        return {};
    }
    if (names.size() == 1) return QString::fromStdString(names.front());

    // Several models: let the user pick one from a menu in the middle of the view.
    QMenu menu(this);
    roundMenu(menu);
    menu.setToolTipsVisible(true);
    QAction* title = menu.addAction(tx("Open which model from %1?").arg(QFileInfo(zipPath).fileName()));
    title->setEnabled(false);
    menu.addSeparator();
    QString chosen;
    for (const std::string& n : names) {
        const QString full = QString::fromStdString(n);
        QAction* a = menu.addAction(QFileInfo(full).fileName());
        a->setToolTip(full);
        connect(a, &QAction::triggered, this, [&chosen, full] { chosen = full; });
    }
    const QSize hint = menu.sizeHint();
    const QPoint centre = viewport_->mapToGlobal(viewport_->rect().center());
    menu.exec(centre - QPoint(hint.width() / 2, std::min(hint.height(), viewport_->height()) / 2));
    return chosen;
}

namespace {
// Unpacks a model from a zip into a fixed folder per zip and model, so the model cache recognises it next time. The file
// gets the zip's time: when the zip is unchanged the copy already there is used. *skipped: side files (textures,
// materials) that could not be unpacked; kept in a small note file so a reused copy reports them too.
QString unpackZipModel(const QString& zipPath, const QString& entry, int* skipped) {
    *skipped = 0;
    const QFileInfo zipInfo(zipPath);
    const QByteArray key = QCryptographicHash::hash((zipInfo.absoluteFilePath().toLower() + '|' + entry).toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(16);
    QDir root(QDir::tempPath() + "/CADscout/zip");
    // Old unpacked models are not needed any more: remove the ones not used for a week.
    for (const QFileInfo& d : root.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        if (d.baseName() == QString::fromLatin1(key) || d.lastModified().daysTo(QDateTime::currentDateTime()) <= 7) continue;
        if (d.isDir())
            QDir(d.absoluteFilePath()).removeRecursively();
        else
            QFile::remove(d.absoluteFilePath());  // a ".skipped" note
    }
    const QString dir = root.filePath(QString::fromLatin1(key));
    const QString target = QDir(dir).filePath(QFileInfo(entry).fileName());
    const QFileInfo existing(target);
    // (A folder removed above while its zip is still in use is simply unpacked again, with the same time.)
    // The note lives next to the folder, not inside it: inside it could clash with a file of the archive.
    const QString note = dir + ".skipped";
    if (existing.exists() && existing.lastModified() == zipInfo.lastModified()) {
        QFile n(note);
        if (n.open(QIODevice::ReadOnly)) *skipped = n.readAll().trimmed().toInt();
        return target;
    }
    QDir(dir).removeRecursively();
    QFile::remove(note);
    std::vector<std::string> exts;
    for (const QString& e : modelExtensions()) exts.push_back(e.toStdString());
    const QString written = QString::fromStdString(extractZipModel(zipPath.toUtf8().toStdString(),
                                                                   entry.toUtf8().toStdString(),
                                                                   dir.toUtf8().toStdString(), exts, skipped));
    if (*skipped > 0) {
        QFile n(note);
        if (n.open(QIODevice::WriteOnly)) n.write(QByteArray::number(*skipped));
    }
    QFile f(written);
    if (f.open(QIODevice::ReadWrite)) f.setFileTime(zipInfo.lastModified(), QFileDevice::FileModificationTime);
    return written;
}
}  // namespace
#endif

QString MainWindow::shownName(const QString& path) const {
#ifdef CADSCOUT_LITE
    if (path == currentFile_ && !zipEntry_.isEmpty())
        return QFileInfo(path).fileName() + "  ›  " + QFileInfo(zipEntry_).fileName();
#endif
    return QFileInfo(path).fileName();
}

void MainWindow::openFile(const QString& path) {
    if (loading_) {  // one file at a time: say so instead of ignoring the request
        setStatus(tx("Still loading %1 - please wait until it has finished.").arg(QFileInfo(currentFile_).fileName()));
        return;
    }
#ifdef CADSCOUT_LITE
    // A zip: open a model inside it. A reload keeps the model chosen before; otherwise ask when there are several.
    QString entry;
    if (QFileInfo(path).suffix().compare("zip", Qt::CaseInsensitive) == 0) {
        if (keepViewNext_ && path == currentFile_ && !zipEntry_.isEmpty()) {
            entry = zipEntry_;
        } else if (!pendingEntry_.isEmpty()) {  // chosen earlier, while another file was loading
            entry = pendingEntry_;
            pendingEntry_.clear();
        } else {
            entry = chooseZipEntry(path);
        }
        if (entry.isEmpty()) {
            keepViewNext_ = false;
            return;
        }
        if (loading_) {  // the menu above waits for the user; meanwhile a reload started: open the choice after it
            keepViewNext_ = false;
            pendingZip_ = path;
            pendingEntry_ = entry;
            setStatus(tx("%1 opens when the current loading has finished.").arg(QFileInfo(entry).fileName()));
            return;
        }
    }
    // What the load will read: a save made while it runs is newer and must be reloaded afterwards (checkReload).
    {
        const QFileInfo fi(path);
        startSize_ = fi.size();
        startTime_ = fi.lastModified();
    }
#endif
    loading_ = true;
    browseFrom_.clear();  // set again only if this file fails
    if (popup_) popup_->hide();
    previousFile_ = currentFile_;
    currentFile_ = path;
#ifdef CADSCOUT_LITE
    previousZipEntry_ = zipEntry_;
    zipEntry_ = entry;
#endif
    openBtn_->setEnabled(false);
    hint_->hide();
    setStatus(tx("Loading %1 ...").arg(shownName(path)));
    layoutOverlays();
    QApplication::setOverrideCursor(Qt::BusyCursor);

    const std::string utf8 = path.toUtf8().toStdString();
    const auto quality = MeshQuality(std::clamp(QSettings().value("mesh/quality", 1).toInt(), 0, 2));
    const bool useCache = ModelCache::enabled();
    const int cacheLimit = ModelCache::limitMegabytes();
    const double cacheMinSeconds = ModelCache::minLoadSeconds();
    const bool keepView = keepViewNext_;  // a reload keeps the camera
    keepViewNext_ = false;
    progress_ = std::make_shared<LoadProgress>();
    const std::shared_ptr<LoadProgress> progress = progress_;
#ifdef CADSCOUT_LITE
    loadClock_.restart();
    cancelBtn_->setEnabled(true);
    progressTimer_.start();
#endif
#ifdef CADSCOUT_LITE
    const QString zipEntry = entry;
#else
    const QString zipEntry;
#endif
    QThread* t = QThread::create([this, utf8, path, zipEntry, quality, useCache, cacheLimit, cacheMinSeconds, keepView,
                                  progress] {
        std::shared_ptr<Model> model;
        QString error;
        bool fresh = false;  // read from the file, so it may be worth remembering
        QString source = path;  // the model file itself (for a zip: the unpacked copy)
        int skipped = 0;        // zip side files (textures, materials) that could not be unpacked
        try {
            std::string sourceUtf8 = utf8;
#ifdef CADSCOUT_LITE
            if (!zipEntry.isEmpty()) {
                progress->stage = int(LoadStage::Reading);
                source = unpackZipModel(path, zipEntry, &skipped);
                sourceUtf8 = source.toUtf8().toStdString();
            }
#endif
            if (useCache) model = ModelCache::load(source, int(quality));
            if (!model) {
                model = loadModelFile(sourceUtf8, quality, progress.get());
                fresh = true;
            }
        } catch (const LoadCancelled&) {
            error = kCancelled;
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
            error = tx("Unknown error while reading the file.");
        }
        QMetaObject::invokeMethod(
            this, [this, model, path, error, keepView, source, skipped] { onLoaded(model, path, error, keepView, source, skipped); },
            Qt::QueuedConnection);
        // Saved after the model is on its way to the window, so it does not delay it. Both only read the model.
        if (useCache && fresh && model) {
            try {
                ModelCache::store(source, int(quality), *model, cacheLimit, cacheMinSeconds);
            } catch (...) {
            }
        }
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    loader_ = t;
    t->start();
}

void MainWindow::onLoaded(std::shared_ptr<Model> model, const QString& path, const QString& error, bool keepView,
                          const QString& source, int skipped) {
    Q_UNUSED(source);
    Q_UNUSED(skipped);
    QApplication::restoreOverrideCursor();
    loading_ = false;
    openBtn_->setEnabled(true);
#ifdef CADSCOUT_LITE
    progressTimer_.stop();
    loadCard_->hide();
    if (!pendingZip_.isEmpty()) {  // a zip model chosen while this load ran: open it now
        const QString zip = pendingZip_;
        pendingZip_.clear();
        QTimer::singleShot(0, this, [this, zip] { openFile(zip); });
    }
#endif
    if (error == kCancelled) {  // Cancel pressed: nothing changes, the model shown before stays
        currentFile_ = previousFile_;
#ifdef CADSCOUT_LITE
        zipEntry_ = previousZipEntry_;
#endif
        hint_->setVisible(!viewport_->hasModel());
        layoutOverlays();
        setStatus(viewport_->hasModel() ? tx("Loading cancelled - %1").arg(info_) : tx("Loading cancelled"));
        return;
    }
    if (!model) {
        hint_->setVisible(!viewport_->hasModel());
        layoutOverlays();
        if (keepView) {  // a reload (the file may still be being written): keep showing the old model, no pop-up
            setStatus(tx("Could not reload %1 - still showing the previous version").arg(shownName(path)));
            return;
        }
        setStatus(tx("Failed to load %1").arg(shownName(path)));
        // The model shown before stays, so the file menu, reload and the watcher keep working on that file. The arrow keys
        // continue from the failed file, so a broken file in a folder can be stepped past.
        browseFrom_ = path;
        currentFile_ = previousFile_;
#ifdef CADSCOUT_LITE
        zipEntry_ = previousZipEntry_;
#endif
        QMessageBox::warning(this, "CADscout", error);
        return;
    }
    model_ = model;
    browseFrom_.clear();
    titleLabel_->setText(shownName(path));
    titleLabel_->update();
#ifdef CADSCOUT_LITE
    nameArrow_->show();
#endif
#ifdef CADSCOUT_LITE
    setWindowTitle(shownName(path) + " - CADscout Lite");
#else
    buildTree();
    updateTreeAvailability();
    setWindowTitle(QFileInfo(path).fileName());  // Qt adds " - CADscout Full" (the application display name)
#endif
    // Status bar text: CAD files show loading time, faces, triangles; Bambu/Orca projects show loading time,
    // printer, nozzle size and number of filaments (Model::extraInfo).
    const QString loadTime =
        (model->fromCache ? tx("Loaded in %1 s (from cache)") : tx("Loaded in %1 s")).arg(model->loadSeconds, 0, 'f', 2);
    if (!model->extraInfo.empty())
        info_ = loadTime + "  ·  " + QString::fromStdString(model->extraInfo);
    else
        info_ = loadTime + "  ·  " + tx("%1 faces").arg(model->faceCount) + "  ·  " + tx("%1 triangles").arg(model->triangleCount);
#ifdef CADSCOUT_LITE
    {
        // Model size. The unit is shown only for formats that define it (or where mm is the convention).
        sizeUnit_ = extensionIsMillimetres(QFileInfo(zipEntry_.isEmpty() ? path : zipEntry_).suffix().toLower()) ? " mm" : "";
        sourceFile_ = source;
        // Textures or materials of a zip model that could not be unpacked: say so, or the missing colours look like the file's.
        if (skipped > 0)
            info_ += "  ·  " + plural(skipped, "1 texture/material file could not be unpacked",
                                      "%1 texture/material files could not be unpacked");
        infoBase_ = info_;
        updateOverallInfo();
        rememberRecent(path);
        // Watch the file, so a change on disk can reload it (Settings > General).
        loadedSize_ = startSize_;
        loadedTime_ = startTime_;
        missingChecks_ = 0;
        if (!watcher_.files().isEmpty()) watcher_.removePaths(watcher_.files());
        watcher_.addPath(path);
        // Saved again while it was loading: read the newer version too.
        const QFileInfo fi(path);
        if (fi.size() != loadedSize_ || fi.lastModified() != loadedTime_) scheduleReload();
    }
#endif
    viewport_->setModel(model, keepView);
    if (viewport_->selectedPart() < 0) setStatus(info_);  // a reload keeps the clicked part and its status text
    infoPanel_->setSections(model->sections, keepView);
    infoTab_->setVisible(infoPanel_->hasContent());
    if (!infoPanel_->hasContent()) infoTab_->setChecked(false);
    layoutOverlays();

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
