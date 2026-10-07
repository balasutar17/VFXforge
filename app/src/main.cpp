// VFX Forge: the desktop app.
#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlPropertyMap>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QtQml>

#include "AppController.h"
#include "ViewportItem.h"

#ifndef VFX_APP_VERSION
#define VFX_APP_VERSION "0.0.0"
#endif

namespace {

// Colours and sizes for the whole window, in one place.
void fillTheme(QQmlPropertyMap& theme) {
    theme.insert(QStringLiteral("window"), QStringLiteral("#15161a"));
    theme.insert(QStringLiteral("panel"), QStringLiteral("#1d1f25"));
    theme.insert(QStringLiteral("raised"), QStringLiteral("#272a32"));
    theme.insert(QStringLiteral("hover"), QStringLiteral("#313540"));
    theme.insert(QStringLiteral("line"), QStringLiteral("#2c2f38"));
    theme.insert(QStringLiteral("field"), QStringLiteral("#121317"));
    theme.insert(QStringLiteral("viewport"), QStringLiteral("#0b0c0f"));
    theme.insert(QStringLiteral("text"), QStringLiteral("#e7e9ee"));
    theme.insert(QStringLiteral("dim"), QStringLiteral("#979daa"));
    theme.insert(QStringLiteral("faint"), QStringLiteral("#646a77"));
    theme.insert(QStringLiteral("accent"), QStringLiteral("#ff8f3d"));
    theme.insert(QStringLiteral("accentText"), QStringLiteral("#1b1105"));
    theme.insert(QStringLiteral("error"), QStringLiteral("#ff6b6b"));
    theme.insert(QStringLiteral("fontSize"), 13);
    theme.insert(QStringLiteral("smallFontSize"), 11);
}

// --self-test <picture.png>
//
// Used by the automatic builds, where nobody is watching: start the real
// app, let it run for a few seconds, save a picture of the window, and fail
// loudly if the window did not load cleanly or no particles appeared.
struct SelfTest {
    bool enabled = false;
    QString picture;
    QStringList problems;
};

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("VFX Forge"));
    QCoreApplication::setOrganizationName(QStringLiteral("VFX Forge"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VFX_APP_VERSION));

    SelfTest selfTest;
    QString fileToOpen;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == QStringLiteral("--self-test") && i + 1 < arguments.size()) {
            selfTest.enabled = true;
            selfTest.picture = arguments[++i];
        } else if (!arguments[i].startsWith(QLatin1Char('-'))) {
            fileToOpen = arguments[i];
        }
    }

    // One look on every platform, drawn by the app itself.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<ViewportItem>("VfxForge", 1, 0, "Viewport");
    qmlRegisterUncreatableType<AppController>("VfxForge", 1, 0, "AppController",
                                              QStringLiteral("The app provides this."));

    AppController controller;
    QQmlPropertyMap theme;
    fillTheme(theme);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&selfTest](const QList<QQmlError>& warnings) {
                         for (const QQmlError& warning : warnings) {
                             selfTest.problems.push_back(warning.toString());
                         }
                     });
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "VFX Forge could not build its window.\n");
        for (const QString& problem : selfTest.problems) {
            std::fprintf(stderr, "  %s\n", qPrintable(problem));
        }
        return 2;
    }

    if (!fileToOpen.isEmpty()) {
        controller.openPath(QFileInfo(fileToOpen).absoluteFilePath());
    }

    if (selfTest.enabled) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QTimer::singleShot(4000, &application, [&, window]() {
            int result = 0;
            QString report;
            QTextStream out(&report);
            out << "SELF-TEST VFX Forge " << VFX_APP_VERSION << "\n";
            if (window) {
                out << "graphics: " << static_cast<int>(window->rendererInterface()->graphicsApi())
                    << " (QSGRendererInterface::GraphicsApi)\n";
            }
            const bool shaders = QFile::exists(QStringLiteral(":/shaders/sprite.vert.qsb")) &&
                                 QFile::exists(QStringLiteral(":/shaders/sprite.frag.qsb"));
            out << "shaders present: " << (shaders ? "yes" : "NO") << "\n";
            out << "particles alive: " << controller.particleCount() << "\n";
            out << "frame: " << controller.frame() << "  time: " << controller.time() << "\n";
            out << "frames per second: " << controller.framesPerSecond() << "\n";

            if (!shaders) {
                out << "FAIL: the particle shaders are missing from the app\n";
                result = 1;
            }
            if (controller.particleCount() <= 0) {
                out << "FAIL: no particles after four seconds\n";
                result = 1;
            }
            if (controller.time() <= 0.0) {
                out << "FAIL: time did not move, so the window is not being redrawn\n";
                result = 1;
            }
            for (const QString& problem : selfTest.problems) {
                out << "FAIL: " << problem << "\n";
                result = 1;
            }
            if (window) {
                const QImage image = window->grabWindow();
                if (image.isNull()) {
                    out << "FAIL: a picture of the window could not be taken\n";
                    result = 1;
                } else {
                    // Count clearly lit, warm pixels in the middle of the
                    // window, where the viewport is: proof particles were drawn.
                    int lit = 0;
                    const int x0 = image.width() * 3 / 10, x1 = image.width() * 7 / 10;
                    const int y0 = image.height() * 2 / 10, y1 = image.height() * 7 / 10;
                    for (int y = y0; y < y1; ++y) {
                        for (int x = x0; x < x1; ++x) {
                            const QRgb pixel = image.pixel(x, y);
                            if (qRed(pixel) > 150 && qRed(pixel) > qBlue(pixel) + 40) {
                                ++lit;
                            }
                        }
                    }
                    out << "picture: " << image.width() << "x" << image.height() << ", " << lit
                        << " lit particle pixels\n";
                    QDir().mkpath(QFileInfo(selfTest.picture).absolutePath());
                    if (!image.save(selfTest.picture)) {
                        out << "FAIL: the picture could not be saved to " << selfTest.picture << "\n";
                        result = 1;
                    }
                    const bool software = window->rendererInterface()->graphicsApi() ==
                                          QSGRendererInterface::Software;
                    if (software) {
                        out << "note: drawing without a graphics card, which cannot show "
                               "particles, so the picture is not checked\n";
                    } else if (lit < 50) {
                        out << "FAIL: the viewport shows no particles\n";
                        result = 1;
                    }
                }
            } else {
                out << "FAIL: the main window is not a window\n";
                result = 1;
            }
            out << (result == 0 ? "SELF-TEST PASS\n" : "SELF-TEST FAILED\n");
            out.flush();

            // A windowed program on Windows has no console to print to, so
            // the report also goes into a file beside the picture.
            std::fputs(qPrintable(report), stdout);
            std::fflush(stdout);
            QFile file(selfTest.picture + QStringLiteral(".txt"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(report.toUtf8());
            }
            QCoreApplication::exit(result);
        });
    }

    return application.exec();
}
