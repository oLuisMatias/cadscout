#include <QApplication>
#include <QSurfaceFormat>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setOrganizationName("StepViewer");
#ifdef STEPVIEWER_LIGHT
    QApplication::setApplicationName("STEP Viewer Light");
    QApplication::setApplicationDisplayName("STEP Viewer Light");
#else
    QApplication::setApplicationName("STEP Viewer");
    QApplication::setApplicationDisplayName("STEP Viewer Full");
#endif

    MainWindow w;
    w.show();
    const QStringList args = QApplication::arguments();
    QString file;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size()) w.setScreenshotPath(args[++i]);
        else if (args[i] == "--tree") w.showTree();
        else if (args[i] == "--click" && i + 2 < args.size()) {
            const int x = args[i + 1].toInt(), y = args[i + 2].toInt();
            w.setClickPoint(QPoint(x, y));
            i += 2;
        }
        else file = args[i];
    }
    if (!file.isEmpty()) w.openFile(file);
    return app.exec();
}
