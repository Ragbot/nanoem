/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "emapp/Allocator.h"
#include "emapp/emapp.h"

#include "MainWindow.h"

#include "bx/commandline.h"
#include "bx/os.h"
#include "whereami.h"

#if defined(NANOEM_ENABLE_LOGGING)
#include "spdlog/cfg/env.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#endif /* NANOEM_ENABLE_LOGGING */

#if defined(SOKOL_GLCORE33)
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#endif /* SOKOL_GLCORE33 */

using namespace nanoem;

namespace {

#if defined(SOKOL_GLCORE33)
/* the GL entry points are resolved at runtime by the sokol loader, so this diagnostic has to look
 * the one it needs up the same way instead of linking against libGL */
static void
logOpenGLContextInformation()
{
    enum {
        /* GL_VENDOR, GL_RENDERER and GL_VERSION, spelled out so no GL header is needed here */
        kGLVendor = 0x1F00,
        kGLRenderer = 0x1F01,
        kGLVersion = 0x1F02
    };
    typedef const unsigned char *(*GetStringProc)(unsigned int);
    GetStringProc getString = reinterpret_cast<GetStringProc>(dlsym(RTLD_DEFAULT, "glGetString"));
    if (!getString) {
        /* sokol loads libGL with RTLD_GLOBAL once the window exists, dlopen it as fallback */
        void *library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
        getString = library ? reinterpret_cast<GetStringProc>(dlsym(library, "glGetString")) : nullptr;
    }
    const char *kUnknown = "(unavailable)";
    const char *vendor = getString ? reinterpret_cast<const char *>(getString(kGLVendor)) : nullptr;
    const char *renderer = getString ? reinterpret_cast<const char *>(getString(kGLRenderer)) : nullptr;
    const char *version = getString ? reinterpret_cast<const char *>(getString(kGLVersion)) : nullptr;
    fprintf(stderr, "[nanoem] OpenGL vendor  : %s\n[nanoem] OpenGL renderer: %s\n[nanoem] OpenGL version : %s\n",
        vendor ? vendor : kUnknown, renderer ? renderer : kUnknown, version ? version : kUnknown);
    const bool softwareRenderer = renderer && (strstr(renderer, "llvmpipe") || strstr(renderer, "Software Rasterizer"));
    /* an NVIDIA driver being installed but not being the one GL renders with is the usual reason
     * nvidia-smi stays at 0% while the application is busy */
    const bool nvidiaDriverPresent = access("/proc/driver/nvidia/version", F_OK) == 0;
    const bool nvidiaIdle = nvidiaDriverPresent && !(renderer && strstr(renderer, "NVIDIA"));
    if (softwareRenderer || nvidiaIdle) {
        fprintf(stderr,
            "[nanoem] the viewport does not render on the NVIDIA GPU (%s),\n"
            "[nanoem] start it with __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia\n"
            "[nanoem] to render there (prefix the command line with both)\n",
            softwareRenderer ? "software rasterizer" : (renderer ? renderer : "unknown renderer"));
    }
}

/* Vsync has to be turned off at runtime rather than through sapp_desc: sokol coerces a
 * swap_interval of 0 to 1 on every platform, and a vsync'd glXSwapBuffers blocks for about
 * one second per frame whenever another window covers this one, because Xwayland stops
 * presenting a hidden surface and no vblank is ever delivered. That froze the whole frame
 * loop (viewport and video export) while e.g. a browser video played on top. The same
 * sequence sokol performs for swap_interval 1 is issued here with 0 once the GLX context
 * exists, and the loop is paced in software in MainWindow::draw() instead. */
static void
disableVsync()
{
    typedef void *(*GetCurrentDisplayProc)(void);            /* Display* */
    typedef unsigned long (*GetCurrentDrawableProc)(void);   /* GLXDrawable is an XID */
    typedef void (*SwapIntervalEXTProc)(void *, unsigned long, int); /* glXSwapIntervalEXT */
    typedef int (*SwapIntervalMESAPROC)(unsigned int);       /* glXSwapIntervalMESA */
    void *library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!library) {
        library = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!library) {
        return;
    }
    auto getCurrentDisplay = reinterpret_cast<GetCurrentDisplayProc>(dlsym(library, "glXGetCurrentDisplay"));
    auto getCurrentDrawable = reinterpret_cast<GetCurrentDrawableProc>(dlsym(library, "glXGetCurrentDrawable"));
    void *display = getCurrentDisplay ? getCurrentDisplay() : nullptr;
    unsigned long drawable = getCurrentDrawable ? getCurrentDrawable() : 0;
    if (!display || !drawable) {
        return;
    }
    /* the display must be the one owning the drawable, so resolve it from the current context
     * exactly like sokol does instead of opening a second connection to the same server */
    if (auto swapIntervalEXT = reinterpret_cast<SwapIntervalEXTProc>(dlsym(library, "glXSwapIntervalEXT"))) {
        swapIntervalEXT(display, drawable, 0);
    }
    else if (auto swapIntervalMESA = reinterpret_cast<SwapIntervalMESAPROC>(dlsym(library, "glXSwapIntervalMESA"))) {
        swapIntervalMESA(0);
    }
}
#endif /* SOKOL_GLCORE33 */

class ApplicationState NANOEM_DECL_SEALED : private NonCopyable {
public:
    ApplicationState(int argc, char *argv[]);
    ~ApplicationState();

    void initialize();

    sapp::MainWindow *window() NANOEM_DECL_NOEXCEPT;

private:
    JSON_Value *m_config;
    sapp::MainWindow *m_window;
    bx::CommandLine m_command;
};

ApplicationState::ApplicationState(int argc, char *argv[])
    : m_config(nullptr)
    , m_window(nullptr)
    , m_command(argc, argv)
{
    m_config = json_value_init_object();
}

ApplicationState::~ApplicationState()
{
    delete m_window;
    m_window = nullptr;
    json_value_free(m_config);
    m_config = nullptr;
}

void
ApplicationState::initialize()
{
    char localeBuffer[32] = { 0 };
    uint32_t localeSize = sizeof(localeBuffer);
    bx::getEnv(localeBuffer, &localeSize, "LANG");
    char executablePathBuffer[1024] = { 0 };
    int executableDirPathSize;
    wai_getExecutablePath(executablePathBuffer, sizeof(executablePathBuffer), &executableDirPathSize);
    MutableString executableDirPath(executablePathBuffer, executablePathBuffer + executableDirPathSize);
    executableDirPath.push_back(0);
    FileUtils::canonicalizePathSeparator(executableDirPath);
    String pluginDirPath(executableDirPath.data());
    pluginDirPath.append("/plugins");
    String effectPluginPath(pluginDirPath);
    effectPluginPath.append("/plugin_effect." BX_DL_EXT);
    bx::FilePath tempPath(bx::Dir::Temp);
    const String configurationPath = FileUtils::userConfigurationFilePath();
    if (FileUtils::exists(configurationPath.c_str())) {
        JSON_Value *loaded = json_parse_file(configurationPath.c_str());
        if (loaded && json_value_get_type(loaded) == JSONObject) {
            json_value_free(m_config);
            m_config = loaded;
        }
        else if (loaded) {
            json_value_free(loaded);
        }
    }
    JSON_Object *root = json_value_get_object(m_config);
    json_object_dotset_string(root, "sapp.config.path", configurationPath.c_str());
    json_object_dotset_string(root, "sapp.executable.path", executablePathBuffer);
    json_object_dotset_string(root, "sapp.plugin.path", pluginDirPath.c_str());
    json_object_dotset_string(root, "plugin.effect.path", effectPluginPath.c_str());
    json_object_dotset_string(root, "project.tmp.path", tempPath.getCPtr());
    json_object_dotset_string(root, "project.locale", localeBuffer);
    m_window = new sapp::MainWindow(&m_command, m_config);
    m_window->initialize();
}

sapp::MainWindow *
ApplicationState::window() NANOEM_DECL_NOEXCEPT
{
    return m_window;
}

static inline nanoem_f32_t
inverseDevicePixelRatio() NANOEM_DECL_NOEXCEPT
{
    return 1.0f / glm::max(sapp_dpi_scale(), 1.0f);
}

static inline Vector2SI32
logicalCursorPosition(const sapp_event *event) NANOEM_DECL_NOEXCEPT
{
    return Vector2SI32(Vector2(event->mouse_x, event->mouse_y) * inverseDevicePixelRatio());
}

} /* namespace anonymous */

sapp_desc
sokol_main(int argc, char *argv[])
{
    Allocator::initialize();
#if defined(NANOEM_ENABLE_LOGGING)
    spdlog::stdout_color_mt("emapp");
    spdlog::cfg::load_env_levels();
#endif /* NANOEM_ENABLE_LOGGING */
    auto state = new ApplicationState(argc, argv);
    sapp_desc desc = {};
    desc.user_data = state;
    desc.init_userdata_cb = [](void *userData) {
        auto state = static_cast<ApplicationState *>(userData);
        BaseApplicationService::setup();
#if defined(SOKOL_GLCORE33)
        /* the GL implementation decides where the viewport is rendered, and therefore where the
         * frames the video exporter reads back come from */
        logOpenGLContextInformation();
        /* sokol has already applied swap_interval 1 to this drawable, undo it */
        disableVsync();
#endif /* SOKOL_GLCORE33 */
        state->initialize();
    };
    desc.event_userdata_cb = [](const sapp_event *event, void *userData) {
        auto state = static_cast<ApplicationState *>(userData);
        auto window = state->window();
        switch (event->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN: {
            window->handleMouseDown(logicalCursorPosition(event), event->mouse_button, event->modifiers);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_MOVE: {
            window->handleMouseMove(logicalCursorPosition(event), event->modifiers);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_UP: {
            window->handleMouseUp(logicalCursorPosition(event), event->mouse_button, event->modifiers);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_SCROLL: {
            window->handleMouseScroll(event->scroll_y, event->modifiers);
            break;
        }
        case SAPP_EVENTTYPE_KEY_DOWN: {
            window->handleKeyDown(event->key_code);
            break;
        }
        case SAPP_EVENTTYPE_KEY_UP: {
            window->handleKeyUp(event->key_code);
            break;
        }
        case SAPP_EVENTTYPE_CHAR: {
            window->handleChar(event->char_code);
            break;
        }
        case SAPP_EVENTTYPE_RESIZED: {
            const Vector2UI32 size(
                Vector2(event->framebuffer_width, event->framebuffer_height) * inverseDevicePixelRatio());
            window->handleResize(size);
            break;
        }
        case SAPP_EVENTTYPE_ICONIFIED: {
            window->handleIconify();
            break;
        }
        case SAPP_EVENTTYPE_SUSPENDED: {
            window->handleSuspend();
            break;
        }
        case SAPP_EVENTTYPE_RESTORED: {
            window->handleRestore();
            break;
        }
        case SAPP_EVENTTYPE_RESUMED: {
            window->handleResume();
            break;
        }
        case SAPP_EVENTTYPE_FILES_DROPPED: {
            const char *path = sapp_get_dropped_file_path(0);
            /* an X11 drop may yield no usable entry (malformed URI list, dropped directory, ...)
             * and sokol then reports an empty path; ignore it instead of loading an empty URI */
            if (path && path[0] != '\0') {
                window->handleFileDrop(path);
            }
            break;
        }
        case SAPP_EVENTTYPE_QUIT_REQUESTED: {
            window->handleQuitRequest();
            break;
        }
        case SAPP_EVENTTYPE_FOCUSED: {
            /* Not every window manager emits SAPP_EVENTTYPE_RESTORED when an iconified
             * window is shown again (X11 WM_STATE NormalState), which would leave the
             * project deactivated and drawDefaultPass() skipping every frame forever.
             * Re-activating on focus guarantees the viewport starts rendering again. */
            window->handleRestore();
            break;
        }
        case SAPP_EVENTTYPE_UNFOCUSED: {
            /* Deliberately does NOT deactivate: losing input focus (another window on top)
             * must not stop rendering or interrupt a running video export. */
            break;
        }
        default:
            break;
        }
    };
    desc.frame_userdata_cb = [](void *userData) {
        auto state = static_cast<ApplicationState *>(userData);
        auto window = state->window();
        window->draw();
    };
    desc.cleanup_userdata_cb = [](void *userData) {
        auto state = static_cast<ApplicationState *>(userData);
        delete state;
        Allocator::destroy();
    };
    desc.window_title = "nanoem";
    /* Requesting no vsync; note that sokol coerces 0 back to 1, so the swap interval is
     * actually set to 0 from the init callback above (see disableVsync()): a vsync'd
     * glXSwapBuffers blocks for ~1s per frame whenever another window covers this one,
     * because Xwayland stops presenting a hidden surface and no vblank is delivered. That
     * froze the frame loop (viewport and video export) while e.g. a browser video played on
     * top. The loop is paced in software instead, see MainWindow::draw(). */
    desc.swap_interval = 0;
    desc.html5_canvas_resize = true;
    desc.html5_premultiplied_alpha = true;
    desc.html5_ask_leave_site = true;
    desc.high_dpi = true;
    desc.enable_dragndrop = true;
    return desc;
}
