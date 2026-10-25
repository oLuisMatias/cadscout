#pragma once
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListView>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QString>

// Widths of the arrow areas; shared by the stylesheet (which reserves the space) and the code that draws the arrows.
inline constexpr int kComboArrowWidth = 30;
inline constexpr int kSpinButtonWidth = 24;

// Look of buttons, scroll bars, dropdown lists, menus, number boxes, tool tips and message boxes, applied to the whole
// app (QApplication::setStyleSheet in main.cpp). The small arrows of the lists and number boxes are drawn by the classes
// below.
inline QString modernStyleSheet() {
    return QString(
               "QPushButton{border:1px solid palette(mid);border-radius:8px;padding:7px 18px;background:transparent;"
               "color:palette(window-text);}"
               "QPushButton:hover{background:palette(midlight);}"
               "QPushButton:pressed{background:palette(mid);}"
               "QPushButton:default{background:palette(highlight);color:palette(highlighted-text);"
               "border-color:palette(highlight);}"
               "QScrollBar:vertical{background:transparent;width:10px;margin:0;}"
               "QScrollBar::handle:vertical{background:palette(mid);border-radius:5px;min-height:30px;}"
               "QScrollBar::handle:vertical:hover{background:palette(dark);}"
               "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
               "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
               "QComboBox{min-height:28px;border:1px solid palette(mid);border-radius:8px;padding:0 10px;"
               "background:palette(base);}"
               "QComboBox:hover,QComboBox:on{border-color:palette(highlight);}"
               "QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:center right;width:%1px;border:none;}"
               "QDoubleSpinBox,QSpinBox{min-height:28px;border:1px solid palette(mid);border-radius:8px;padding:0 8px;"
               "background:palette(base);}"
               "QDoubleSpinBox:focus,QSpinBox:focus{border-color:palette(highlight);}"
               "QDoubleSpinBox:disabled,QSpinBox:disabled{color:palette(placeholder-text);}"
               "QDoubleSpinBox::up-button,QSpinBox::up-button{subcontrol-origin:border;subcontrol-position:top right;"
               "width:%2px;border:none;border-top-right-radius:8px;}"
               "QDoubleSpinBox::down-button,QSpinBox::down-button{subcontrol-origin:border;"
               "subcontrol-position:bottom right;width:%2px;border:none;border-bottom-right-radius:8px;}"
               "QDoubleSpinBox::up-button:hover,QSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover,"
               "QSpinBox::down-button:hover{background:palette(midlight);}"
               "QMenu{background:palette(window);color:palette(window-text);border:1px solid palette(mid);"
               "border-radius:10px;padding:6px;}"
               "QMenu::item{padding:8px 22px 8px 14px;border-radius:6px;min-width:110px;}"
               "QMenu::item:selected{background:palette(highlight);color:palette(highlighted-text);}"
               "QMenu::item:disabled{color:palette(placeholder-text);}"
               "QMenu::separator{height:1px;background:palette(mid);margin:6px 8px;}"
               "QToolTip{background:palette(window);color:palette(window-text);border:1px solid palette(mid);"
               "padding:5px 8px;}"
               "QMessageBox{background:palette(window);}"
               "QMessageBox QLabel{font-size:13px;}"
               "QMessageBox QPushButton{min-width:84px;}")
        .arg(kComboArrowWidth)
        .arg(kSpinButtonWidth);
}

// A thin modern chevron (the Windows default arrow is a heavy triangle), centred in `area`, in the text colour.
inline void paintChevron(QWidget* w, const QRectF& area, bool up) {
    QPainter p(w);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor color = w->palette().color(QPalette::WindowText);
    if (!w->isEnabled()) color.setAlpha(100);
    const qreal half = qMin(qMin(area.width(), area.height()) * 0.3, 4.5);
    const qreal tip = up ? -half * 0.5 : half * 0.5;  // the point of the chevron
    const QPointF c = area.center();
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    path.moveTo(c.x() - half, c.y() - tip);
    path.lineTo(c.x(), c.y() + tip);
    path.lineTo(c.x() + half, c.y() - tip);
    p.drawPath(path);
}

// A dropdown whose list pops up as a rounded card instead of the old square Windows list.
class ModernComboBox : public QComboBox {
public:
    explicit ModernComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setView(new QListView);
        view()->setStyleSheet(
            "QListView{background:palette(base);border:1px solid palette(mid);border-radius:10px;padding:5px;outline:0;}"
            "QListView::item{padding:7px 12px;border-radius:6px;min-height:22px;}"
            "QListView::item:hover,QListView::item:selected{background:palette(highlight);"
            "color:palette(highlighted-text);}");
        QWidget* popup = view()->window();  // the frame around the list: make it see-through so the corners are round
        popup->setWindowFlag(Qt::FramelessWindowHint, true);
        popup->setWindowFlag(Qt::NoDropShadowWindowHint, true);
        popup->setAttribute(Qt::WA_TranslucentBackground, true);
        popup->setStyleSheet("QFrame{background:transparent;border:none;}");
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QComboBox::paintEvent(e);
        paintChevron(this, QRectF(width() - kComboArrowWidth, 0, kComboArrowWidth, height()), false);
    }
};

// A number box with thin chevrons on its up / down buttons.
class ModernDoubleSpinBox : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void paintEvent(QPaintEvent* e) override {
        QDoubleSpinBox::paintEvent(e);
        const qreal half = height() / 2.0;
        paintChevron(this, QRectF(width() - kSpinButtonWidth, 0, kSpinButtonWidth, half), true);
        paintChevron(this, QRectF(width() - kSpinButtonWidth, half, kSpinButtonWidth, half), false);
    }
};

// A menu with rounded corners (call before exec()).
inline void roundMenu(QMenu& menu) {
    menu.setWindowFlag(Qt::FramelessWindowHint, true);
    menu.setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu.setAttribute(Qt::WA_TranslucentBackground, true);
}
