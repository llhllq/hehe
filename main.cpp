#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setStereo(true);
    fmt.setSwapInterval(0);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("QStereoView"));
    app.setApplicationName(QStringLiteral("QStereoView"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    qsv::MainWindow window;
    window.show();
    return app.exec();
}
