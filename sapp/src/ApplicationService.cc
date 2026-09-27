/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "ApplicationService.h"

#include "emapp/IProjectHolder.h"
#include "emapp/StringUtils.h"
#include "sokol/sokol_app.h"

#include "emapp/ApplicationPreference.h"
#include "emapp/BaseAudioPlayer.h"
#include "emapp/Error.h"
#include "emapp/internal/OpenGLComputeShaderSkinDeformerFactory.h"
#include "emapp/internal/OpenGLTransformFeedbackSkinDeformerFactory.h"
#include "emapp/private/CommonInclude.h"

#include "sokol/sokol_audio.h"
#include <atomic>

#if defined(__linux__)
#include <alsa/asoundlib.h>
#include <stdlib.h>
#endif /* __linux__ */

namespace nanoem {
namespace sapp {
namespace {

static const uint64_t kClockLatencyThresholdFPSCount = 10;

struct AudioPlayer : BaseAudioPlayer {
    static const uint32_t kSmoothnessScaleFactor = 16;

    AudioPlayer(BaseApplicationService *application)
        : BaseAudioPlayer()
        , m_application(application)
        , m_offset(0)
        , m_volumeGain(1.0f)
    {
    }

    bool
    initialize(nanoem_frame_index_t /* duration */, nanoem_u32_t sampleRate, Error &error) NANOEM_DECL_OVERRIDE
    {
        nanoem_u32_t actualSampleRate = sampleRate * kSmoothnessScaleFactor;
        initializeDescription(8, 1, actualSampleRate, m_linearPCMSamples.size(), m_description);
        m_currentRational.m_denominator = m_description.m_formatData.m_sampleRate;
        m_state.first = kStopState;
        return !error.hasReason();
    }
    void
    expandDuration(nanoem_frame_index_t /* frameIndex */) NANOEM_DECL_OVERRIDE
    {
        if (isPlaying()) {
            Error error;
            internalTransitStateStopped(error);
        }
    }
    void
    destroy() NANOEM_DECL_OVERRIDE
    {
        saudio_shutdown();
    }
    bool
    loadAllLinearPCMSamples(const nanoem_u8_t *data, size_t size, Error &error) NANOEM_DECL_OVERRIDE
    {
        m_loaded = true;
        m_linearPCMSamples.assign(data, data + size);
        m_currentRational.m_denominator = m_description.m_formatData.m_sampleRate;
        m_durationRational.m_numerator = size;
        m_durationRational.m_denominator = m_description.m_formatData.m_bytesPerSecond;
        m_offset = 0;
        return !error.hasReason();
    }
    void
    playPart(double /* start */, double /* length */) NANOEM_DECL_OVERRIDE
    {
    }
    void
    update() NANOEM_DECL_OVERRIDE
    {
        const Format &format = m_description.m_formatData;
        const nanoem_u64_t audioSampleOffset = static_cast<nanoem_u64_t>(m_offset / format.m_bytesPerPacket),
                           clockSampleOffset = static_cast<nanoem_u64_t>(m_clock.seconds() * format.m_sampleRate),
                           clockSampleLatencyThreshold = format.m_sampleRate / kClockLatencyThresholdFPSCount,
                           clockSampleLatency =
                               (clockSampleOffset > audioSampleOffset ? clockSampleOffset - audioSampleOffset
                                                                      : audioSampleOffset - clockSampleOffset) *
            (audioSampleOffset > 0);
        m_lastRational = m_currentRational;
        if (clockSampleLatency < clockSampleLatencyThreshold) {
            m_currentRational.m_numerator = clockSampleOffset;
        }
        else {
            m_currentRational.m_numerator = audioSampleOffset + clockSampleLatencyThreshold;
        }
    }
    void
    seek(const Rational &value) NANOEM_DECL_OVERRIDE
    {
        Error error;
        value.m_numerator > 0 ? internalTransitStatePaused(error) : internalTransitStateStopped(error);
        m_finished = false;
    }
    void
    internalSetVolumeGain(float value, Error & /* error */) NANOEM_DECL_OVERRIDE
    {
        m_volumeGain = glm::clamp(value, 0.0f, 1.0f);
    }
    void
    internalTransitStateStarted(Error & /* error */) NANOEM_DECL_OVERRIDE
    {
        startAudioPlayer();
        m_clock.start();
    }
    void
    internalTransitStatePaused(Error & /* error */) NANOEM_DECL_OVERRIDE
    {
        saudio_shutdown();
        m_clock.pause();
    }
    void
    internalTransitStateResumed(Error & /* error */) NANOEM_DECL_OVERRIDE
    {
        startAudioPlayer();
        m_clock.resume();
    }
    void
    internalTransitStateStopped(Error & /* error */) NANOEM_DECL_OVERRIDE
    {
        saudio_shutdown();
        m_clock.stop();
        m_offset = 0;
    }

    void
    startAudioPlayer()
    {
        if (!m_linearPCMSamples.empty()) {
            const Format &format = m_description.m_formatData;
            saudio_desc desc = {};
            desc.stream_userdata_cb = [](float *buffer, int numFrames, int numChannels, void *userData) {
                auto self = static_cast<AudioPlayer *>(userData);
                const Format &format = self->m_description.m_formatData;
                const uint64_t offset = self->m_offset;
                const int numSamples = numFrames * numChannels, numBytes = numSamples * (format.m_bitsPerSample / 8);
                if (offset + numBytes <= self->m_linearPCMSamples.size()) {
                    const nanoem_f32_t volumeGain = self->m_volumeGain;
                    switch (format.m_bitsPerSample) {
                    case 32: {
                        auto data = reinterpret_cast<const nanoem_i32_t *>(&self->m_linearPCMSamples[offset]);
                        for (int i = 0; i < numSamples; i++) {
                            buffer[i] = (data[i] / 2147483647.0f) * volumeGain;
                        }
                        break;
                    }
                    case 24: {
                        auto data = &self->m_linearPCMSamples[offset];
                        for (int i = 0; i < numSamples; i++) {
                            buffer[i] = (Inline::readI24(&data[i * 3]) / 8388607.0f) * volumeGain;
                        }
                        break;
                    }
                    case 16: {
                        auto data = reinterpret_cast<const nanoem_i16_t *>(&self->m_linearPCMSamples[offset]);
                        for (int i = 0; i < numSamples; i++) {
                            buffer[i] = (data[i] / 32767.0f) * volumeGain;
                        }
                        break;
                    }
                    case 8: {
                        auto data = reinterpret_cast<const nanoem_u8_t *>(&self->m_linearPCMSamples[offset]);
                        for (int i = 0; i < numSamples; i++) {
                            buffer[i] = (data[i] / 255.0f) * volumeGain;
                        }
                        break;
                    }
                    default:
                        for (int i = 0; i < numSamples; i++) {
                            buffer[i] = 0.0f;
                        }
                        break;
                    }
                    self->m_offset += numBytes;
                }
            };
            desc.user_data = this;
            desc.num_channels = format.m_numChannels;
            desc.sample_rate = format.m_sampleRate;
            m_deviceName = resolveAudioOutputDevice();
            desc.device_name = m_deviceName.c_str();
            saudio_setup(&desc);
        }
    }

    String
    resolveAudioOutputDevice() const
    {
        static const char kAudioOutputDeviceKey[] = "application.preference.audio.output-device";
        if (const JSON_Object *pending = json_object(m_application->applicationPendingChangeConfiguration())) {
            if (const char *value = json_object_dotget_string(pending, kAudioOutputDeviceKey)) {
                if (value[0] != '\0') {
                    return String(value);
                }
            }
        }
        if (const JSON_Object *config = json_object(m_application->applicationConfiguration())) {
            if (const char *value = json_object_dotget_string(config, kAudioOutputDeviceKey)) {
                if (value[0] != '\0') {
                    return String(value);
                }
            }
        }
        return String("default");
    }

    BaseApplicationService *m_application;
    String m_deviceName;
    Clock m_clock;
    std::atomic<uint64_t> m_offset;
    std::atomic<nanoem_f32_t> m_volumeGain;
};

#if defined(__linux__)
static const char kAudioOutputDevicesKey[] = "audio.output-devices";

static bool
containsAudioOutputDevice(const JSON_Array *devices, const char *value)
{
    const size_t count = json_array_get_count(devices);
    for (size_t i = 0; i < count; i++) {
        const JSON_Object *item = json_array_get_object(devices, i);
        const char *name = item ? json_object_get_string(item, "name") : json_array_get_string(devices, i);
        if (name && StringUtils::equals(name, value)) {
            return true;
        }
    }
    return false;
}

static bool
isAudioOutputDeviceAvailable(const char *name)
{
    snd_pcm_t *pcm = nullptr;
    const int result = snd_pcm_open(&pcm, name, SND_PCM_STREAM_PLAYBACK, 0);
    if (result >= 0) {
        snd_pcm_close(pcm);
    }
    return result >= 0;
}

static void
appendAudioOutputDevice(JSON_Array *devices, const char *name, const char *description)
{
    if (name && name[0] != '\0' && !containsAudioOutputDevice(devices, name) && isAudioOutputDeviceAvailable(name)) {
        if (JSON_Value *value = json_value_init_object()) {
            JSON_Object *object = json_value_get_object(value);
            json_object_set_string(object, "name", name);
            json_object_set_string(object, "description", description && description[0] != '\0' ? description : name);
            json_array_append_value(devices, value);
        }
    }
}

/*
 * Enumerates the ALSA playback devices and exposes them as a JSON array of { name, description }
 * objects in the pending application configuration so that the preference dialog is able to offer
 * them for selection.
 *
 * snd_device_name_hint() reports the PCM plugins too (lavrate, samplerate, oss, upmix, ...), which
 * are converters instead of selectable output devices, so the list is built from the control
 * interface instead: every playback capable PCM device of every installed card, probed before it
 * is published so that the menu only contains devices that can actually be opened.
 *
 * The list is refreshed on every startup to keep it in sync with the devices attached to the
 * system.
 */
static void
enumerateAudioOutputDevices(JSON_Object *pending)
{
    if (!pending) {
        return;
    }
    JSON_Value *value = json_value_init_array();
    JSON_Array *devices = json_value_get_array(value);
    appendAudioOutputDevice(devices, "default", "System default output device");
    int card = -1;
    snd_card_next(&card);
    while (card >= 0) {
        char controlName[32];
        StringUtils::format(controlName, sizeof(controlName), "hw:%d", card);
        snd_ctl_t *control = nullptr;
        if (snd_ctl_open(&control, controlName, 0) >= 0) {
            snd_ctl_card_info_t *info = nullptr;
            snd_ctl_card_info_alloca(&info);
            if (snd_ctl_card_info(control, info) >= 0) {
                const char *cardId = snd_ctl_card_info_get_id(info), *cardName = snd_ctl_card_info_get_name(info);
                int device = -1;
                snd_ctl_pcm_next_device(control, &device);
                while (device >= 0) {
                    snd_pcm_info_t *pcmInfo = nullptr;
                    snd_pcm_info_alloca(&pcmInfo);
                    snd_pcm_info_set_device(pcmInfo, device);
                    snd_pcm_info_set_subdevice(pcmInfo, 0);
                    snd_pcm_info_set_stream(pcmInfo, SND_PCM_STREAM_PLAYBACK);
                    if (snd_ctl_pcm_info(control, pcmInfo) >= 0) {
                        const char *label = snd_pcm_info_get_name(pcmInfo);
                        if (!label || label[0] == '\0') {
                            label = snd_pcm_info_get_id(pcmInfo);
                        }
                        char name[128], description[256];
                        StringUtils::format(name, sizeof(name), "plughw:CARD=%s,DEV=%d", cardId, device);
                        StringUtils::format(description, sizeof(description), "%s: %s",
                            cardName && cardName[0] != '\0' ? cardName : cardId, label ? label : name);
                        appendAudioOutputDevice(devices, name, description);
                    }
                    snd_ctl_pcm_next_device(control, &device);
                }
            }
            snd_ctl_close(control);
        }
        snd_card_next(&card);
    }
    if (json_array_get_count(devices) > 0) {
        if (json_object_dotset_value(pending, kAudioOutputDevicesKey, value) != JSONSuccess) {
            /* parson does not take the ownership when the value cannot be stored */
            json_value_free(value);
        }
    }
    else {
        json_value_free(value);
    }
}
#endif /* __linux__ */

} /* namespace anonymous */

ApplicationService::ApplicationService(const JSON_Value *config, ApplicationClient::Bridge *bridge)
    : BaseApplicationService(config)
    , m_menubarApplciationClient(bridge)
    , m_bridge(bridge)
{
#if defined(__linux__)
    enumerateAudioOutputDevices(json_object(applicationPendingChangeConfiguration()));
#endif /* __linux__ */
}

ApplicationService::~ApplicationService() noexcept
{
    if (m_dllHandle) {
        bx::dlclose(m_dllHandle);
        m_dllHandle = nullptr;
    }
}

BaseApplicationClient *
ApplicationService::menubarApplicationClient()
{
    return &m_menubarApplciationClient;
}

Project::ISkinDeformerFactory *
ApplicationService::createSkinDeformerFactory()
{
    Project::ISkinDeformerFactory *factory = nullptr;
    ApplicationPreference preference(this);
    if (preference.isSkinDeformAcceleratorEnabled()) {
        switch (sg::query_backend()) {
        case SG_BACKEND_GLCORE33:
        case SG_BACKEND_GLES3: {
#if defined(_WIN32)
#if defined(NANOEM_WIN32_HAS_OPENGL)
            factory = nanoem_new(internal::OpenGLComputeShaderSkinDeformerFactory(
                reinterpret_cast<internal ::OpenGLComputeShaderSkinDeformerFactory::PFN_GetProcAddress>(
                    wglGetProcAddress)));
#endif /* NANOEM_WIN32_HAS_OPENGL */
#else
            if (void *handle = bx::dlopen("libGL.so")) {
                using PFN_glXGetProcAddress = internal::OpenGLComputeShaderSkinDeformerFactory::PFN_GetProcAddress;
                auto func = reinterpret_cast<PFN_glXGetProcAddress>(bx::dlsym(handle, "glXGetProcAddress"));
                if (!func) {
                    func = reinterpret_cast<PFN_glXGetProcAddress>(bx::dlsym(handle, "glXGetProcAddressARB"));
                }
                if (func("glDispatchCompute")) {
                    factory = nanoem_new(internal::OpenGLComputeShaderSkinDeformerFactory(func));
                }
                else if (func("glTransformFeedbackVaryings")) {
                    factory = nanoem_new(internal::OpenGLTransformFeedbackSkinDeformerFactory(func));
                }
                m_dllHandle = handle;
            }
#endif
            break;
        }
        default:
            break;
        }
    }
    return factory;
}

void
ApplicationService::draw()
{
    drawDefaultPass();
}

void
ApplicationService::consumeDispatchAllCommandMessages()
{
    Project *project = projectHolder()->currentProject();
    ByteArrayList &commands = m_bridge->m_commands;
    ByteArrayList pending;
    /* Dispatching a command may enqueue further commands: resolving the save file dialog of an
     * export pushes the execute exporting video command while the save file command is still
     * being handled. Moving the queue aside before dispatching keeps such commands pending for
     * the next frame. Clearing the queue afterwards used to drop them without dispatching them,
     * which made File > Export > Video silently do nothing after the confirmation screen. */
    pending.swap(commands);
    for (ByteArrayList::const_iterator it = pending.begin(), end = pending.end(); it != end; ++it) {
        dispatchCommandMessage(it->data(), it->size(), project, false);
    }
}

void
ApplicationService::dispatchAllEventMessages()
{
    m_menubarApplciationClient.dispatchAllEventMessages();
}

IAudioPlayer *
ApplicationService::createAudioPlayer()
{
    return nanoem_new(AudioPlayer(this));
}

bool
ApplicationService::isRendererAvailable(const char *value) const noexcept
{
    return StringUtils::equals(value, kRendererOpenGL);
}

void
ApplicationService::handleSetupGraphicsEngine(sg_desc &desc)
{
    sg_context_desc &context = desc.context;
    context.color_format = static_cast<sg_pixel_format>(sapp_color_format());
    context.depth_format = static_cast<sg_pixel_format>(sapp_depth_format());
    context.sample_count = sapp_sample_count();
    context.metal.device = sapp_metal_get_device();
    context.metal.renderpass_descriptor_cb = sapp_metal_get_renderpass_descriptor;
    context.metal.drawable_cb = sapp_metal_get_drawable;
    context.d3d11.device = sapp_d3d11_get_device();
    context.d3d11.device_context = sapp_d3d11_get_device_context();
    context.d3d11.render_target_view_cb = sapp_d3d11_get_render_target_view;
    context.d3d11.depth_stencil_view_cb = sapp_d3d11_get_depth_stencil_view;
    context.wgpu.device = sapp_wgpu_get_device();
    context.wgpu.render_view_cb = sapp_wgpu_get_render_view;
    context.wgpu.resolve_view_cb = sapp_wgpu_get_resolve_view;
    context.wgpu.depth_stencil_view_cb = sapp_wgpu_get_depth_stencil_view;
}

void
ApplicationService::sendEventMessage(const Nanoem__Application__Event *event)
{
    ByteArray bytes(sizeofEventMessage(event));
    packEventMessage(event, bytes.data());
    m_bridge->m_events.push_back(bytes);
}

} /* namespace sapp */
} /* namespace nanoem */
