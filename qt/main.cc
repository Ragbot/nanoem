/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "emapp/Allocator.h"
#include "emapp/BaseApplicationService.h"
#include "emapp/emapp.h"

#include "MainWindow.h"
#include "QtApplicationClient.h"
#include "QtApplicationService.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
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
                QTimer::singleShot(4000, &window, [&window, &service, screenshotPath, screenshotDialog]() {
                    /* the offscreen platform never re-invokes paintGL on its own, so render the GL
                       frame on demand: this runs drawDefaultPass and populates
                       Project::logicalScaleUniformedViewportLayoutRect, which the viewport HUD
                       overlay needs to position the FPS badge and the CPU/MEM box */
                    if (QOpenGLWidget *gl = window.findChild<QOpenGLWidget *>()) {
                        gl->grabFramebuffer();
                    }
                    /* let the overlay repaint with the freshly populated layout rect */
                    QTimer::singleShot(500, &window, [&window, &service, screenshotPath, screenshotDialog]() {
                        if (!screenshotPath.isEmpty()) {
                            const QImage image(window.grab().toImage());
                            image.save(screenshotPath);
                        }
                        if (screenshotDialog.isEmpty()) {
                            QCoreApplication::exit(0);
                            return;
                        }
                        const int separator = screenshotDialog.indexOf(QLatin1Char(':'));
                        const QByteArray name = screenshotDialog.left(separator).toUtf8();
                        const QString dialogPath = screenshotDialog.mid(separator + 1);
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
                            if (menuItemType != 0) {
                                if (nanoem::Project *project = service.projectHolder()->currentProject()) {
                                    nanoem::Error error;
                                    service.dispatchMenuItemAction(project, menuItemType, error);
                                }
                            }
                        }
                        QTimer::singleShot(300, qApp, [&window, dialogPath]() {
                            const QWidgetList tops = QApplication::topLevelWidgets();
                            for (QWidget *it : tops) {
                                if (it != &window && it->isVisible() && it->isWindow()) {
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
