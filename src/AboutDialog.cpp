#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "SupportButtons.h"

#ifndef CADSCOUT_VERSION
#define CADSCOUT_VERSION "dev"
#endif

namespace {
QString readResource(const char* path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
#ifdef CADSCOUT_LITE
    const QString appName = "CADscout Lite";
#else
    const QString appName = "CADscout Full";
#endif
    setWindowTitle("About CADscout");
    resize(700, 580);
    setStyleSheet(
        "QDialog{background:palette(window);}"
        "QLabel{font-size:13px;}"
        "QPushButton{padding:7px 22px;}");  // buttons and scroll bars: app-wide look in ModernStyle.h

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 18);
    root->setSpacing(10);

    auto* title = new QLabel(appName);
    title->setStyleSheet("QLabel{font-size:26px;font-weight:bold;}");
    root->addWidget(title);

    auto* version = new QLabel(QString("Version %1  ·  CAD and 3D print file viewer").arg(CADSCOUT_VERSION));
    version->setStyleSheet("QLabel{color:palette(placeholder-text);}");
    root->addWidget(version);
    root->addSpacing(8);

    auto* license = new QLabel(
        "Free software under the MIT license. Copyright (c) 2026 CADscout contributors.\n"
        "The license text is in the file LICENSE next to the program.");
    license->setWordWrap(true);
    root->addWidget(license);

    auto* support = new QLabel("Support CADscout (it is free; nothing is unlocked in return):");
    support->setWordWrap(true);
    root->addSpacing(4);
    root->addWidget(support);
    root->addWidget(new SupportButtons);

    auto* credits = new QLabel(
        "CADscout is built with Qt (LGPL v3), Open CASCADE Technology (LGPL 2.1 with exception) and other "
        "open-source libraries. Their licenses:");
    credits->setWordWrap(true);
    root->addSpacing(4);
    root->addWidget(credits);

    auto* notices = new QPlainTextEdit;
    notices->setReadOnly(true);
    notices->setLineWrapMode(QPlainTextEdit::NoWrap);
    notices->setStyleSheet("QPlainTextEdit{border:1px solid palette(mid);border-radius:6px;background:palette(base);"
                           "padding:6px;font-family:Consolas;font-size:12px;}");
    notices->setPlainText(readResource(":/legal/THIRD-PARTY-NOTICES.txt"));
    root->addWidget(notices, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    box->button(QDialogButtonBox::Close)->setDefault(true);
    root->addWidget(box);
}
