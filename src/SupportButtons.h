#pragma once
#include <QWidget>

// The "Buy Me a Coffee" and "Ko-fi" buttons, shared by the Settings Support page and the About window.
// A click opens the donation page in the browser (addresses in Support.h).
class SupportButtons : public QWidget {
    Q_OBJECT
public:
    explicit SupportButtons(QWidget* parent = nullptr);
};
