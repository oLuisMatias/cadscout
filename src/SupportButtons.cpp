#include "SupportButtons.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>

#include "Icons.h"
#include "Support.h"

SupportButtons::SupportButtons(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    // Each button is named after the donation service (the Store asks that the payment provider is identified).
    auto add = [&](const QString& text, const char* url, const QString& background, const QString& hover,
                   const QColor& foreground) {
        auto* button = new QPushButton(text);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoDefault(false);  // Enter must not trigger a donation button in a dialog
        button->setIcon(tablerIcon("heart", foreground, foreground, 18, devicePixelRatioF()));
        button->setIconSize(QSize(18, 18));
        button->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:none;border-radius:8px;"
                                      "padding:10px 20px;font-size:13px;font-weight:bold;}"
                                      "QPushButton:hover{background:%3;}")
                                  .arg(background, foreground.name(), hover));
        connect(button, &QPushButton::clicked, this, [this, url, text] {
            if (!*url) {
                QMessageBox::information(this, text, "This donation page is not set up yet.");
                return;
            }
            QDesktopServices::openUrl(QUrl(QString::fromUtf8(url)));
        });
        row->addWidget(button);
    };
    add("Buy Me a Coffee", kBuyMeACoffeeUrl, "#FFDD00", "#FFE84D", QColor(0x1a, 0x1a, 0x1a));
    add("Ko-fi", kKofiUrl, "#FF5E5B", "#FF7A77", QColor(0xff, 0xff, 0xff));
    row->addStretch(1);
}
