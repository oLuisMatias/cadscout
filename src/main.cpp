#include <QApplication>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSettings>
#include <QSurfaceFormat>

#include "MainWindow.h"

// True when this PC can give us an OpenGL 3.3 context, which the 3D view needs (old graphics cards and some
// virtual machines cannot).
static bool openGlAvailable() {
    QOpenGLContext context;
    context.setFormat(QSurfaceFormat::defaultFormat());
    if (!context.create()) return false;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
    const QSurfaceFormat got = context.format();
    return got.majorVersion() > 3 || (got.majorVersion() == 3 && got.minorVersion() >= 3);
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setOrganizationName("CADscout");
#ifdef CADSCOUT_LITE
    QApplication::setApplicationName("CADscout Lite");
    QApplication::setApplicationDisplayName("CADscout Lite");
#else
    QApplication::setApplicationName("CADscout");
    QApplication::setApplicationDisplayName("CADscout Full");
#endif

    // The app used to be called "STEP Viewer". Copy settings saved under that name (mouse controls, plate,
    // mesh quality, last folder) once, the first time the new name runs.
    {
        QSettings current;
        if (current.allKeys().isEmpty()) {
#ifdef CADSCOUT_LITE
            QSettings old("StepViewer", "STEP Viewer Light");
#else
            QSettings old("StepViewer", "STEP Viewer");
#endif
            for (const QString& key : old.allKeys()) current.setValue(key, old.value(key));
        }
    }

    if (!openGlAvailable()) {
        QMessageBox::critical(nullptr, QApplication::applicationDisplayName(),
                              "The 3D view needs a graphics card or driver that supports OpenGL 3.3, and this computer "
                              "does not provide it.\n\nPlease update your graphics driver and try again. In a virtual "
                              "machine, turn on 3D acceleration.");
        return 1;
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
