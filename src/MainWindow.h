#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <utility>
#include <vector>
#include "StepLoader.h"

class InfoPanel;
class Viewport;
class ViewCube;
class QHBoxLayout;
class QIcon;
class QLabel;
class QMenu;
class QPushButton;
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
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;
    bool event(QEvent* e) override;  // re-draws the icons when the display scale changes
#ifdef CADSCOUT_LITE
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
    // Both apps draw their own top bar: no Windows title bar, so these replace its buttons and its resizing.
    void buildWindowControls(QHBoxLayout* row, const QString& btnStyle);
    void updateWindowControls();
    void layoutResizeGrips();
    void toggleMaximized();
    void updateBrand();  // "CADScout" (+ "Lite") at the left of the top bar, coloured for the light or dark theme
#ifdef CADSCOUT_LITE
    QString sizeText(float x, float y, float z) const;  // "W × D × H mm" (or in) in the status bar
    QString partSizeText(size_t part) const;            // size of one part, empty when it has no triangles
    void updateOverallInfo();                            // info_ = infoBase_ + the size of the whole model (single plate only)
    void copyImage();                                    // Ctrl+C: the view as a picture with a transparent background
    void flashStatus(const QString& text);               // short message in the status bar, then the old text returns
    void reloadFile();                                   // F5: read the open file again, keeping the camera
    void scheduleReload();                               // the file changed on disk: reload once it has stopped changing
    void checkReload();
    void showInFolder();
    void showBarMenu(const QPoint& globalPos);           // right-click on the top bar
    void iconMenu(QMenu& menu);                          // look of the menus with icons
    QIcon menuIcon(const char* name) const;              // 16 px icon in the text colour
    void newWindow();                                    // Ctrl+N: choose a file and open it in a second window
    void updateLoadCard();                               // progress card while a file loads (timer)
    void cancelLoad();                                   // Cancel button / Esc while loading
    void browseFolder(int step);  // opens the next (+1) or previous (-1) model file in the current file's folder
    void rememberRecent(const QString& path);
    void showRecentMenu();
    void openRecent(const QString& path);
    QString chooseZipEntry(const QString& zipPath);  // model inside a zip to open; empty when none or cancelled
    QString zipEntry_;          // model inside currentFile_ when it is a zip
    QString previousZipEntry_;  // the same for previousFile_
    QString sourceFile_;        // the file the shown model was read from (for a zip: the unpacked copy)
    QString pendingZip_, pendingEntry_;  // a zip model chosen while another load ran, opened when it ends
#endif
    QString shownName(const QString& path) const;  // file name for the top bar ("parts.zip › bracket.stp" for a zip)
    void layoutOverlays();
    void setStatus(const QString& text);  // text of the status bar at the bottom of the window
    void refreshIcons();
    void setBtnIcon(QToolButton* b, const QString& name);
    void buildTree();
#ifndef CADSCOUT_LITE
    void updateTreeAvailability();  // hides the tree and disables its button for one-object formats
    bool treeWasOpen_ = true;       // the user's tree choice, restored when a multi-object file is opened
#endif
    // source: the file that was read (for a zip: the unpacked copy); skipped: zip side files that could not be unpacked.
    void onLoaded(std::shared_ptr<Model> model, const QString& path, const QString& error, bool keepView,
                  const QString& source, int skipped);
    QString askForModel();  // the Open dialog; empty when cancelled

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
    InfoPanel* infoPanel_ = nullptr;  // everything the file says besides geometry (3MF projects), in sections
    QToolButton* infoTab_ = nullptr;  // small tab at the top right that opens and closes the panel
    QLabel* status_;
    QString statusText_;  // full text; the label shows a shortened copy when the window is narrow
    QLabel* hint_;
    bool loading_ = false;
    QPointer<QThread> loader_;  // captures `this`, so it must finish before the window is destroyed
    std::shared_ptr<LoadProgress> progress_;  // stage, fraction and cancel flag of the running load
    QString screenshotPath_;
    bool keepViewNext_ = false;  // the next load is a reload of the open file: keep the camera
    QString info_;
    QString currentFile_;
    QString previousFile_;  // the file shown before a load started: back to it when the load is cancelled or fails
    QString browseFrom_;    // a file that failed to open: the arrow keys continue from it (empty otherwise)
    QLabel* brandLabel_ = nullptr;  // app name at the left of the top bar
    QWidget* nameBox_ = nullptr;    // holds the file name (and in Lite the arrow with the file menu)
    QLabel* titleLabel_ = nullptr;  // name of the open file, in the middle of the top bar
    QToolButton* minBtn_ = nullptr;
    QToolButton* maxBtn_ = nullptr;
    QToolButton* closeBtn_ = nullptr;
    std::vector<QWidget*> grips_;
    bool closeHover_ = false;
    bool dragPending_ = false;
#ifdef CADSCOUT_LITE
    QToolButton* perspBtn_ = nullptr;
    QToolButton* recentBtn_ = nullptr;  // small arrow next to Open: list of recent files
    bool arrowKeys_ = true;             // Left/Right open the previous/next file (Settings > General)
    QString infoBase_;                  // status text without the size of the whole model
    QString sizeUnit_;                  // " mm" for formats that define the unit, otherwise empty
    bool inches_ = false;               // Settings > General: show sizes in inches (only where the unit is mm)
    bool autoReload_ = true;            // Settings > General: reload when the file changes on disk
    QToolButton* copyBtn_ = nullptr;
    void showPartMenu(int part, const QPoint& pos);  // right click in the view: hide a part, show all
    QToolButton* nameArrow_ = nullptr;  // arrow next to the file name: the same menu as a right click on the bar
    QWidget* loadCard_ = nullptr;       // "Repairing the parts... 45 %  [bar]  Cancel" while a big file loads
    QLabel* loadText_ = nullptr;
    QWidget* loadBar_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QTimer progressTimer_;
    QElapsedTimer loadClock_;
    static void clearStuckHover(QToolButton* b);
    QFileSystemWatcher watcher_;
    QTimer reloadTimer_;
    qint64 loadedSize_ = -1, seenSize_ = -1;  // size and time of the file as loaded / as last seen while waiting
    QDateTime loadedTime_, seenTime_;
    qint64 startSize_ = -1;  // size and time of the file when the running load started (what it reads)
    QDateTime startTime_;
    int missingChecks_ = 0;
#endif
    QPoint clickPoint_;
    bool hasClick_ = false;
};
