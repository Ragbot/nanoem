/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "MainWindow.h"
#include "QtAccessoryPanel.h"
#include "QtApplicationClient.h"
#include "QtApplicationMenuBuilder.h"
#include "QtApplicationService.h"
#include "QtBonePanel.h"
#include "QtCameraPanel.h"
#include "QtInterpolationPanel.h"
#include "QtLightPanel.h"
#include "QtModelPanel.h"
#include "QtMorphPanel.h"
#include "QtPlayPanel.h"
#include "QtTimelinePanel.h"
#include "QtViewPanel.h"
#include "QtViewport3D.h"
#include "QtViewportOverlay.h"
#include "QtViewportParameterBox.h"

#include "emapp/ApplicationPreference.h"
#include "emapp/BaseApplicationService.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/StringUtils.h"
#include "emapp/internal/IUIWindow.h"
#include "emapp/private/CommonInclude.h"

#include <QCloseEvent>
#include <QCursor>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLayout>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#include "bx/os.h"

#include <cstdio>

namespace nanoem {
namespace qt {
namespace {

static const char kWindowTitle[] = "nanoem";

static Vector2SI32
logicalCursorPosition(const QPointF &position)
{
    return Vector2SI32(static_cast<int>(position.x()), static_cast<int>(position.y()));
}

static Vector2SI32
screenCursorPosition(const QPointF &globalPosition, qreal devicePixelRatio)
{
    return Vector2SI32(
        static_cast<int>(globalPosition.x() * devicePixelRatio), static_cast<int>(globalPosition.y() * devicePixelRatio));
}

static void
sendUnicodeInputIfAny(QtApplicationClient *client, const QKeyEvent *event)
{
    const QString text(event->text());
    const Qt::KeyboardModifiers modifiers(event->modifiers());
    if (!text.isEmpty() && !modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::MetaModifier)) {
        for (int i = 0, size = text.size(); i < size; i++) {
            const char16_t value = text.at(i).unicode();
            nanoem_u32_t codePoint = value;
            if (value >= 0xd800 && value < 0xdc00 && i + 1 < size) {
                const char16_t low = text.at(i + 1).unicode();
                if (low >= 0xdc00 && low < 0xe000) {
                    codePoint = 0x10000u + ((value - 0xd800u) << 10) + (low - 0xdc00u);
                    i++;
                }
            }
            if (codePoint >= 0x20u) {
                client->sendUnicodeInputMessage(codePoint);
            }
        }
    }
}

} /* namespace anonymous */

Viewport::Viewport(QtApplicationService *service, QtApplicationClient *client, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
    , m_client(client)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    m_viewport3D = new QtViewport3D(m_service, m_client, this);
    layout->addWidget(m_viewport3D);
    
    m_overlay = new QtViewportOverlay(m_service, m_viewport3D);
    m_parameterBox = new QtViewportParameterBox(m_service, m_viewport3D);
}

Viewport::~Viewport()
{
    m_service->setFrameBufferResolver(nullptr);
}

void
Viewport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_overlay->setGeometry(rect());
    const int parameterBoxHeight = m_parameterBox->height();
    m_parameterBox->setGeometry(0, height() - parameterBoxHeight, width(), parameterBoxHeight);
}

MainWindow::MainWindow(QtApplicationService *service, QtApplicationClient *client, QWidget *parent)
    : QMainWindow(parent)
    , m_service(service)
    , m_client(client)
{
    m_viewport = new Viewport(m_service, m_client, this);
    /* the native timeline column shares the central widget with the viewport. ImGuiWindow clamps
       io.DisplaySize up to the 1080px project minimum, so the window grows by the timeline width
       (like it grows by the menu bar height below) to keep the GL widget at the full project
       width with the timeline sitting beside it instead of stealing viewport space. */
    const Vector2SI32 minimumWindowSize(BaseApplicationService::minimumRequiredWindowSize());
    const int timelineWidth = int(minimumWindowSize.x * 0.3f); /* ImGuiWindow::kTimelineDefaultWidthRatio */
    auto *centralWidget = new QWidget(this);
    auto *centralLayout = new QHBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    auto *timeline = new QtTimelinePanel(m_service, centralWidget);
    auto *splitter = new QSplitter(Qt::Horizontal, centralWidget);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
    splitter->addWidget(timeline);
    splitter->addWidget(m_viewport);
    centralLayout->addWidget(splitter);
    setCentralWidget(centralWidget);
    setAcceptDrops(true);
    setWindowTitle(kWindowTitle);
    resize(minimumWindowSize.x + timelineWidth, minimumWindowSize.y);
    /* The Qt shell uses the native menu bar exclusively: QtApplicationMenuBuilder implements the
       shared ApplicationMenuBuilder structure with QMenu/QAction nodes and menubarApplicationClient()
       returns nullptr so ImGuiWindow does not create its own menu bar. */
    /* Use native menu bar (default on macOS, opt-in on Linux/Windows) so QMenu dropdowns appear
       above the QOpenGLWidget; native menus use separate native windows that stack correctly. */
    menuBar()->setNativeMenuBar(false);
    m_menuBuilder = new QtApplicationMenuBuilder(m_client, m_service->eventPublisher(), m_service->fileManager(),
        m_service->translator(), m_service, menuBar(),
        ApplicationPreference(m_service).isModelEditingEnabled());
    /* The project enforces a 1080x700 minimum display size (ImGuiWindow::kMinimumMainWindowSize) while the
       native menu bar steals height from the central widget. Without compensating for it the widget's
       framebuffer is shorter than the project window and the top rows (including the ImGui main menu bar)
       are rendered outside the framebuffer and clipped. Grow the window by the menu bar height and forbid
       shrinking below that size so the framebuffer always covers the full project size. */
    layout()->activate();
    splitter->setSizes(QList<int>() << timelineWidth << minimumWindowSize.x);
    /* dragging the splitter wider must never shrink the GL widget below the project minimum:
       ImGuiWindow would clamp io.DisplaySize back up and clip the viewport chrome on the right */
    timeline->setMaximumWidth(width() - minimumWindowSize.x - splitter->handleWidth());
    if (const int menuBarHeight = menuBar()->height()) {
        resize(width(), height() + menuBarHeight);
        setMinimumSize(width(), height());
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    QObject::connect(m_timer, &QTimer::timeout, this, [this]() {
        /* dispatch on the timer so command/event processing never depends on paint scheduling
           (the offscreen platform delivers no repeated paint events, and queued commands must
           still run; the GL context is made current because loads upload GPU resources) */
        if (m_viewport->viewport3D()->makeContextCurrent()) {
            m_service->consumeDispatchAllCommandMessages();
        }
        m_client->consumeDispatchAllEventMessages();
        /* repaint both the container (overlay/parameter box children) and the GL widget itself:
           updating only the parent does not schedule paintGL() on the QOpenGLWidget child */
        m_viewport->update();
        m_viewport->viewport3D()->update();
    });
    m_timer->start();
    m_client->addDisableCursorEventListener(
        [](void *userData, const Vector2SI32 &logicalScaleCursorPosition) {
            BX_UNUSED_1(logicalScaleCursorPosition);
            auto self = static_cast<MainWindow *>(userData);
            self->m_viewport->setCursor(Qt::BlankCursor);
        },
        this, false);
    m_client->addEnableCursorEventListener(
        [](void *userData, const Vector2SI32 &logicalScaleCursorPosition) {
            auto self = static_cast<MainWindow *>(userData);
            self->m_viewport->unsetCursor();
            if (logicalScaleCursorPosition.x != 0 && logicalScaleCursorPosition.y != 0) {
                QCursor::setPos(self->m_viewport->mapToGlobal(
                    QPoint(logicalScaleCursorPosition.x, logicalScaleCursorPosition.y)));
            }
        },
        this, false);
    m_client->addQuitApplicationEventListener(
        [](void *userData) { static_cast<MainWindow *>(userData)->requestClose(); }, this, false);
}

MainWindow::~MainWindow()
{
    delete m_menuBuilder;
}

bool
MainWindow::initialize()
{
    m_service->setModalDialogParentWidget(this);
    setupBottomPanels();
    const JSON_Object *config = json_object(m_service->applicationConfiguration());
    const char *pluginDirPath = json_object_dotget_string(config, "qt.plugin.path");
    if (!pluginDirPath) {
        return false;
    }
    String pluginPath(pluginDirPath);
    pluginPath.append("/sokol_glcore33." BX_DL_EXT);
    if (!QFile::exists(QString::fromUtf8(pluginPath.c_str()))) {
        return false;
    }
    const BaseApplicationClient::InitializeMessageDescription desc(BaseApplicationService::minimumRequiredWindowSize(),
        SG_PIXELFORMAT_RGBA8, devicePixelRatioF(), pluginPath);
    m_client->addInitializationCompleteEventListener(
        [](void *userData) {
            auto self = static_cast<MainWindow *>(userData);
            self->m_client->sendActivateMessage();
            /* Re-send resize after activation so the layout matches the actual viewport widget */
            const qreal dpr = self->m_viewport->viewport3D()->devicePixelRatioF();
            self->m_client->sendResizeWindowMessage(Vector2UI32(
                static_cast<nanoem_u32_t>(self->m_viewport->viewport3D()->width() * dpr),
                static_cast<nanoem_u32_t>(self->m_viewport->viewport3D()->height() * dpr)));
            if (!self->m_fileToLoad.isEmpty()) {
                const URI fileURI(URI::createFromFilePath(String(self->m_fileToLoad.toUtf8().constData())));
                self->setTitle(fileURI);
                if (Model::isLoadableExtension(fileURI.pathExtension())) {
                    /* programmatic load: skips the interactive confirm dialog */
                    self->m_client->sendLoadFileMessage(fileURI, IFileManager::kDialogTypeLoadModelFile);
                }
                else {
                    self->m_client->sendDropFileMessage(fileURI);
                }
            }
        },
        this, true);
    m_client->sendInitializeMessage(desc);
    return true;
}

void
MainWindow::setFileToLoad(const QString &value)
{
    m_fileToLoad = value;
}

void
MainWindow::setupBottomPanels()
{
    /* Panels ported to native Qt widgets are masked out of ImGuiWindow::drawMainWindow so ImGui stops
       drawing them; everything (bottom row, timeline column, viewport HUD) now lives in native Qt
       widgets docked around the central viewport widget (outside the GL widget). */
    m_service->setNativePanelMask(internal::IUIWindow::kNativePanelTypeInterpolation |
        internal::IUIWindow::kNativePanelTypeModel | internal::IUIWindow::kNativePanelTypeBone |
        internal::IUIWindow::kNativePanelTypeCamera | internal::IUIWindow::kNativePanelTypeMorph |
        internal::IUIWindow::kNativePanelTypeLight | internal::IUIWindow::kNativePanelTypeAccessory |
        internal::IUIWindow::kNativePanelTypeView | internal::IUIWindow::kNativePanelTypePlay |
        internal::IUIWindow::kNativePanelTypeTimeline | internal::IUIWindow::kNativePanelTypeViewportHUD |
        internal::IUIWindow::kNativePanelTypeViewportParameterBox | internal::IUIWindow::kNativePanelTypeViewport);
    auto *dock = new QDockWidget(tr("Panels"), this);
    dock->setObjectName(QStringLiteral("bottomPanelDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    auto *container = new QWidget(dock);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);
    QWidget *panels[] = { new QtInterpolationPanel(m_service, container),
        new QtModelPanel(m_service, m_client, container), new QtBonePanel(m_service, container),
        new QtCameraPanel(m_service, container), new QtMorphPanel(m_service, container),
        new QtLightPanel(m_service, container), new QtAccessoryPanel(m_service, container),
        new QtViewPanel(m_service, container), new QtPlayPanel(m_service, container) };
    for (QWidget *panel : panels) {
        /* the groups compress to fit the window width like the fixed width ImGui panel children;
           the mutually exclusive ones (bone/camera, morph/light, accessory) hide themselves */
        panel->setMinimumSize(0, 0);
        panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(panel);
    }
    /* the dock must stay tall enough for the tallest panel (the play segment editors) regardless
       of how the window height is split between the viewport and the dock */
    int contentHeight = 0;
    for (QWidget *panel : panels) {
        contentHeight = qMax(contentHeight, panel->sizeHint().height());
    }
    container->setMinimumHeight(contentHeight + layout->contentsMargins().top() + layout->contentsMargins().bottom());
    layout->addStretch(1);
    dock->setWidget(container);
    /* hide the dock title bar: the panels are always visible and the dock must not be closable */
    dock->setTitleBarWidget(new QWidget(dock));
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void
MainWindow::setTitle(const URI &fileURI)
{
    if (Project::isLoadableExtension(fileURI)) {
        String title;
        StringUtils::format(title, "%s - %s", fileURI.lastPathComponentConstString(), kWindowTitle);
        setWindowTitle(QString::fromUtf8(title.c_str()));
    }
}

void
MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_shuttingDown) {
        event->accept();
        return;
    }
    event->ignore();
    requestClose();
}

void
MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
            m_client->sendDeactivateMessage();
        }
        else {
            m_client->sendActivateMessage();
        }
    }
}

void
MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void
MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            const URI fileURI(URI::createFromFilePath(String(url.toLocalFile().toUtf8().constData())));
            setTitle(fileURI);
            m_client->sendDropFileMessage(fileURI);
        }
    }
    event->acceptProposedAction();
}

void
MainWindow::requestClose()
{
    m_client->sendIsProjectDirtyRequestMessage(
        [](void *userData, bool dirty) {
            auto self = static_cast<MainWindow *>(userData);
            if (dirty) {
                QtApplicationClient *client = self->m_client;
                client->clearAllProjectAfterConfirmOnceEventListeners();
                client->addSaveProjectAfterConfirmEventListener(
                    [](void *userData) {
                        auto self = static_cast<MainWindow *>(userData);
                        self->m_client->addCompleteSavingFileEventListener(
                            [](void *userData, const URI & /* fileURI */, nanoem_u32_t /* type */,
                                nanoem_u64_t /* ticks */) { static_cast<MainWindow *>(userData)->destroyWindow(); },
                            self, true);
                        self->m_client->sendSaveProjectMessage();
                    },
                    self, true);
                client->addDiscardProjectAfterConfirmEventListener(
                    [](void *userData) { static_cast<MainWindow *>(userData)->destroyWindow(); }, self, true);
                client->sendConfirmBeforeExitApplicationMessage();
            }
            else {
                self->destroyWindow();
            }
        },
        this);
}

void
MainWindow::destroyWindow()
{
    m_client->addCompleteDestructionEventListener(
        [](void *userData) {
            auto self = static_cast<MainWindow *>(userData);
            self->m_client->addCompleteTerminationEventListener(
                [](void *userData) {
                    auto self = static_cast<MainWindow *>(userData);
                    self->m_timer->stop();
                    self->m_shuttingDown = true;
                    /* the termination event is dispatched from paintGL: defer the close so that the
                       widget is not destroyed while it is being painted */
                    QTimer::singleShot(0, self, [self]() { self->close(); });
                },
                self, true);
            self->m_client->sendTerminateMessage();
        },
        this, true);
    m_client->sendDestroyMessage();
}

} /* namespace qt */
} /* namespace nanoem */
