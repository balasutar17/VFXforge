// VFX Forge: the desktop app.
#include <cstdio>
#include <variant>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
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

#include <algorithm>
#include <cmath>
#include <QUrl>
#include <QVariantMap>
#include <QtQml>

#include "vfx/Serialize.h"
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
            // The 3D cards, each playing.
            if (QObject* library = window->findChild<QObject*>(QStringLiteral("library"))) {
                library->setProperty("category", QStringLiteral("3D"));
            }
            QTimer::singleShot(1800, &application, [&, window, software, finish]() {
                const int lit3d = capture(window, selfTest, QStringLiteral("-library-3d"), QStringLiteral("the 3D cards"));
                if (!software && lit3d < 500) {
                    QTextStream(&selfTest.report) << "FAIL: the 3D cards show nothing\n";
                    selfTest.result = 1;
                }
                // Two effects in one: Portal 3D, with Guardian Orbs 3D added as
                // a group, then moved up, started later and made larger.
                controller.setLibraryOpen(false);
                controller.openPreset(QStringLiteral("portal-3d"));
                const int before = static_cast<int>(controller.session().effect().layers.size());
                const bool added = controller.addPreset(QStringLiteral("guardian-orbs-3d"));
                const auto groups = controller.session().groups();
                const QString group = groups.empty() ? QString() : QString::fromStdString(groups.front());
                controller.moveGroup(group, 0.0, 0.5, 0.0);
                controller.delayGroup(group, 0.3);
                controller.sizeGroup(group, 1.1);
                double start = -1;
                for (const vfx::Layer& l : controller.session().effect().layers) {
                    if (l.group == group.toStdString()) {
                        start = l.start;
                    }
                }
                controller.clickGroup(group, 0);  // the whole group: its heading clicked
                QTextStream groupsOut(&selfTest.report);
                groupsOut << "groups: added " << (added ? "yes" : "NO") << ", group \"" << group << "\", "
                          << controller.session().effect().layers.size() - static_cast<std::size_t>(before)
                          << " layers, starts at " << start << " s, effect " << controller.duration() << " s\n";
                if (!added || group != QStringLiteral("Guardian Orbs 3D") || std::fabs(start - 0.3) > 1e-6 ||
                    std::fabs(controller.duration() - 4.0) > 0.02) {
                    groupsOut << "FAIL: adding an effect as a group did not work\n";
                    selfTest.result = 1;
                }
                // The handles: a quarter turn of the group about the upright,
                // dragged in steps as a hand would, undone as one step.
                const QVariantMap handles = controller.gizmo();
                const std::size_t undoBefore = controller.session().commands().undoCount();
                bool turned = controller.beginGizmo(QStringLiteral("turn"));
                for (int i = 1; i <= 9; ++i) {
                    controller.dragGizmo(QVariantMap{{QStringLiteral("turn"), 10.0 * i}, {QStringLiteral("axis"), 1}});
                }
                controller.endGizmo();
                const bool oneStep = controller.session().commands().undoCount() == undoBefore + 1;
                turned = turned && oneStep &&
                         controller.session().commands().undoName() == "Turn Guardian Orbs 3D";
                groupsOut << "handles: on \"" << handles.value(QStringLiteral("label")).toString() << "\" ("
                          << handles.value(QStringLiteral("count")).toInt() << " layers), pivot height "
                          << handles.value(QStringLiteral("y")).toDouble() << "; a dragged quarter turn "
                          << (turned ? "is one undo step" : "did NOT work") << "\n";
                if (!handles.value(QStringLiteral("visible")).toBool() || !turned) {
                    groupsOut << "FAIL: the handles did not work\n";
                    selfTest.result = 1;
                }
                // Selection: one layer of the group alone, then two layers
                // from different places; the handles reach only those.
                const int orbsIndex = before;
                controller.clickLayer(orbsIndex, 0);
                const int alone = controller.gizmo().value(QStringLiteral("count")).toInt();
                controller.clickLayer(0, int(Qt::ControlModifier));
                const int two = controller.gizmo().value(QStringLiteral("count")).toInt();
                const std::string beforeMove = vfx::writeEffect(controller.session().effect());
                controller.beginGizmo(QStringLiteral("move"));
                controller.dragGizmo(QVariantMap{{QStringLiteral("move"), QVariantList{0.5, 0.0, 0.0}}});
                controller.endGizmo();
                int touched = 0;
                {
                    auto parsed = vfx::readEffect(beforeMove);
                    const vfx::Effect& was = parsed.value().effect;
                    for (std::size_t i = 0; i < was.layers.size(); ++i) {
                        const vfx::Layer& a = was.layers[i];
                        const vfx::Layer& b = controller.session().effect().layers[i];
                        bool same = a.modules.size() == b.modules.size();
                        for (std::size_t m = 0; same && m < a.modules.size(); ++m) {
                            same = a.modules[m].values == b.modules[m].values;
                        }
                        touched += same ? 0 : 1;
                    }
                }
                groupsOut << "selection: a layer of a group alone reaches " << alone << ", Cmd-click adds one: " << two
                          << "; moving them changed " << touched << " layers\n";
                if (alone != 1 || two != 2 || touched != 2) {
                    groupsOut << "FAIL: the handles reached more than the selection\n";
                    selfTest.result = 1;
                }
                // The layer list's menu: copy two layers at once, group them,
                // copy the group; every step undoes and nothing is left over.
                {
                    const std::string untouched = vfx::writeEffect(controller.session().effect());
                    const std::size_t layersBefore = controller.session().effect().layers.size();
                    const std::size_t groupSize = static_cast<std::size_t>(
                        std::count_if(controller.session().effect().layers.begin(),
                                      controller.session().effect().layers.end(),
                                      [&](const vfx::Layer& l) { return l.group == group.toStdString(); }));
                    controller.clickLayer(orbsIndex, 0);
                    controller.clickLayer(0, int(Qt::ControlModifier));
                    controller.duplicateSelection();
                    const bool twoCopied = controller.session().effect().layers.size() == layersBefore + 2 &&
                                           controller.selectionCount() == 2;
                    controller.groupSelection();
                    const bool grouped = controller.selectedGroup() == QStringLiteral("Group");
                    controller.duplicateSelection();
                    const bool groupCopied = controller.selectedGroup() == QStringLiteral("Group copy") &&
                                             controller.session().effect().layers.size() == layersBefore + 4;
                    // Right-clicking a layer outside the selection selects it alone.
                    controller.prepareLayerMenu(0);
                    const bool menuSelects = controller.selectionCount() == 1 && controller.selectedGroup().isEmpty();
                    for (int i = 0; i < 3; ++i) {
                        controller.undo();
                    }
                    const bool restored = vfx::writeEffect(controller.session().effect()) == untouched;
                    groupsOut << "duplicate: two layers " << (twoCopied ? "yes" : "NO") << ", grouped "
                              << (grouped ? "yes" : "NO") << ", the group copied " << (groupCopied ? "yes" : "NO")
                              << ", menu selects the clicked row " << (menuSelects ? "yes" : "NO")
                              << ", undone to the start " << (restored ? "yes" : "NO") << " (group of " << groupSize
                              << " layers)\n";
                    if (!(twoCopied && grouped && groupCopied && menuSelects && restored)) {
                        groupsOut << "FAIL: copying or grouping layers did not work\n";
                        selfTest.result = 1;
                    }
                }
                // The turn rings, for the picture.
                controller.clickGroup(group, 0);
                controller.setGizmoMode(QStringLiteral("turn"));
                QTimer::singleShot(900, &application, [&, window, software, finish]() {
                    capture(window, selfTest, QStringLiteral("-groups"), QStringLiteral("two effects in one, as groups"));
                    // The slot-symbol workflow: a symbol as the target, a
                    // Symbol Outlines effect fitted to it, then another symbol.
                    controller.setGizmoMode(QStringLiteral("move"));
                    controller.openPreset(QStringLiteral("legendary-outline"));
                    auto drawSymbol = [](bool crown) {
                        QImage symbol(256, 256, QImage::Format_RGBA8888);
                        symbol.fill(Qt::transparent);
                        QPainter painter(&symbol);
                        painter.setRenderHint(QPainter::Antialiasing);
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(QColor(240, 190, 40));
                        if (crown) {
                            QPolygonF shape;
                            shape << QPointF(30, 200) << QPointF(30, 70) << QPointF(80, 130) << QPointF(128, 40)
                                  << QPointF(176, 130) << QPointF(226, 70) << QPointF(226, 200);
                            painter.drawPolygon(shape);
                        } else {
                            painter.drawEllipse(QPointF(128, 128), 100, 100);
                        }
                        painter.end();
                        const QString file = QDir::temp().filePath(crown ? QStringLiteral("vfxforge-selftest-crown.png")
                                                                         : QStringLiteral("vfxforge-selftest-coin.png"));
                        symbol.save(file);
                        return QUrl::fromLocalFile(file);
                    };
                    auto pathOfFirst = [&]() {
                        const vfx::Layer& l = controller.session().effect().layers.front();
                        for (const vfx::Module& m : l.modules) {
                            if (m.type == "shape") {
                                return std::get<vfx::PointList>(*m.find("path"));
                            }
                        }
                        return vfx::PointList{};
                    };
                    const vfx::PointList square = pathOfFirst();
                    const bool coin = controller.setTargetFile(drawSymbol(false));
                    const vfx::PointList round = pathOfFirst();
                    const bool crown = controller.setTargetFile(drawSymbol(true));
                    const vfx::PointList spiky = pathOfFirst();
                    double top = -1e9;
                    for (const vfx::Point2& q : spiky.points) {
                        top = std::max(top, q.y);
                    }
                    QTextStream targetOut(&selfTest.report);
                    targetOut << "target: coin " << (coin ? "set" : "NOT set") << ", crown " << (crown ? "set" : "NOT set")
                              << "; " << controller.fittedLayers() << " layers follow it; outline points "
                              << round.points.size() << " then " << spiky.points.size() << ", crown top at " << top << "\n";
                    if (!coin || !crown || controller.fittedLayers() < 3 || round == square || spiky == round ||
                        !controller.hasTarget()) {
                        targetOut << "FAIL: the effect did not follow the target\n";
                        selfTest.result = 1;
                    }
                    controller.setPreviewBackground(QStringLiteral("checker"));
                    QTimer::singleShot(900, &application, [&, window, software, finish]() {
                        const int lit = capture(window, selfTest, QStringLiteral("-target"),
                                                QStringLiteral("an outline effect fitted to a symbol"));
                        if (!software && lit < 500) {
                            QTextStream(&selfTest.report) << "FAIL: the fitted outline shows nothing\n";
                            selfTest.result = 1;
                        }
                        controller.setPreviewBackground(QStringLiteral("dark"));
                        finish();
                    });
                });
            });
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

            // Something lying on the ground: a clip of the Vortex Portal
            // preset. It has to be read as lying flat and turning, and
            // rebuilt with a layer that lies flat.
            const bool portal = ref.loadPath(QStringLiteral(":/samples/reference-portal.gif"));
            ref.waitUntilIdle(60000);
            out << "reference portal: " << ref.report() << "\n";
            bool lying = false, turning = false;
            for (const QVariant& row : ref.findings()) {
                const QVariantMap finding = row.toMap();
                lying = lying || finding.value(QStringLiteral("key")).toString() == QStringLiteral("ground");
                turning = turning || (finding.value(QStringLiteral("key")).toString() == QStringLiteral("groundTurn") &&
                                      finding.value(QStringLiteral("value")).toString().contains(QStringLiteral("degrees a second")));
            }
            out << "read as lying on the ground: " << (lying ? "yes" : "NO") << ", turning measured: "
                << (turning ? "yes" : "NO") << "\n";
            if (!portal || !lying || !turning) {
                out << "FAIL: the sample portal was not read as a turning base on the ground\n";
                selfTest.result = 1;
            }
            ref.build();
            ref.waitUntilIdle(180000);
            out << "built from the portal: " << ref.report() << "\n";
            int flat = 0;
            for (const vfx::Layer& layer : controller.session().effect().layers) {
                for (const vfx::Module& module : layer.modules) {
                    if (module.type == "sprite") {
                        if (const vfx::Value* tilt = module.find("tilt")) {
                            flat += std::get<double>(*tilt) > 1.0 ? 1 : 0;
                        }
                    }
                }
            }
            out << "layers lying flat: " << flat << "\n";
            if (!ref.built() || flat < 1) {
                out << "FAIL: the portal was not rebuilt with a layer lying flat\n";
                selfTest.result = 1;
            }
            ref.setTime(ref.length() * 0.4);
            QCoreApplication::processEvents();
            capture(window, selfTest, QStringLiteral("-reference-ground"),
                    QStringLiteral("the reference workspace with a ground effect"));

            // A sheet of several effects (a frame, a lightning column, an
            // orb): each is found; one is picked and read again on its own;
            // the column is read as two bolts and a fill; and a flipbook of
            // its own frames is made from it.
            const bool sheet = ref.loadPath(QStringLiteral(":/samples/reference-sheet.gif"));
            ref.waitUntilIdle(60000);
            out << "reference sheet: " << ref.sheetEffects().size() << " effects found\n";
            if (!sheet || ref.sheetEffects().size() != 3) {
                out << "FAIL: the sample sheet's three effects were not found\n";
                selfTest.result = 1;
            }
            capture(window, selfTest, QStringLiteral("-reference-sheet"),
                    QStringLiteral("the reference workspace with a sheet of several effects"));
            // The column holds the most light; the second box in reading order.
            ref.pickSheetEffect(1);
            ref.waitUntilIdle(60000);
            int strokes = 0;
            bool lead = false, filled = false;
            for (const QVariant& row : ref.findings()) {
                const QString key = row.toMap().value(QStringLiteral("key")).toString();
                strokes += key == QStringLiteral("line") ? 1 : 0;
                lead = lead || key == QStringLiteral("lines");
                filled = filled || key == QStringLiteral("lineFill");
            }
            out << "picked from the sheet: " << ref.report() << " (strokes " << strokes << ", fill "
                << (filled ? "yes" : "no") << ")\n";
            if (!ref.sheetPicked() || !lead || strokes != 2 || !filled) {
                out << "FAIL: the column picked from the sheet was not read as two bolts and a fill\n";
                selfTest.result = 1;
            }
            ref.setFlipbook(true);
            ref.build();
            ref.waitUntilIdle(180000);
            out << "built from the sheet's column with a flipbook: " << ref.report() << "\n";
            bool flipbookLayer = false;
            for (const vfx::Layer& layer : controller.session().effect().layers) {
                flipbookLayer = flipbookLayer || (layer.name == "Flipbook" && layer.enabled);
            }
            const double flipAlike = ref.similarity().value(QStringLiteral("overall")).toDouble();
            if (!ref.built() || !flipbookLayer || flipAlike < 0.75) {
                out << "FAIL: no flipbook was made from the sheet's column (overall " << flipAlike << ")\n";
                selfTest.result = 1;
            }
            ref.setFlipbook(false);
            ref.setTime(ref.length() * 0.3);
            QCoreApplication::processEvents();
            capture(window, selfTest, QStringLiteral("-reference-flipbook"),
                    QStringLiteral("the reference workspace with a flipbook"));
            ref.wholeSheet();
            ref.waitUntilIdle(60000);
            if (ref.sheetPicked() || ref.sheetEffects().size() != 3) {
                out << "FAIL: going back to the whole sheet did not show its three effects again\n";
                selfTest.result = 1;
            }

            // Video goes through the computer's own media support, which a
            // build machine may lack. What happens is written down either way.
            for (const char* name : {"reference-burst.mp4", "reference-burst.webm"}) {
                const QString temp = QDir::tempPath() + QStringLiteral("/vfxforge-selftest-") + QString::fromLatin1(name);
                QFile::remove(temp);
                QFile::copy(QStringLiteral(":/samples/") + QString::fromLatin1(name), temp);
                QFile::setPermissions(temp, QFile::ReadOwner | QFile::WriteOwner);
                ref.loadPath(temp);
                ref.waitUntilIdle(30000);
                const bool read = ref.moving() && ref.frameCount() >= 8 && ref.name().endsWith(QString::fromLatin1(name));
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
            // A ground preset in the main window: the new shapes are drawn by
            // the graphics card here, and lying flat is done by the mesh.
            if (!controller.openPreset(QStringLiteral("magic-circle"))) {
                out << "FAIL: the Magic Circle preset could not be opened\n";
                selfTest.result = 1;
            }
            QTimer::singleShot(2200, &application, [&, window, software, third]() {
                QTextStream line(&selfTest.report);
                line << "ground preset: " << controller.effectName() << ", " << controller.particleCount() << " particles\n";
                const int shown = capture(window, selfTest, QStringLiteral("-ground"), QStringLiteral("a ground preset"));
                if (!software && shown < 500) {
                    line << "FAIL: the ground preset shows nothing\n";
                    selfTest.result = 1;
                }
                // Dissolve, drawn by the graphics card: a sheet caught while it
                // is being eaten away. Then turbulence: ribbons winding upward.
                if (!controller.openPreset(QStringLiteral("burn-away"))) {
                    line << "FAIL: the Burn Away preset could not be opened\n";
                    selfTest.result = 1;
                }
                QTimer::singleShot(1150, &application, [&, window, software, third]() {
                    QTextStream more(&selfTest.report);
                    more << "dissolve preset: " << controller.effectName() << ", " << controller.particleCount()
                         << " particles\n";
                    const int burning = capture(window, selfTest, QStringLiteral("-dissolve"),
                                                QStringLiteral("a dissolving preset"));
                    if (!software && burning < 500) {
                        more << "FAIL: the dissolving preset shows nothing\n";
                        selfTest.result = 1;
                    }
                    if (!controller.openPreset(QStringLiteral("spirit-wisps"))) {
                        more << "FAIL: the Spirit Wisps preset could not be opened\n";
                        selfTest.result = 1;
                    }
                    QTimer::singleShot(3200, &application, [&, window, software, third]() {
                        QTextStream last(&selfTest.report);
                        last << "turbulence preset: " << controller.effectName() << ", " << controller.particleCount()
                             << " particles\n";
                        const int winding = capture(window, selfTest, QStringLiteral("-turbulence"),
                                                    QStringLiteral("a turbulent preset"));
                        if (!software && winding < 100) {
                            last << "FAIL: the turbulent preset shows nothing\n";
                            selfTest.result = 1;
                        }
                        // The Advanced view: every setting of a layer, with
                        // the curve and colour-gradient editors. Campfire's
                        // flames have a size curve and a white-hot gradient.
                        if (!controller.openPreset(QStringLiteral("campfire"))) {
                            last << "FAIL: the Campfire preset could not be opened\n";
                            selfTest.result = 1;
                        }
                        controller.selectLayer(1);
                        controller.setAdvanced(true);
                        QTimer::singleShot(900, &application, [&, window, software, third]() {
                            QTextStream adv(&selfTest.report);
                            const QVariantList rows = controller.controls();
                            QStringList kinds;
                            int sizeCurve = -1, speed = -1, gradient = -1, drag = -1;
                            QString section;
                            for (const QVariant& item : rows) {
                                const QVariantMap row = item.toMap();
                                const QString kind = row.value(QStringLiteral("kind")).toString();
                                if (!kinds.contains(kind)) {
                                    kinds.push_back(kind);
                                }
                                if (kind == QStringLiteral("section")) {
                                    section = row.value(QStringLiteral("label")).toString();
                                    continue;
                                }
                                const QString label = row.value(QStringLiteral("label")).toString();
                                const int at = row.value(QStringLiteral("control")).toInt();
                                if (section == QStringLiteral("Over Lifetime") && label == QStringLiteral("Size")) {
                                    sizeCurve = at;
                                } else if (section == QStringLiteral("Over Lifetime") && label == QStringLiteral("Color")) {
                                    gradient = at;
                                } else if (section == QStringLiteral("Initial State") && label == QStringLiteral("Speed")) {
                                    speed = at;
                                } else if (section == QStringLiteral("Motion") && label == QStringLiteral("Drag")) {
                                    drag = at;
                                }
                            }
                            adv << "advanced view: " << rows.size() << " rows; kinds: " << kinds.join(QStringLiteral(", "))
                                << "\n";
                            for (const char* wanted : {"section", "number", "range", "curve", "gradient", "bursts", "vector",
                                                       "choice", "shape", "direction", "sound"}) {
                                if (!kinds.contains(QString::fromLatin1(wanted))) {
                                    adv << "FAIL: the advanced view has no " << wanted << " row\n";
                                    selfTest.result = 1;
                                }
                            }
                            if (kinds.contains(QStringLiteral("unsupported"))) {
                                adv << "FAIL: the advanced view has a setting it cannot edit\n";
                                selfTest.result = 1;
                            }
                            capture(window, selfTest, QStringLiteral("-advanced"), QStringLiteral("the advanced view"));

                            // Edits through it, as the editors make them.
                            const QVariantList popped = controller.applyCurveShape(sizeCurve, QStringLiteral("pop"));
                            bool smooth = false;
                            for (const QVariant& key : popped) {
                                smooth = smooth || key.toMap().value(QStringLiteral("smooth")).toBool();
                            }
                            const int drawn = static_cast<int>(controller.curveLine(sizeCurve, popped).size());
                            controller.setControlMode(speed, QStringLiteral("curve"));
                            controller.setControlNumber(drag, 1.25);
                            QVariantList stops;
                            for (const QVariant& item : controller.controls()) {
                                const QVariantMap row = item.toMap();
                                if (row.value(QStringLiteral("control")).toInt() == gradient &&
                                    row.value(QStringLiteral("kind")).toString() == QStringLiteral("gradient")) {
                                    stops = row.value(QStringLiteral("stops")).toList();
                                }
                            }
                            const int strip = static_cast<int>(controller.gradientStrip(stops, 48).size());
                            bool speedIsCurve = false;
                            for (const QVariant& item : controller.controls()) {
                                const QVariantMap row = item.toMap();
                                speedIsCurve = speedIsCurve ||
                                               (row.value(QStringLiteral("control")).toInt() == speed &&
                                                row.value(QStringLiteral("kind")).toString() == QStringLiteral("curve"));
                            }
                            adv << "curve shape applied: " << popped.size() << " points, smooth: " << (smooth ? "yes" : "NO")
                                << ", drawn as " << drawn << " pieces; speed turned into a curve: "
                                << (speedIsCurve ? "yes" : "NO") << "; gradient stops: " << stops.size() << ", strip "
                                << strip << "\n";
                            if (popped.size() < 3 || !smooth || drawn <= popped.size() || !speedIsCurve ||
                                stops.size() < 2 || strip != 48) {
                                adv << "FAIL: editing through the advanced view did not work\n";
                                selfTest.result = 1;
                            }
                            // Show the curve and gradient editors themselves, once
                            // the rebuilt list has been laid out.
                            QTimer::singleShot(500, &application, [&, window, software, third]() {
                            QTextStream scrolled(&selfTest.report);
                            QObject* panel = window->findChild<QObject*>(QStringLiteral("controlsPanel"));
                            QVariant shown;
                            if (!panel ||
                                !QMetaObject::invokeMethod(panel, "showSection", Q_RETURN_ARG(QVariant, shown),
                                                           Q_ARG(QVariant, QVariant(QStringLiteral("Over Lifetime")))) ||
                                !shown.toBool()) {
                                scrolled << "FAIL: the Over Lifetime group could not be scrolled to\n";
                                selfTest.result = 1;
                            }
                            QTimer::singleShot(900, &application, [&, window, software, third]() {
                                capture(window, selfTest, QStringLiteral("-curves"),
                                        QStringLiteral("the curve and gradient editors"));
                                controller.setAdvanced(false);
                                // The animation principles: a preset that squashes
                                // and stretches, shown on twos, with its curve in
                                // the Simple view; then the one-click changes.
                                if (!controller.openPreset(QStringLiteral("bouncy-pop"))) {
                                    QTextStream(&selfTest.report) << "FAIL: the Bouncy Pop preset could not be opened\n";
                                    selfTest.result = 1;
                                }
                                controller.selectLayer(0);
                                QTimer::singleShot(500, &application, [&, window, software, third]() {
                                    QTextStream motion(&selfTest.report);
                                    // What the panel shows for one setting of the selected layer.
                                    const auto row = [&](const QString& label) {
                                        for (const QVariant& item : controller.controls()) {
                                            const QVariantMap found = item.toMap();
                                            if (found.value(QStringLiteral("label")).toString() == label) {
                                                return found;
                                            }
                                        }
                                        return QVariantMap();
                                    };
                                    const QVariantMap stretch = row(QStringLiteral("Squash and Stretch"));
                                    const int ownShapes = static_cast<int>(stretch.value(QStringLiteral("shapes")).toList().size());
                                    const bool isCurve = stretch.value(QStringLiteral("kind")).toString() == QStringLiteral("curve");
                                    const double held = row(QStringLiteral("Hold Frames")).value(QStringLiteral("value")).toDouble();
                                    motion << "cartoon motion preset: " << controller.effectName() << ", "
                                           << controller.particleCount() << " particles; Squash and Stretch is "
                                           << (isCurve ? "a curve" : "NOT a curve") << " with " << ownShapes
                                           << " shapes on offer; Hold Frames " << held << "\n";
                                    if (!isCurve || ownShapes <= static_cast<int>(controller.curveShapes().size()) || held != 12.0) {
                                        motion << "FAIL: the Simple view does not show the principles of a Cartoon motion preset\n";
                                        selfTest.result = 1;
                                    }
                                    const int shown = capture(window, selfTest, QStringLiteral("-motion"),
                                                              QStringLiteral("a squash and stretch preset"));
                                    if (!software && shown < 500) {
                                        motion << "FAIL: the squash and stretch preset shows nothing\n";
                                        selfTest.result = 1;
                                    }
                                    // A curve made for this setting alone, then one click
                                    // each of two changes to the whole effect; the panel follows.
                                    const QVariantList wobble = controller.applyCurveShape(
                                        stretch.value(QStringLiteral("control")).toInt(), QStringLiteral("wobble"));
                                    const int offered = static_cast<int>(controller.motionChanges().size());
                                    controller.applyMotionChange(QStringLiteral("smooth-timing"));
                                    const double smooth = row(QStringLiteral("Hold Frames")).value(QStringLiteral("value")).toDouble();
                                    const bool undoable = controller.canUndo();
                                    controller.applyMotionChange(QStringLiteral("on-twos"));
                                    const double twos = row(QStringLiteral("Hold Frames")).value(QStringLiteral("value")).toDouble();
                                    const QString said = controller.message();
                                    motion << "one-click changes: " << offered << " offered; Smooth gave Hold Frames " << smooth
                                           << ", On twos gave " << twos << " (\"" << said << "\"); Wobble has "
                                           << wobble.size() << " points\n";
                                    if (offered != 7 || smooth != 0.0 || twos != 12.0 || !undoable || wobble.size() < 5) {
                                        motion << "FAIL: the one-click motion changes did not work\n";
                                        selfTest.result = 1;
                                    }
                                    // The curve editor on Squash and Stretch, and the
                                    // Motion menu open over the window.
                                    QObject* panel = window->findChild<QObject*>(QStringLiteral("controlsPanel"));
                                    QObject* menu = window->findChild<QObject*>(QStringLiteral("motionMenu"));
                                    QTimer::singleShot(500, &application, [&, window, software, third, panel, menu]() {
                                        QTextStream opened(&selfTest.report);
                                        QVariant found;
                                        if (!panel ||
                                            !QMetaObject::invokeMethod(panel, "showControl", Q_RETURN_ARG(QVariant, found),
                                                                       Q_ARG(QVariant, QVariant(QStringLiteral("Squash and Stretch")))) ||
                                            !found.toBool()) {
                                            opened << "FAIL: the Squash and Stretch control could not be scrolled to\n";
                                            selfTest.result = 1;
                                        }
                                        if (!menu || !QMetaObject::invokeMethod(menu, "open")) {
                                            opened << "FAIL: the Motion menu could not be opened\n";
                                            selfTest.result = 1;
                                        }
                                        QTimer::singleShot(700, &application, [&, window, software, third, panel, menu]() {
                                            capture(window, selfTest, QStringLiteral("-motion-menu"),
                                                    QStringLiteral("the Motion menu and the stretch curve"));
                                            if (menu) {
                                                QMetaObject::invokeMethod(menu, "close");
                                            }
                                            // The shape library: a preset made with it, whose
                                            // shapes the graphics card draws from their pictures,
                                            // and the picker with its groups.
                                            if (!controller.openPreset(QStringLiteral("trophy-win"))) {
                                                QTextStream(&selfTest.report) << "FAIL: the Trophy Win preset could not be opened\n";
                                                selfTest.result = 1;
                                            }
                                            controller.selectLayer(1);
                                            QTimer::singleShot(900, &application, [&, window, software, third, panel]() {
                                                QTextStream shapes(&selfTest.report);
                                                QVariantMap shapeRow;
                                                for (const QVariant& item : controller.controls()) {
                                                    const QVariantMap found = item.toMap();
                                                    if (found.value(QStringLiteral("kind")).toString() == QStringLiteral("shape")) {
                                                        shapeRow = found;
                                                    }
                                                }
                                                const QVariantList offered = shapeRow.value(QStringLiteral("options")).toList();
                                                const int groups = static_cast<int>(shapeRow.value(QStringLiteral("categories")).toList().size());
                                                const QString drawn = shapeRow.value(QStringLiteral("value")).toString();
                                                int grouped = 0;
                                                for (const QVariant& option : offered) {
                                                    grouped += option.toMap().value(QStringLiteral("category")).toString().isEmpty() ? 0 : 1;
                                                }
                                                shapes << "shape library: " << offered.size() << " shapes, " << grouped << " of them in one of "
                                                       << groups << " groups; the selected layer draws \"" << drawn << "\"\n";
                                                if (offered.size() < 135 || grouped != offered.size() || groups < 8 ||
                                                    drawn != QStringLiteral("trophy")) {
                                                    shapes << "FAIL: the shape library is not what the panel shows\n";
                                                    selfTest.result = 1;
                                                }
                                                QVariant found;
                                                if (!panel ||
                                                    !QMetaObject::invokeMethod(panel, "showControl", Q_RETURN_ARG(QVariant, found),
                                                                               Q_ARG(QVariant, QVariant(QStringLiteral("Shape")))) ||
                                                    !found.toBool()) {
                                                    shapes << "FAIL: the Shape control could not be scrolled to\n";
                                                    selfTest.result = 1;
                                                }
                                                const int lit = capture(window, selfTest, QStringLiteral("-shapes"),
                                                                        QStringLiteral("a preset made with library shapes"));
                                                if (!software && lit < 500) {
                                                    shapes << "FAIL: the preset made with library shapes shows nothing\n";
                                                    selfTest.result = 1;
                                                }
                                                // A moment later, so the picker's own small
                                                // pictures have been drawn too.
                                                QTimer::singleShot(600, &application, [&, window, software, third, panel]() {
                                                    capture(window, selfTest, QStringLiteral("-shape-picker"),
                                                            QStringLiteral("the shape picker"));
                                                    // Shapes that can be changed: the stars of the same
                                                    // preset given eight sharper points, then another
                                                    // shape picked (which comes as drawn), and that undone.
                                                    QTextStream edits(&selfTest.report);
                                                    const auto shapeRow = [&]() {
                                                        // The panel's list is only built again when the
                                                        // layer shown changes, so change it and come back.
                                                        controller.selectLayer(0);
                                                        controller.selectLayer(5);
                                                        QVariantMap row;
                                                        for (const QVariant& item : controller.controls()) {
                                                            const QVariantMap found = item.toMap();
                                                            if (found.value(QStringLiteral("kind")).toString() == QStringLiteral("shape")) {
                                                                row = found;
                                                            }
                                                        }
                                                        return row;
                                                    };
                                                    const auto pointsNow = [&]() {
                                                        return shapeRow().value(QStringLiteral("picture")).toMap()
                                                            .value(QStringLiteral("shapeCount")).toInt();
                                                    };
                                                    const QVariantMap stars = shapeRow();
                                                    int sliders = 0;
                                                    for (const QVariant& option : stars.value(QStringLiteral("options")).toList()) {
                                                        const QVariantMap o = option.toMap();
                                                        if (o.value(QStringLiteral("value")) == stars.value(QStringLiteral("value"))) {
                                                            sliders = static_cast<int>(o.value(QStringLiteral("edits")).toList().size());
                                                        }
                                                    }
                                                    controller.setPictureNumber(QStringLiteral("shapeCount"), 8);
                                                    controller.setPictureNumber(QStringLiteral("shapeSharpness"), 0.6);
                                                    const int eight = pointsNow();
                                                    controller.chooseShape(stars.value(QStringLiteral("control")).toInt(),
                                                                           QStringLiteral("starsix"));
                                                    const int asDrawn = pointsNow();
                                                    const QString picked = shapeRow().value(QStringLiteral("value")).toString();
                                                    controller.undo();
                                                    const int back = pointsNow();
                                                    edits << "shape settings: \"" << stars.value(QStringLiteral("value")).toString()
                                                          << "\" has " << sliders << " sliders; given " << eight
                                                          << " points; picking \"" << picked << "\" left " << asDrawn
                                                          << "; undo brought back " << back << "\n";
                                                    if (sliders != 3 || eight != 8 || picked != QStringLiteral("starsix") ||
                                                        asDrawn != 0 || back != 8) {
                                                        edits << "FAIL: a shape's own settings did not work\n";
                                                        selfTest.result = 1;
                                                    }
                                                    // Once more, so the panel's sliders show where the
                                                    // settings now stand.
                                                    shapeRow();
                                                    QTimer::singleShot(500, &application, [&, window, software, third, panel]() {
                                                        QVariant found;
                                                        if (panel) {
                                                            QMetaObject::invokeMethod(panel, "showControl", Q_RETURN_ARG(QVariant, found),
                                                                                      Q_ARG(QVariant, QVariant(QStringLiteral("Shape"))));
                                                        }
                                                        // From the start of its loop, so the picture is not
                                                        // taken in the pause at the end of one.
                                                        controller.restart();
                                                        QTimer::singleShot(600, &application, [&, window, software, third]() {
                                                            const int lit = capture(window, selfTest, QStringLiteral("-shape-edit"),
                                                                                    QStringLiteral("a changed shape and its sliders"));
                                                            if (!software && lit < 500) {
                                                                QTextStream(&selfTest.report) << "FAIL: the preset with a changed shape shows nothing\n";
                                                                selfTest.result = 1;
                                                            }
                                                            // A model to make an effect on: the practice dummy
                                                            // under Healing Aura.
                                                            if (!controller.openPreset(QStringLiteral("healing-aura"))) {
                                                                QTextStream(&selfTest.report) << "FAIL: the Healing Aura preset could not be opened\n";
                                                                selfTest.result = 1;
                                                            }
                                                            controller.useSampleModel();
                                                            QTimer::singleShot(900, &application, [&, window, software, third]() {
                                                                QTextStream model(&selfTest.report);
                                                                model << "model: " << (controller.hasModel() ? "shown" : "NOT shown") << ", \""
                                                                      << controller.modelName() << "\", " << controller.modelTriangles()
                                                                      << " triangles, particles behind it "
                                                                      << (controller.hideBehindModel() ? "hidden" : "shown") << "\n";
                                                                if (!controller.hasModel() || controller.modelTriangles() < 1000) {
                                                                    model << "FAIL: the practice dummy is not shown\n";
                                                                    selfTest.result = 1;
                                                                }
                                                                const int shown = capture(window, selfTest, QStringLiteral("-model"),
                                                                                          QStringLiteral("an effect on the practice dummy"));
                                                                if (!software && shown < 2000) {
                                                                    model << "FAIL: the model is not drawn\n";
                                                                    selfTest.result = 1;
                                                                }
                                                                controller.clearModel();
                                                                controller.setLibraryOpen(true);
                                                                QTimer::singleShot(2500, &application, third);
                                                            });
                                                        });
                                                    });
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                            });
                        });
                    });
                });
            });
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
