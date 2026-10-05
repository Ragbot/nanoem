/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "emapp/Allocator.h"
#include "emapp/BaseApplicationService.h"
#include "emapp/emapp.h"
#include "emapp/plugin/ModelIOPlugin.h"
#include "emapp/plugin/MotionIOPlugin.h"

#include "MainWindow.h"
#include "QtApplicationClient.h"
#include "QtApplicationService.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTimer>

#include "bx/commandline.h"
#include "bx/os.h"

#include <cstdio>

using namespace nanoem;

static int
runMain(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("nanoem"));
    Allocator::initialize();
    BaseApplicationService::setup();
    {
        char localeBuffer[32] = { 0 };
        uint32_t localeSize = sizeof(localeBuffer);
        bx::getEnv(localeBuffer, &localeSize, "LANG");
        const QString executableDirPath = QCoreApplication::applicationDirPath();
        const QByteArray pluginDirBytes = (executableDirPath + QStringLiteral("/plugins")).toUtf8();
        const String pluginDirPath(pluginDirBytes.constData());
        URIList pluginURIs;
        const QDir pluginDir(QString::fromUtf8(pluginDirPath.c_str()));
        QStringList nameFilters;
        nameFilters << QStringLiteral("plugin*");
        const QStringList entries(pluginDir.entryList(nameFilters, QDir::Files));
        for (QStringList::const_iterator it = entries.begin(), end = entries.end(); it != end; ++it) {
            const QByteArray path = pluginDir.filePath(*it).toUtf8();
            pluginURIs.push_back(URI::createFromFilePath(String(path.constData())));
        }
        String effectPluginPath(pluginDirPath);
        effectPluginPath.append("/plugin_effect." BX_DL_EXT);
        bx::FilePath tempPath(bx::Dir::Temp);
        JSON_Value *config = json_value_init_object();
        JSON_Object *root = json_object(config);
        json_object_dotset_string(root, "qt.plugin.path", pluginDirPath.c_str());
        json_object_dotset_string(root, "plugin.effect.path", effectPluginPath.c_str());
        json_object_dotset_string(root, "project.tmp.path", tempPath.getCPtr());
        json_object_dotset_string(root, "project.locale", localeBuffer);
        QString fileToLoad, screenshotPath, screenshotDialog;
        for (int i = 1; i < argc; i++) {
            const QString arg(QString::fromLocal8Bit(argv[i]));
            if (arg == QStringLiteral("--screenshot") && i + 1 < argc) {
                screenshotPath = QString::fromLocal8Bit(argv[++i]);
            }
            else if (arg == QStringLiteral("--screenshot-dialog") && i + 1 < argc) {
                /* --screenshot-dialog <name>:<path> opens a native Qt dialog after the window has
                   settled and grabs it instead of the main window (window.grab() does not cover
                   independent top-level windows) */
                screenshotDialog = QString::fromLocal8Bit(argv[++i]);
            }
            else if (!arg.startsWith(QLatin1Char('-'))) {
                fileToLoad = QFileInfo(arg).absoluteFilePath();
            }
        }
        qt::QtApplicationClient::Bridge bridge;
        qt::QtApplicationClient client(&bridge);
        qt::QtApplicationService service(config, &bridge);
        qt::MainWindow window(&service, &client);
        window.setFileToLoad(fileToLoad);
        int result = 1;
        if (window.initialize()) {
            client.sendLoadAllDecoderPluginsMessage(pluginURIs);
            client.sendLoadAllEncoderPluginsMessage(pluginURIs);
            client.sendLoadAllModelPluginsMessage(pluginURIs);
            client.sendLoadAllMotionPluginsMessage(pluginURIs);
            window.show();
            if (!screenshotPath.isEmpty() || !screenshotDialog.isEmpty()) {
                /* the screenshot mode is a non-interactive test harness: grab the window after
                   the app has settled and force-exit, otherwise the dirty project confirmation
                   dialog (Save/Discard/Cancel) would block waiting for user input */
                QTimer::singleShot(4000, &window, [&window, &service, &client, screenshotPath, screenshotDialog]() {
                    /* the offscreen platform never re-invokes paintGL on its own, so render the GL
                       frame on demand: this runs drawDefaultPass and populates
                       Project::logicalScaleUniformedViewportLayoutRect, which the viewport HUD
                       overlay needs to position the FPS badge and the CPU/MEM box */
                    if (QOpenGLWidget *gl = window.findChild<QOpenGLWidget *>()) {
                        gl->grabFramebuffer();
                    }
                    /* let the overlay repaint with the freshly populated layout rect */
                    QTimer::singleShot(500, &window, [&window, &service, &client, screenshotPath, screenshotDialog]() {
                        if (!screenshotPath.isEmpty()) {
                            /* window.grab() only rasterizes native Qt widgets; the offscreen platform
                               never composites the QOpenGLWidget FBO, so read the rendered 3D frame
                               directly and overlay it at the viewport widget's position. This is what
                               makes the viewport pixels (and therefore the native present path)
                               visible in offscreen captures. */
                            QOpenGLWidget *gl = window.findChild<QOpenGLWidget *>();
                            const QImage framebuffer(gl ? gl->grabFramebuffer() : QImage());
                            const QImage image(window.grab().toImage());
                            if (!framebuffer.isNull() && gl) {
                                QPainter painter(&image);
                                painter.drawImage(QRect(gl->mapTo(&window, QPoint(0, 0)), gl->size()), framebuffer);
                            }
                            image.save(screenshotPath);
                        }
                        if (screenshotDialog.isEmpty()) {
                            QCoreApplication::exit(0);
                            return;
                        }
                        const int separator = screenshotDialog.indexOf(QLatin1Char(':'));
                        const QByteArray name = screenshotDialog.left(separator).toUtf8();
                        const QString dialogPath = screenshotDialog.mid(separator + 1);
                        /* tabbed dialogs record which tab to switch to before the grab */
                        int dialogTabIndex = -1;
                        /* list based dialogs record which row of the current page's list to select */
                        int dialogListRow = -1;
                        bool enableParameterInspection = false;
                        /* the effect parameter dialog hides its Parameter tab unless the active
                           model has an attached effect, so those captures attach a fixture first */
                        bool attachEffect = false;
                        if (name == "self-shadow") {
                            service.openSelfShadowConfigurationDialog();
                        }
                        else {
                            /* go through the real menu dispatch so the whole chain the menus use
                               (service switch -> IUIWindow seam -> QtUIWindow override) is what
                               gets exercised */
                            nanoem_u32_t menuItemType = 0;
                            if (name == "morph-correction") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeEditMorphOpenCorrectionDialog;
                            }
                            else if (name == "bone-correction") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeEditBoneOpenCorrectionDialog;
                            }
                            else if (name == "camera-correction") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeEditCameraOpenCorrectionDialog;
                            }
                            else if (name == "bone-bias") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeEditBoneOpenBiasDialog;
                            }
                            else if (name == "model-outside-parent") {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    service.openModelOutsideParentDialog(project);
                                }
                            }
                            else if (name == "accessory-outside-parent") {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    service.openAccessoryOutsideParentDialog(project);
                                }
                            }
                            else if (name == "viewport-setting") {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    service.openViewportDialog(project);
                                }
                            }
                            else if (name == "model-edge") {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    service.openModelEdgeDialog(project);
                                }
                            }
                            else if (name == "physics-engine") {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    service.openPhysicsEngineDialog(project);
                                }
                            }
                            else if (name == "scale-all-keyframes") {
                                service.openScaleAllSelectedKeyframesDialog();
                            }
                            else if (name == "save-project") {
                                /* File ▸ Save on a project that has never been saved sends the
                                   SAVE_PROJECT command, which lands in
                                   QtUIWindow::openSaveProjectDialog */
                                client.sendSaveProjectMessage();
                            }
                            else if (name == "model-plugin" || name == "motion-plugin") {
                                /* Function 0 ("UI") of the bundled test plugin returns a UI window
                                   layout instead of executing right away, so the shell opens the
                                   IO plugin dialog built from that layout */
                                const bool model = name == QStringLiteral("model-plugin");
                                if (model) {
                                    nanoem::DefaultFileManager::ModelIOPluginList plugins;
                                    service.defaultFileManager()->getAllModelIOPlugins(plugins);
                                    for (nanoem_rsize_t i = 0; i < plugins.size(); i++) {
                                        if (QString::fromUtf8(plugins[i]->name()) ==
                                            QStringLiteral("This is a test.")) {
                                            client.sendExecuteModelPluginMessage(nanoem_u16_t(i), 0);
                                            break;
                                        }
                                    }
                                }
                                else {
                                    nanoem::DefaultFileManager::MotionIOPluginList plugins;
                                    service.defaultFileManager()->getAllMotionIOPlugins(plugins);
                                    /* plugin_motion reports an empty name (and is the only bundled
                                       motion plugin), so there is nothing to match on */
                                    if (plugins.size() > 0) {
                                        client.sendExecuteMotionPluginMessage(0, 0);
                                    }
                                }
                            }
                            else if (name == "preference" || name == "preference-project" ||
                                name == "preference-special") {
                                menuItemType = nanoem::ApplicationMenuBuilder::kMenuItemTypeEditPreference;
                                dialogTabIndex =
                                    name == "preference-project" ? 1 : (name == "preference-special" ? 2 : 0);
                            }
                            else if (name == "bone-parameters") {
                                menuItemType = nanoem::ApplicationMenuBuilder::kMenuItemTypeEditBoneOpenParameterDialog;
                            }
                            else if (name == "camera-parameters") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeEditCameraOpenParameterDialog;
                            }
                            else if (name == "effect-parameter" || name == "effect-parameter-model" ||
                                name == "effect-parameter-parameters" || name == "effect-parameter-option" ||
                                name == "effect-parameter-uniform" || name == "effect-parameter-render-target") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeEditOpenEffectParameterWindow;
                                attachEffect = true;
                                if (name == "effect-parameter-model") {
                                    dialogTabIndex = 1;
                                }
                                else if (name == "effect-parameter-option") {
                                    dialogTabIndex = 0;
                                    dialogListRow = 1;
                                }
                                else if (name == "effect-parameter-parameters") {
                                    dialogTabIndex = 2;
                                    dialogListRow = 0;
                                }
                                else if (name == "effect-parameter-uniform") {
                                    dialogTabIndex = 2;
                                    dialogListRow = 1;
                                    enableParameterInspection = true;
                                }
                                else if (name == "effect-parameter-render-target") {
                                    dialogTabIndex = 2;
                                    dialogListRow = 4;
                                }
                            }
                            else if (name == "draw-order") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeProjectOpenDrawOrderDialog;
                            }
                            else if (name == "transform-order") {
                                menuItemType =
                                    nanoem::ApplicationMenuBuilder::kMenuItemTypeProjectOpenTransformOrderDialog;
                            }
                            if (menuItemType != 0) {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    if (attachEffect) {
                                        /* attach through the same chain the Offscreen tab's
                                           "(None)" button uses: register the drawable then let
                                           IFileManager load the effect source */
                                        if (nanoem::Model *model = project->activeModel()) {
                                            project->setEffectPluginEnabled(true);
                                            const nanoem::String ownerName(
                                                nanoem::Effect::kOffscreenOwnerNameMain);
                                            project->clearAllDrawablesToAttachOffscreenRenderTargetEffect(ownerName);
                                            project->addDrawableToAttachOffscreenRenderTargetEffect(ownerName, model);
                                            const QString effectPath =
                                                QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/../test/fixtures/effects/offscreen.fx");
                                            const String effectPathString(effectPath.toUtf8().constData());
                                            const nanoem::URI fxURI(nanoem::URI::createFromFilePath(effectPathString));
                                            nanoem::Error effectError;
                                            const bool loaded = project->fileManager()->loadFromFile(
                                                fxURI, nanoem::IFileManager::kDialogTypeOpenEffectFile, project,
                                                effectError);
                                            fprintf(stderr,
                                                "[screenshot] effect=%s loaded=%d found=%d handle=%d enabled=%d "
                                                "err=%s\n",
                                                project->resolveEffect(model) ? "attached" : "none",
                                                int(loaded), int(project->findEffect(fxURI) != nullptr),
                                                int(model->handle()), int(project->isEffectPluginEnabled()),
                                                effectError.hasReason() ? effectError.reasonConstString()
                                                                        : "no error");
                                            if (enableParameterInspection) {
                                                if (nanoem::Effect *attached = project->resolveEffect(model)) {
                                                    attached->setPassUniformBufferInspectionEnabled(true);
                                                }
                                            }
                                        }
                                    }
                                    nanoem::Error error;
                                    service.dispatchMenuItemAction(project, menuItemType, error);
                                }
                            }
                        }
                        QTimer::singleShot(300, qApp, [&window, dialogPath, dialogTabIndex, dialogListRow]() {
                            const QWidgetList tops = QApplication::topLevelWidgets();
                            for (QWidget *it : tops) {
                                if (it != &window && it->isVisible() && it->isWindow()) {
                                    if (dialogTabIndex >= 0) {
                                        if (QTabWidget *tabs = it->findChild<QTabWidget *>()) {
                                            tabs->setCurrentIndex(dialogTabIndex);
                                        }
                                    }
                                    if (dialogListRow >= 0) {
                                        if (QTabWidget *tabs = it->findChild<QTabWidget *>()) {
                                            if (QWidget *page = tabs->currentWidget()) {
                                                if (QListWidget *list = page->findChild<QListWidget *>()) {
                                                    list->setCurrentRow(dialogListRow);
                                                }
                                            }
                                        }
                                    }
                                    it->grab().toImage().save(dialogPath);
                                    break;
                                }
                            }
                            QCoreApplication::exit(0);
                        });
                    });
                });
            }
            result = app.exec();
        }
        else {
            fprintf(stderr,
                "[nanoem] failed to initialize the Qt shell "
                "(is plugins/sokol_glcore33 shared library built?)\n");
        }
        json_value_free(config);
        Allocator::destroy();
        return result;
    }
}

int
main(int argc, char *argv[])
{
    return runMain(argc, argv);
}
