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
    : QOpenGLWidget(parent)
    , m_service(service)
    , m_client(client)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_service->setFrameBufferResolver([this]() -> nanoem_u32_t {
        return static_cast<nanoem_u32_t>(defaultFramebufferObject());
    });
    m_overlay = new QtViewportOverlay(m_service, this);
    m_parameterBox = new QtViewportParameterBox(m_service, this);
}

Viewport::~Viewport()
{
    m_service->setFrameBufferResolver(nullptr);
}

void
Viewport::initializeGL()
{
    /* the initialize message is sent by MainWindow::initialize() before the window is shown so that
       the command order matches the other platform shells (initialize, plugins, resize) */
}

void
Viewport::resizeGL(int w, int h)
{
    m_client->sendResizeWindowMessage(Vector2UI32(static_cast<nanoem_u32_t>(w), static_cast<nanoem_u32_t>(h)));
}

void
Viewport::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    m_overlay->setGeometry(rect());
    /* pinned at the widget bottom: ImGui's io-clipped copy lived below io.DisplaySize and rarely
       fit, while the native row is always reachable */
    const int parameterBoxHeight = m_parameterBox->height();
    m_parameterBox->setGeometry(0, height() - parameterBoxHeight, width(), parameterBoxHeight);
}

void
Viewport::paintGL()
{
    m_service->draw();
    m_service->consumeDispatchAllCommandMessages();
    m_client->consumeDispatchAllEventMessages();
}

void
Viewport::mousePressEvent(QMouseEvent *event)
{
    const Vector2SI32 position(logicalCursorPosition(event->position())),
        screenPosition(screenCursorPosition(event->globalPosition(), devicePixelRatioF()));
    const int modifiers = QtApplicationService::convertCursorModifier(event->modifiers());
    const int cursorType = QtApplicationService::convertCursorType(event->button());
    if (cursorType != 0) {
        m_client->sendScreenCursorPressMessage(screenPosition, cursorType, modifiers);
        m_client->sendCursorPressMessage(position, cursorType, modifiers);
        m_pressedCursorType = cursorType;
        m_pressedCursorModifiers = modifiers;
    }
    m_lastCursorPosition = position;
    event->accept();
}

void
Viewport::mouseReleaseEvent(QMouseEvent *event)
{
    const Vector2SI32 position(logicalCursorPosition(event->position())),
        screenPosition(screenCursorPosition(event->globalPosition(), devicePixelRatioF()));
    const int modifiers = QtApplicationService::convertCursorModifier(event->modifiers());
    const int cursorType = QtApplicationService::convertCursorType(event->button());
    if (cursorType != 0) {
        m_client->sendScreenCursorReleaseMessage(screenPosition, cursorType, modifiers);
        m_client->sendCursorReleaseMessage(position, cursorType, modifiers);
        m_pressedCursorType = 0;
        m_pressedCursorModifiers = 0;
    }
    m_lastCursorPosition = position;
    event->accept();
}

void
Viewport::mouseMoveEvent(QMouseEvent *event)
{
    const Vector2SI32 position(logicalCursorPosition(event->position())),
        delta(position - m_lastCursorPosition),
        screenPosition(screenCursorPosition(event->globalPosition(), devicePixelRatioF()));
    m_client->sendScreenCursorMoveMessage(screenPosition, m_pressedCursorType, m_pressedCursorModifiers);
    m_client->sendCursorMoveMessage(position, delta, m_pressedCursorType, m_pressedCursorModifiers);
    m_lastCursorPosition = position;
    event->accept();
}

void
Viewport::wheelEvent(QWheelEvent *event)
{
    QPoint delta(event->angleDelta());
    if (delta.isNull()) {
        delta = event->pixelDelta();
    }
    const Vector2SI32 position(logicalCursorPosition(event->position())),
        scroll(delta.x() / 120, delta.y() / 120);
    const int modifiers = QtApplicationService::convertCursorModifier(event->modifiers());
    m_client->sendCursorScrollMessage(position, scroll, modifiers);
    event->accept();
}

void
Viewport::keyPressEvent(QKeyEvent *event)
{
    /* auto-repeat only produces text input like the GLFW shell (GLFW_KEY_ACTION_REPEAT is ignored) */
    if (!event->isAutoRepeat()) {
        const nanoem_u32_t key = QtApplicationService::convertKeyType(static_cast<Qt::Key>(event->key()));
        if (key != static_cast<nanoem_u32_t>(BaseApplicationService::kKeyType_UNKNOWN)) {
            m_client->sendKeyPressMessage(key);
        }
    }
    sendUnicodeInputIfAny(m_client, event);
    /* do not call QOpenGLWidget::keyPressEvent so that Tab and friends reach nanoem */
    event->accept();
}

void
Viewport::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        const nanoem_u32_t key = QtApplicationService::convertKeyType(static_cast<Qt::Key>(event->key()));
        if (key != static_cast<nanoem_u32_t>(BaseApplicationService::kKeyType_UNKNOWN)) {
            m_client->sendKeyReleaseMessage(key);
        }
    }
    event->accept();
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
    menuBar()->setNativeMenuBar(false);
    /* The Qt shell uses the native menu bar exclusively: QtApplicationMenuBuilder implements the
       shared ApplicationMenuBuilder structure with QMenu/QAction nodes and menubarApplicationClient()
       returns nullptr so ImGuiWindow does not create its own menu bar. */
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
        if (m_viewport->isValid()) {
            m_viewport->makeCurrent();
            m_service->consumeDispatchAllCommandMessages();
            m_client->consumeDispatchAllEventMessages();
            m_viewport->doneCurrent();
        }
        m_viewport->update();
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
            /* resizeGL fires while no project is current yet, so the first WINDOW_RESIZED gets
               dropped on the floor and ImGui keeps the stale creation-time display size; re-send
               it after activation so the ImGui layout matches the actual viewport widget */
            const qreal dpr = self->m_viewport->devicePixelRatioF();
            self->m_client->sendResizeWindowMessage(Vector2UI32(
                static_cast<nanoem_u32_t>(self->m_viewport->width() * dpr),
                static_cast<nanoem_u32_t>(self->m_viewport->height() * dpr)));
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
        internal::IUIWindow::kNativePanelTypeViewportParameterBox);
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
