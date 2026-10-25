#pragma once
#include <QDialog>

// "About CADscout": name, version, license line, credits and the third-party license texts.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
