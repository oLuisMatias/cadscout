#include "SupportButtons.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSvgRenderer>
#include <QUrl>
#include <QVBoxLayout>

#include "Support.h"
#include "Translate.h"

namespace {
constexpr int kQrSize = 88;
constexpr int kButtonHeight = 38;  // both buttons get the same height
}

SupportButtons::SupportButtons(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    const qreal dpr = devicePixelRatioF();

    // A picture button that opens a donation page. The buttons are the services' own graphics, so the provider is named
    // on them (the Store asks for that). While an address is empty the click says "not set up yet".
    auto makeButton = [this](const QPixmap& picture, const QString& name, const char* url) {
        auto* button = new QPushButton;
        const QSize size = picture.deviceIndependentSize().toSize();
        button->setIcon(QIcon(picture));
        button->setIconSize(size);
        button->setFixedSize(size);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoDefault(false);  // Enter must not trigger a donation button in a dialog
        button->setToolTip(name);
        button->setStyleSheet("QPushButton{border:none;background:transparent;padding:0;}"
                              "QPushButton:hover{background:transparent;}");
        connect(button, &QPushButton::clicked, this, [this, name, url] {
            if (!*url) {
                QMessageBox::information(this, name, tx("This donation page is not set up yet."));
                return;
            }
            QDesktopServices::openUrl(QUrl(QString::fromUtf8(url)));
        });
        return button;
    };
    // One card per service: QR code on the left, the button and a hint on the right. Both cards are equally wide.
    auto addCard = [&](const QPixmap& buttonPicture, const QString& name, const char* url, const char* qrResource) {
        auto* card = new QFrame;
        card->setObjectName("supportCard");
        card->setStyleSheet("QFrame#supportCard{border:1px solid palette(mid);border-radius:10px;}"
                            "QFrame#supportCard QLabel{border:none;}");
        auto* inner = new QHBoxLayout(card);
        inner->setContentsMargins(12, 12, 12, 12);
        inner->setSpacing(14);

        auto* qr = new QLabel;
        qr->setPixmap(QPixmap(qrResource));
        qr->setFixedSize(kQrSize, kQrSize);
        qr->setScaledContents(true);
        qr->setToolTip(tx("Scan with your phone to open the %1 page").arg(name));
        inner->addWidget(qr);

        auto* side = new QVBoxLayout;
        side->setSpacing(8);
        side->addStretch(1);
        side->addWidget(makeButton(buttonPicture, name, url), 0, Qt::AlignLeft);
        auto* hint = new QLabel(tx("Click, or scan the code with your phone."));
        hint->setWordWrap(true);
        hint->setStyleSheet("QLabel{color:palette(placeholder-text);font-size:11px;}");
        side->addWidget(hint);
        side->addStretch(1);
        inner->addLayout(side, 1);
        row->addWidget(card, 1);
    };

    // Buy Me a Coffee: vector graphic (resources/logo/button-buymeacoffee.svg, 545 x 153).
    QPixmap bmc;
    {
        const QSize size(kButtonHeight * 545 / 153, kButtonHeight);
        QSvgRenderer renderer(QString(":/logo/button-buymeacoffee.svg"));
        bmc = QPixmap(size * dpr);
        bmc.setDevicePixelRatio(dpr);
        bmc.fill(Qt::transparent);
        QPainter p(&bmc);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    }
    // Ko-fi: bitmap with rounded corners (the corners are cut here because the BMP has no transparency).
    QPixmap kofi;
    {
        const QSize size(kButtonHeight * 980 / 198, kButtonHeight);
        QPixmap source(":/logo/button-kofi.bmp");
        kofi = QPixmap(size * dpr);
        kofi.setDevicePixelRatio(dpr);
        kofi.fill(Qt::transparent);
        QPainter p(&kofi);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), size.height() * 0.27, size.height() * 0.27);
        p.setClipPath(clip);
        p.drawPixmap(QRectF(QPointF(0, 0), QSizeF(size)), source, source.rect());
    }
    // (The QR pictures are BMP files because this Qt build cannot read PNG or WebP.)
    addCard(bmc, "Buy Me a Coffee", kBuyMeACoffeeUrl, ":/logo/qr-buymeacoffee.bmp");
    addCard(kofi, "Ko-fi", kKofiUrl, ":/logo/qr-kofi.bmp");
}
