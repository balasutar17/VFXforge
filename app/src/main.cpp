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
#include <QVariantMap>
#include <QtQml>

#include "AppController.h"
#include "PreviewItems.h"
#include "ReferenceController.h"
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
// app, let it run, take pictures of the window in three states (the starter
// effect, a library preset, and the library itself), and fail loudly if the
// window did not load cleanly or nothing was drawn.
struct SelfTest {
    bool enabled = false;
    QString picture;
    QStringList problems;
    QString report;
    int result = 0;
};

// Saves a picture of the window beside the first one, with a suffix, and
// returns how many clearly lit pixels the middle of it has.
int capture(QQuickWindow* window, SelfTest& test, const QString& suffix, const QString& what) {
    QTextStream out(&test.report);
    const QImage image = window->grabWindow();
    if (image.isNull()) {
        out << "FAIL: a picture of " << what << " could not be taken\n";
        test.result = 1;
        return 0;
    }
    int lit = 0;
    const int x0 = image.width() * 3 / 10, x1 = image.width() * 7 / 10;
    const int y0 = image.height() * 2 / 10, y1 = image.height() * 7 / 10;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qMax(qRed(pixel), qMax(qGreen(pixel), qBlue(pixel))) > 140) {
                ++lit;
            }
        }
    }
    QFileInfo first(test.picture);
    const QString path = suffix.isEmpty()
        ? test.picture
        : first.absolutePath() + QLatin1Char('/') + first.completeBaseName() + suffix + QStringLiteral(".png");
    QDir().mkpath(first.absolutePath());
    if (!image.save(path)) {
        out << "FAIL: the picture of " << what << " could not be saved to " << path << "\n";
        test.result = 1;
    }
    out << "picture of " << what << ": " << image.width() << "x" << image.height() << ", " << lit
        << " lit pixels in the middle\n";
    return lit;
}

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
    qmlRegisterType<PresetPreview>("VfxForge", 1, 0, "PresetPreview");
    qmlRegisterType<ShapeIcon>("VfxForge", 1, 0, "ShapeIcon");
    qmlRegisterUncreatableType<AppController>("VfxForge", 1, 0, "AppController",
                                              QStringLiteral("The app provides this."));
    qmlRegisterType<ReferenceView>("VfxForge", 1, 0, "ReferenceView");
    qmlRegisterUncreatableType<ReferenceController>("VfxForge", 1, 0, "ReferenceController",
                                                    QStringLiteral("The app provides this."));

    AppController controller;
    ReferenceController referenceController(&controller);
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
    engine.rootContext()->setContextProperty(QStringLiteral("reference"), &referenceController);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    // A fresh start opens on the library. Opening a file goes straight to it.
    engine.rootContext()->setContextProperty(QStringLiteral("showLibraryAtStart"),
                                             !selfTest.enabled && fileToOpen.isEmpty());
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
        if (!window) {
            std::fprintf(stderr, "SELF-TEST FAILED: the main window is not a window\n");
            return 2;
        }
        const bool software =
            window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;

        auto finish = [&]() {
            QTextStream out(&selfTest.report);
            for (const QString& problem : selfTest.problems) {
                out << "FAIL: " << problem << "\n";
                selfTest.result = 1;
            }
            out << (selfTest.result == 0 ? "SELF-TEST PASS\n" : "SELF-TEST FAILED\n");
            out.flush();
            // A windowed program on Windows has no console to print to, so
            // the report also goes into a file beside the picture.
            std::fputs(qPrintable(selfTest.report), stdout);
            std::fflush(stdout);
            QFile file(selfTest.picture + QStringLiteral(".txt"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(selfTest.report.toUtf8());
            }
            QCoreApplication::exit(selfTest.result);
        };

        // The exports, into a scratch folder, so a build proves they work.
        auto exports = [&]() {
            QTextStream out(&selfTest.report);
            const QString scratch = QDir::tempPath() + QStringLiteral("/vfxforge-selftest");
            QDir(scratch).removeRecursively();
            QDir().mkpath(scratch + QStringLiteral("/Unity Project/Assets"));
            QDir().mkpath(scratch + QStringLiteral("/Unity Project/ProjectSettings"));
            const bool unity = controller.exportToUnityPath(scratch + QStringLiteral("/Unity Project/Assets"));
            const bool prefabFile = QFile::exists(scratch + QStringLiteral("/Unity Project/Assets/VFXForge/Effects/") +
                                                  controller.effectName() + QStringLiteral(".vfxforge"));
            const bool package = controller.exportUnityPackage(
                QUrl::fromLocalFile(scratch + QStringLiteral("/effect.unitypackage")));
            QVariantMap view;
            view.insert(QStringLiteral("unitsHigh"), 6.4);
            const bool frames = controller.exportFrames(QUrl::fromLocalFile(scratch), 128, true, true, view);
            const bool sheet = QFile::exists(scratch + QLatin1Char('/') + controller.effectName() +
                                             QStringLiteral(" sheet.png"));
            const bool mixdown = QFile::exists(scratch + QLatin1Char('/') + controller.effectName() +
                                               QStringLiteral(" sound.wav"));
            const bool unitySound = QDir(scratch + QStringLiteral("/Unity Project/Assets/VFXForge/Sounds"))
                                        .entryList(QDir::Files).size() > 0;
            out << "export the sound (mixdown and to Unity): " << (mixdown && unitySound ? "yes" : "NO") << "\n";
            if (!mixdown || !unitySound) {
                out << "FAIL: the sound was not exported\n";
                selfTest.result = 1;
            }
            out << "export to a Unity project: " << (unity && prefabFile ? "yes" : "NO") << "\n";
            out << "export a .unitypackage: " << (package ? "yes" : "NO") << "\n";
            out << "export frames and a sheet: " << (frames && sheet ? "yes" : "NO") << "\n";
            if (!(unity && prefabFile && package && frames && sheet)) {
                out << "FAIL: an export did not work: " << controller.message() << "\n";
                selfTest.result = 1;
            }
            QDir(scratch).removeRecursively();
        };

        // Last: the library, with every card playing.
        auto third = [&, window, software, finish]() {
            const int lit = capture(window, selfTest, QStringLiteral("-library"), QStringLiteral("the library"));
            if (!software && lit < 500) {
                QTextStream(&selfTest.report) << "FAIL: the library shows no previews\n";
                selfTest.result = 1;
            }
            finish();
        };

        // Reference to VFX, start to finish: read a picture and a clip,
        // build an effect from each, and take a picture of the workspace.
        auto referenceTest = [&, window]() {
            QTextStream out(&selfTest.report);
            ReferenceController& ref = referenceController;
            ref.setOpen(true);
            out << "reference formats: " << ref.formats() << "\n";

            const bool loaded = ref.loadPath(QStringLiteral(":/samples/reference-flash.png"));
            ref.waitUntilIdle(30000);
            out << "reference picture: " << ref.report() << "\n";
            if (!loaded || !ref.analysed() || ref.findings().isEmpty()) {
                out << "FAIL: the sample reference picture was not read and studied\n";
                selfTest.result = 1;
            }
            ref.setCutouts(true);
            ref.build();
            ref.waitUntilIdle(120000);
            out << "built from the picture: " << ref.report() << "\n";
            const int layers = static_cast<int>(controller.session().effect().layers.size());
            const double alike = ref.similarity().value(QStringLiteral("overall")).toDouble();
            if (!ref.built() || layers < 1 || alike < 0.3) {
                out << "FAIL: no effect was built from the sample reference picture\n";
                selfTest.result = 1;
            }
            if (!ref.keptWithProject() || !controller.session().keptReference().found) {
                out << "FAIL: the reference was not kept with the project\n";
                selfTest.result = 1;
            }
            const bool undone = controller.session().undo().ok() && controller.session().redo().ok();
            out << "rebuild is one undo step: " << (undone ? "yes" : "NO") << "\n";
            // Each view of the comparison draws.
            for (const char* kind : {"full", "reference", "made", "overlay", "difference"}) {
                if (ref.picture(QString::fromLatin1(kind)).isNull()) {
                    out << "FAIL: the \"" << kind << "\" comparison picture is empty\n";
                    selfTest.result = 1;
                }
            }
            ref.refine(QStringLiteral("more-sparks"));
            ref.waitUntilIdle(60000);
            QCoreApplication::processEvents();
            capture(window, selfTest, QStringLiteral("-reference"), QStringLiteral("the reference workspace"));

            ref.setCutouts(false);
            const bool clip = ref.loadPath(QStringLiteral(":/samples/reference-burst.gif"));
            ref.waitUntilIdle(60000);
            out << "reference GIF: " << ref.report() << "\n";
            if (!clip || !ref.moving() || ref.frameCount() < 8 || !ref.analysed()) {
                out << "FAIL: the sample reference GIF was not read as a clip\n";
                selfTest.result = 1;
            }
            ref.build();
            ref.waitUntilIdle(180000);
            out << "built from the GIF: " << ref.report() << "\n";
            if (!ref.built()) {
                out << "FAIL: no effect was built from the sample reference GIF\n";
                selfTest.result = 1;
            }
            ref.setTime(ref.length() * 0.3);
            QCoreApplication::processEvents();
            capture(window, selfTest, QStringLiteral("-reference-clip"), QStringLiteral("the reference workspace with a clip"));

            // Video goes through the computer's own media support, which a
            // build machine may lack. What happens is written down either way.
            for (const char* name : {"reference-burst.mp4", "reference-burst.webm"}) {
                const QString temp = QDir::tempPath() + QStringLiteral("/vfxforge-selftest-") + QString::fromLatin1(name);
                QFile::remove(temp);
                QFile::copy(QStringLiteral(":/samples/") + QString::fromLatin1(name), temp);
                QFile::setPermissions(temp, QFile::ReadOwner | QFile::WriteOwner);
                ref.loadPath(temp);
                ref.waitUntilIdle(30000);
                const bool read = ref.moving() && ref.frameCount() >= 8 && ref.name() == QString::fromLatin1(name);
                out << "video " << name << ": " << (read ? "read, " : "NOT READ, ") << ref.report() << "\n";
                QFile::remove(temp);
            }
            ref.cancel();
            ref.setOpen(false);
        };

        // Third: a layer drawing a painted sprite sheet, then the exports
        // of that effect, pictures and all.
        auto picture = [&, window, software, third, exports, referenceTest]() {
            QTextStream out(&selfTest.report);
            out << "picture effect: " << controller.effectName() << ", " << controller.particleCount()
                << " particles, " << controller.session().images().size() << " picture(s)\n";
            if (controller.session().images().size() != 1) {
                out << "FAIL: the sample sprite sheet did not load\n";
                selfTest.result = 1;
            }
            const int lit = capture(window, selfTest, QStringLiteral("-picture"), QStringLiteral("a painted sprite sheet"));
            if (!software && lit < 500) {
                out << "FAIL: the painted sprite sheet shows nothing\n";
                selfTest.result = 1;
            }
            exports();
            referenceTest();
            controller.setLibraryOpen(true);
            QTimer::singleShot(2500, &application, third);
        };

        // Second: a toon preset, which uses the hard-edged shapes.
        auto second = [&, window, software, picture]() {
            QTextStream out(&selfTest.report);
            out << "preset: " << controller.effectName() << ", " << controller.particleCount()
                << " particles\n";
            const int lit = capture(window, selfTest, QStringLiteral("-preset"), QStringLiteral("a preset"));
            if (!software && lit < 500) {
                out << "FAIL: the preset shows nothing\n";
                selfTest.result = 1;
            }
            const bool sheet = QFile::exists(QStringLiteral(":/samples/toon-flame-4x2.png")) &&
                               QFile::exists(QStringLiteral(":/shaders/image.frag.qsb"));
            out << "sample sheet and picture shader present: " << (sheet ? "yes" : "NO") << "\n";
            if (!sheet || !controller.openPreset(QStringLiteral("toon-fire"))) {
                out << "FAIL: could not set up the picture test\n";
                selfTest.result = 1;
            }
            controller.selectLayer(0);
            if (!controller.useSamplePicture()) {
                out << "FAIL: the sample sprite sheet could not be used: " << controller.message() << "\n";
                selfTest.result = 1;
            }
            out << "sound output: " << (controller.soundAvailable() ? "yes" : "none on this machine") << "\n";
            out << "library sounds: " << controller.librarySounds().size() << "\n";
            if (!controller.useLibrarySound(QStringLiteral("fire-loop"))) {
                out << "FAIL: a library sound could not be added: " << controller.message() << "\n";
                selfTest.result = 1;
            }
            QTimer::singleShot(1500, &application, picture);
        };

        // First: the starter effect.
        QTimer::singleShot(4000, &application, [&, window, software, second]() {
            QTextStream out(&selfTest.report);
            out << "SELF-TEST VFX Forge " << VFX_APP_VERSION << "\n";
            out << "graphics: " << static_cast<int>(window->rendererInterface()->graphicsApi())
                << " (QSGRendererInterface::GraphicsApi)\n";
            const bool shaders = QFile::exists(QStringLiteral(":/shaders/sprite.vert.qsb")) &&
                                 QFile::exists(QStringLiteral(":/shaders/sprite.frag.qsb"));
            out << "shaders present: " << (shaders ? "yes" : "NO") << "\n";
            out << "particles alive: " << controller.particleCount() << "\n";
            out << "frame: " << controller.frame() << "  time: " << controller.time() << "\n";
            out << "frames per second: " << controller.framesPerSecond() << "\n";
            out << "presets in the library: " << controller.presets().size() << "\n";
            if (!shaders) {
                out << "FAIL: the particle shaders are missing from the app\n";
                selfTest.result = 1;
            }
            if (controller.particleCount() <= 0) {
                out << "FAIL: no particles after four seconds\n";
                selfTest.result = 1;
            }
            if (controller.time() <= 0.0) {
                out << "FAIL: time did not move, so the window is not being redrawn\n";
                selfTest.result = 1;
            }
            if (software) {
                out << "note: drawing without a graphics card, which cannot show particles, "
                       "so the pictures are not checked\n";
            }
            const int lit = capture(window, selfTest, QString(), QStringLiteral("the starter effect"));
            if (!software && lit < 50) {
                out << "FAIL: the viewport shows no particles\n";
                selfTest.result = 1;
            }
            if (!controller.openPreset(QStringLiteral("rainbow-burst"))) {
                out << "FAIL: a library preset would not open\n";
                selfTest.result = 1;
            }
            QTimer::singleShot(350, &application, second);
        });
    }

    return application.exec();
}
