#include <QApplication>
#include <QSettings>
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
    QApplication::setOrganizationName("CADscout");
#ifdef CADSCOUT_LIGHT
    QApplication::setApplicationName("CADscout Light");
    QApplication::setApplicationDisplayName("CADscout Light");
#else
    QApplication::setApplicationName("CADscout");
    QApplication::setApplicationDisplayName("CADscout Full");
#endif

    // The app used to be called "STEP Viewer". Copy settings saved under that name (mouse controls, plate,
    // mesh quality, last folder) once, the first time the new name runs.
    {
        QSettings current;
        if (current.allKeys().isEmpty()) {
#ifdef CADSCOUT_LIGHT
            QSettings old("StepViewer", "STEP Viewer Light");
#else
            QSettings old("StepViewer", "STEP Viewer");
#endif
            for (const QString& key : old.allKeys()) current.setValue(key, old.value(key));
        }
    }

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
