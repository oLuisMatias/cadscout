#pragma once
#include <QFrame>
#include <set>
#include <vector>
#include "InfoSection.h"

class QTextBrowser;

// Side panel with everything a file says besides its geometry (a 3MF project: file, printer, print profile, filaments, plates,
// objects, slicing result), in sections that fold and unfold when their heading is clicked.
class InfoPanel : public QFrame {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget* parent = nullptr);
    void setSections(const std::vector<InfoSection>& sections, bool reload = false);  // reload: keep folds and scroll
    bool hasContent() const { return !sections_.empty(); }
    void refresh();  // draw again (the colours follow the light/dark theme)

protected:
    void wheelEvent(QWheelEvent* e) override;  // never passes the wheel on to the 3D view behind the panel

private:
    QString html() const;

    QTextBrowser* text_;
    std::vector<InfoSection> sections_;
    std::set<QString> folded_;
};
