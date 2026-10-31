#include "InfoPanel.h"

#include "Translate.h"

#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
// A text view that always keeps the mouse wheel. A normal scroll area hands the wheel on to the widget behind it once it
// is scrolled to its top or bottom, and there that would zoom the 3D view.
class WheelKeepingBrowser : public QTextBrowser {
protected:
    void wheelEvent(QWheelEvent* e) override {
        QTextBrowser::wheelEvent(e);
        e->accept();
    }
};
}  // namespace

void InfoPanel::wheelEvent(QWheelEvent* e) { e->accept(); }

InfoPanel::InfoPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("infoPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("#infoPanel{background:palette(window);border-left:1px solid palette(mid);}");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    text_ = new WheelKeepingBrowser;
    text_->setFrameShape(QFrame::NoFrame);
    text_->setOpenLinks(false);
    // Qt's own right-click menu (Copy, Select All) is English only.
    text_->setContextMenuPolicy(Qt::NoContextMenu);
    // Never takes the keyboard: the window's keys (arrows = next file, Ctrl+C = snapshot, ...) keep working after a click
    // in the panel. Section headings still fold with the mouse.
    text_->setFocusPolicy(Qt::NoFocus);
    // Without the keyboard Ctrl+C cannot copy text, so text is not selectable either (it would look copyable).
    text_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    text_->setStyleSheet("QTextBrowser{background:transparent;padding:6px 10px 14px 12px;}");
    layout->addWidget(text_);
    connect(text_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        // Headings are links "fold:<title>": a click folds or unfolds that section.
        const QString title = QUrl::fromPercentEncoding(url.toString().mid(5).toUtf8());
        if (!folded_.erase(title)) folded_.insert(title);
        refresh();
    });
}

void InfoPanel::setSections(const std::vector<InfoSection>& sections, bool reload) {
    // The same project read again (F5 or automatic reload): keep the opened/closed sections and the scroll position.
    bool sameTitles = reload && sections.size() == sections_.size();
    for (size_t i = 0; sameTitles && i < sections.size(); ++i) sameTitles = sections[i].title == sections_[i].title;
    sections_ = sections;
    if (sameTitles && !sections_.empty()) {
        refresh();
        return;
    }
    folded_.clear();
    for (const InfoSection& s : sections_)
        if (s.title == tr8("Objects")) folded_.insert(QString::fromStdString(s.title));  // can be a long list
    text_->setHtml(html());
    text_->verticalScrollBar()->setValue(0);
}

// Draws the panel again (new content, or the light/dark theme changed) and stays where the reader was.
void InfoPanel::refresh() {
    const int scroll = text_->verticalScrollBar()->value();
    text_->setHtml(html());
    text_->verticalScrollBar()->setValue(scroll);
}

QString InfoPanel::html() const {
    const QColor fg = palette().color(QPalette::WindowText), bg = palette().color(QPalette::Window);
    auto mix = [](const QColor& a, const QColor& b, double t) {  // t of a over b
        return QColor(int(a.red() * t + b.red() * (1 - t)), int(a.green() * t + b.green() * (1 - t)),
                      int(a.blue() * t + b.blue() * (1 - t)));
    };
    const QString text = fg.name(), dim = mix(fg, bg, 0.62).name(), rule = mix(fg, bg, 0.14).name();

    QString out = QString("<body style=\"color:%1;font-size:13px;\">").arg(text);
    for (const InfoSection& s : sections_) {
        const QString title = QString::fromStdString(s.title);
        const bool folded = folded_.count(title) > 0;
        out += QString("<p style=\"margin:14px 0 4px 0;\"><a href=\"fold:%1\" style=\"text-decoration:none;color:%2;\">"
                       "<b style=\"font-size:15px;\">%3&nbsp;%4</b></a></p>")
                   .arg(QString::fromLatin1(QUrl::toPercentEncoding(title)), text, folded ? "&#9656;" : "&#9662;",
                        title.toHtmlEscaped());
        if (folded) continue;
        out += "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" style=\"margin-bottom:2px;\">";
        for (const InfoRow& r : s.rows) {
            QStringList lines = QString::fromStdString(r.value).split('\n');
            QString value;
            for (int i = 0; i < lines.size(); ++i) {
                QString line = lines[i].toHtmlEscaped();
                if (i == 0 && !r.swatch.empty()) {
                    // The colour comes from the file: only a real "#RRGGBB" colour may go into the page.
                    static const QRegularExpression colour("^#[0-9A-Fa-f]{6}");
                    const QString swatch = QString::fromStdString(r.swatch).trimmed();
                    if (colour.match(swatch).hasMatch())
                        line = QString("<span style=\"color:%1;\">&#9632;</span>&nbsp;").arg(swatch.left(7)) + line;
                }
                if (i > 0) line = QString("<br><span style=\"color:%1;\">%2</span>").arg(dim, line);
                value += line;
            }
            out += QString("<tr><td width=\"36%\" valign=\"top\" style=\"color:%1;border-top:1px solid %2;\">%3</td>"
                           "<td valign=\"top\" style=\"border-top:1px solid %2;\">%4</td></tr>")
                       .arg(dim, rule, QString::fromStdString(r.label).toHtmlEscaped(), value);
        }
        out += "</table>";
    }
    out += "</body>";
    return out;
}
