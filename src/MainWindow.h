#pragma once
#include <QMainWindow>
#include <QPointer>
#include <memory>
#include <utility>
#include <vector>
#include "StepLoader.h"

class Viewport;
class ViewCube;
class QHBoxLayout;
class QLabel;
class QThread;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    void openFile(const QString& path);
    void showTree();
    void setScreenshotPath(const QString& p) { screenshotPath_ = p; }
    void setClickPoint(const QPoint& p) { clickPoint_ = p; hasClick_ = true; }  // debug: select before screenshot

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
#ifdef CADSCOUT_LITE
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;  // remembers the window size and position
#endif

private slots:
    void openDialog();
    void openSettings();
    void openAbout();
#ifdef CADSCOUT_LITE
    void openInFull();
#endif

private:
#ifdef CADSCOUT_LITE
    void buildWindowControls(QHBoxLayout* row, const QString& btnStyle);
    void updateWindowControls();
    void layoutResizeGrips();
    void toggleMaximized();
    void browseFolder(int step);  // opens the next (+1) or previous (-1) model file in the current file's folder
    void rememberRecent(const QString& path);
    void showRecentMenu();
    void openRecent(const QString& path);
#endif
    void layoutOverlays();
    void setStatus(const QString& text);  // text of the status bar at the bottom of the window
    void refreshIcons();
    void setBtnIcon(QToolButton* b, const QString& name);
    void buildTree();
#ifndef CADSCOUT_LITE
    void updateTreeAvailability();  // hides the tree and disables its button for one-object formats
    bool treeWasOpen_ = true;       // the user's tree choice, restored when a multi-object file is opened
#endif
    void onLoaded(std::shared_ptr<Model> model, const QString& path, const QString& error);

    std::vector<std::pair<QToolButton*, QString>> iconButtons_;
    bool dark_ = false;
    Viewport* viewport_;
    QTreeWidget* tree_;
    std::vector<QTreeWidgetItem*> partItems_;
    std::shared_ptr<Model> model_;
    QWidget* toolbar_;
    QToolButton* openBtn_;
    QToolButton* treeBtn_ = nullptr;
    QWidget* homePanel_;
    ViewCube* cube_;
    QLabel* popup_ = nullptr;  // small card shown next to a clicked plate or part (Bambu/Orca files)
    QLabel* status_;
    QString statusText_;  // full text; the label shows a shortened copy when the window is narrow
    QLabel* hint_;
    bool loading_ = false;
    QPointer<QThread> loader_;  // captures `this`, so it must finish before the window is destroyed
    QString screenshotPath_;
    QString info_;
    QString currentFile_;
#ifdef CADSCOUT_LITE
    QToolButton* minBtn_ = nullptr;
    QToolButton* maxBtn_ = nullptr;
    QToolButton* closeBtn_ = nullptr;
    QToolButton* perspBtn_ = nullptr;
    QToolButton* recentBtn_ = nullptr;  // small arrow next to Open: list of recent files
    bool arrowKeys_ = true;             // Left/Right open the previous/next file (Settings > General)
    std::vector<QWidget*> grips_;
    bool closeHover_ = false;
    bool dragPending_ = false;
#endif
    QPoint clickPoint_;
    bool hasClick_ = false;
};
