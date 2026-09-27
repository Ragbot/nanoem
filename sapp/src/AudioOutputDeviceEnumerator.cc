#include "AudioOutputDeviceEnumerator.h"

#include <alsa/asoundlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <map>

#if defined(NANOEM_ENABLE_PULSEAUDIO)
#include <pulse/pulseaudio.h>
#endif

namespace nanoem {
namespace sapp {

namespace {

const int kPollIntervalMillis = 50;

/*
 * Enumerates the playback capable PCM devices of every installed card.
 *
 * snd_device_name_hint() reports the PCM plugins too (lavrate, samplerate, oss, upmix, ...), which
 * are converters instead of selectable output devices, so the list is built from the control
 * interface instead: every playback capable PCM device of every installed card, probed before it is
 * published so that the menu only contains devices that can actually be opened.
 */
void
enumerateAlsaAudioOutputDevices(AudioOutputDeviceList &devices)
{
    devices.push_back(AudioOutputDevice("default", "System default output device"));
    int card = -1;
    snd_card_next(&card);
    while (card >= 0) {
        char controlName[32];
        snprintf(controlName, sizeof(controlName), "hw:%d", card);
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
                        snprintf(name, sizeof(name), "plughw:CARD=%s,DEV=%d", cardId, device);
                        snprintf(description, sizeof(description), "%s: %s",
                            cardName && cardName[0] != '\0' ? cardName : cardId, label ? label : name);
                        snd_pcm_t *pcm = nullptr;
                        if (snd_pcm_open(&pcm, name, SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
                            snd_pcm_close(pcm);
                            bool contains = false;
                            for (const auto &device2 : devices) {
                                if (device2.m_name == name) {
                                    contains = true;
                                    break;
                                }
                            }
                            if (!contains) {
                                devices.push_back(AudioOutputDevice(name, description));
                            }
                        }
                    }
                    snd_ctl_pcm_next_device(control, &device);
                }
            }
            snd_ctl_close(control);
        }
        snd_card_next(&card);
    }
}

/*
 * ALSA device reaching the sound server, so that the samples follow the sink the sound server sends
 * its streams to.
 *
 * The raw ALSA "default" device is not guaranteed to reach the sound server at all: distributions
 * ship different alsa.conf.d defaults and some of them wire "default" straight to the hardware,
 * which would silently ignore everything the sound server was told to do. The sound server plugins
 * are therefore probed first and the system default is only used when neither of them exists.
 */
std::string
soundServerPlaybackDevice()
{
    static const char *const kSoundServerDevices[] = { "pulse", "pipewire" };
    for (size_t i = 0; i < sizeof(kSoundServerDevices) / sizeof(kSoundServerDevices[0]); i++) {
        snd_pcm_t *pcm = nullptr;
        if (snd_pcm_open(&pcm, kSoundServerDevices[i], SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
            snd_pcm_close(pcm);
            return kSoundServerDevices[i];
        }
    }
    return "default";
}

#if defined(NANOEM_ENABLE_PULSEAUDIO)

void
sleepOneMillisecond()
{
    struct timespec timespec;
    timespec.tv_sec = 0;
    timespec.tv_nsec = 1000 * 1000;
    nanosleep(&timespec, nullptr);
}

/*
 * ALSA device driving the analog output of the card identified by its numeric index, e.g.
 * "plughw:CARD=sofhdadsp,DEV=0". It is used as a last resort for the speaker and headphone outputs
 * that the sound server does not expose as a node of its own, so the id is read the very same way
 * the enumeration of the raw ALSA devices reads it.
 */
std::string
alsaCardDeviceName(int card)
{
    std::string value;
    char controlName[32];
    snprintf(controlName, sizeof(controlName), "hw:%d", card);
    snd_ctl_t *control = nullptr;
    if (snd_ctl_open(&control, controlName, 0) >= 0) {
        snd_ctl_card_info_t *info = nullptr;
        snd_ctl_card_info_alloca(&info);
        if (snd_ctl_card_info(control, info) >= 0) {
            const char *id = snd_ctl_card_info_get_id(info);
            if (id && id[0] != '\0') {
                char device[160];
                snprintf(device, sizeof(device), "plughw:CARD=%s,DEV=0", id);
                value = device;
            }
        }
        snd_ctl_close(control);
    }
    return value;
}

const int kSessionTimeoutMillis = 2000;
const int kOperationTimeoutMillis = 2000;
const int kSinkSearchTimeoutMillis = 1500;

struct Session {
    Session() : m_mainloop(nullptr), m_context(nullptr)
    {
    }

    pa_mainloop *m_mainloop;
    pa_context *m_context;
};

void
closeSession(Session &session)
{
    if (session.m_context) {
        const pa_context_state_t state = pa_context_get_state(session.m_context);
        if (state == PA_CONTEXT_READY || state == PA_CONTEXT_CONNECTING || state == PA_CONTEXT_AUTHORIZING) {
            pa_context_disconnect(session.m_context);
        }
        pa_context_unref(session.m_context);
        session.m_context = nullptr;
    }
    if (session.m_mainloop) {
        pa_mainloop_free(session.m_mainloop);
        session.m_mainloop = nullptr;
    }
}

bool
openSession(Session &session)
{
    session.m_mainloop = pa_mainloop_new();
    if (!session.m_mainloop) {
        return false;
    }
    session.m_context = pa_context_new(pa_mainloop_get_api(session.m_mainloop), "nanoem");
    if (!session.m_context) {
        closeSession(session);
        return false;
    }
    if (pa_context_connect(session.m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        closeSession(session);
        return false;
    }
    bool ready = false;
    for (int i = 0; i < kSessionTimeoutMillis; i++) {
        const pa_context_state_t state = pa_context_get_state(session.m_context);
        if (state == PA_CONTEXT_READY) {
            ready = true;
            break;
        }
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            break;
        }
        pa_mainloop_iterate(session.m_mainloop, 0, nullptr);
        sleepOneMillisecond();
    }
    if (!ready) {
        closeSession(session);
    }
    return ready;
}

void
waitForOperation(Session &session, pa_operation *operation, int timeoutMillis = kOperationTimeoutMillis)
{
    if (!operation) {
        return;
    }
    for (int i = 0; i < timeoutMillis; i++) {
        if (pa_operation_get_state(operation) != PA_OPERATION_RUNNING) {
            break;
        }
        pa_mainloop_iterate(session.m_mainloop, 0, nullptr);
        sleepOneMillisecond();
    }
    pa_operation_unref(operation);
}

void
pump(Session &session, int timeoutMillis)
{
    for (int i = 0; i < timeoutMillis; i++) {
        pa_mainloop_iterate(session.m_mainloop, 0, nullptr);
        sleepOneMillisecond();
    }
}

struct OperationResult {
    OperationResult() : m_succeeded(false)
    {
    }

    bool m_succeeded;
};

void
successCallback(pa_context * /* context */, int success, void *userdata)
{
    static_cast<OperationResult *>(userdata)->m_succeeded = success != 0;
}

/*
 * Picks the card profile the given port is played through. The active profile wins so that the
 * device the desktop currently uses is reported the way it is wired right now, otherwise the
 * available profile with the highest priority is used, which is how the desktop picks the best
 * codec of an external device too. A port is only reported through a profile that cannot create a
 * sink when no profile of the card is able to, which keeps unplugged outputs in the list without
 * ever making them the only way to reach a device.
 */
const pa_card_profile_info2 *
chooseProfileForPort(const pa_card_info *info, const pa_card_port_info *port)
{
    const pa_card_profile_info2 *available = nullptr, *any = nullptr;
    if (!port->profiles2) {
        return nullptr;
    }
    for (uint32_t i = 0; port->profiles2[i]; i++) {
        const pa_card_profile_info2 *profile = port->profiles2[i];
        if (!profile->name || profile->n_sinks == 0) {
            continue;
        }
        if (info->active_profile2 && profile == info->active_profile2) {
            return profile;
        }
        if (profile->available != 0 && (!available || profile->priority > available->priority)) {
            available = profile;
        }
        if (!any || profile->priority > any->priority) {
            any = profile;
        }
    }
    return available ? available : any;
}

/*
 * Speaker, headphone, line and headset outputs of a card are the analog outputs of the card itself.
 * They fall back to the ALSA device of the card when the sound server does not expose them, HDMI
 * outputs never do because their ALSA devices are different ones.
 */
bool
isAnalogOutputPort(int type)
{
    switch (type) {
    case PA_DEVICE_PORT_TYPE_AUX:
    case PA_DEVICE_PORT_TYPE_SPEAKER:
    case PA_DEVICE_PORT_TYPE_HEADPHONES:
    case PA_DEVICE_PORT_TYPE_LINE:
    case PA_DEVICE_PORT_TYPE_HEADSET:
    case PA_DEVICE_PORT_TYPE_ANALOG:
        return true;
    default:
        return false;
    }
}

struct Candidate {
    Candidate() : m_cardIndex(PA_INVALID_INDEX), m_priority(0), m_available(0), m_current(false)
    {
    }

    std::string m_name;
    std::string m_label;
    std::string m_cardLabel;
    std::string m_portName;
    uint32_t m_cardIndex;
    uint32_t m_priority;
    int m_available;
    bool m_current;
};

struct EnumerationResult {
    EnumerationResult() : m_defaultCard(PA_INVALID_INDEX)
    {
    }

    std::vector<Candidate> m_candidates;
    std::string m_defaultSink;
    std::string m_defaultPort;
    uint32_t m_defaultCard;
};

/* Resolves the card profile of a device stored in the configuration. */
struct CardLookup {
    CardLookup() : m_found(false), m_profileFound(false), m_alsaCard(-1), m_portType(-1)
    {
    }

    bool m_found;
    bool m_profileFound;
    std::string m_activeProfile;
    std::string m_profile;
    std::string m_port;
    int m_alsaCard;
    int m_portType;
};

/* Searches the sink exposing a given port. */
struct SinkLookup {
    std::string m_port;
    std::string m_sink;
};

struct ServerLookup {
    std::string m_defaultSink;
};

void
cardInfoCallback(pa_context * /* context */, const pa_card_info *info, int eol, void *userdata)
{
    if (eol || !info || !info->name) {
        return;
    }
    auto *result = static_cast<EnumerationResult *>(userdata);
    const char *description = pa_proplist_gets(info->proplist, PA_PROP_DEVICE_DESCRIPTION);
    const std::string cardLabel = description && description[0] != '\0' ? description : info->name;
    for (uint32_t i = 0; i < info->n_ports; i++) {
        const pa_card_port_info *port = info->ports[i];
        if (!port || !port->name || !(port->direction & PA_DIRECTION_OUTPUT)) {
            continue;
        }
        const pa_card_profile_info2 *profile = chooseProfileForPort(info, port);
        if (!profile || !profile->name) {
            continue;
        }
        Candidate candidate;
        candidate.m_name = std::string(info->name) + "|" + profile->name + "|" + port->name;
        candidate.m_label = port->description && port->description[0] != '\0' ? port->description : port->name;
        candidate.m_cardLabel = cardLabel;
        candidate.m_portName = port->name;
        candidate.m_cardIndex = info->index;
        candidate.m_priority = port->priority;
        candidate.m_available = port->available;
        result->m_candidates.push_back(candidate);
    }
}

void
serverInfoCallback(pa_context * /* context */, const pa_server_info *info, void *userdata)
{
    if (info && info->default_sink_name) {
        static_cast<EnumerationResult *>(userdata)->m_defaultSink = info->default_sink_name;
    }
}

void
defaultSinkCallback(pa_context * /* context */, const pa_sink_info *info, int eol, void *userdata)
{
    if (eol || !info || !info->name) {
        return;
    }
    auto *result = static_cast<EnumerationResult *>(userdata);
    result->m_defaultCard = info->card;
    if (info->active_port && info->active_port->name) {
        result->m_defaultPort = info->active_port->name;
    }
}

void
sinkPortCallback(pa_context * /* context */, const pa_sink_info *info, int eol, void *userdata)
{
    if (eol || !info || !info->name) {
        return;
    }
    auto *lookup = static_cast<SinkLookup *>(userdata);
    if (!lookup->m_sink.empty()) {
        return;
    }
    if (info->active_port && info->active_port->name && lookup->m_port == info->active_port->name) {
        lookup->m_sink = info->name;
        return;
    }
    for (uint32_t i = 0; i < info->n_ports; i++) {
        const pa_sink_port_info *port = info->ports[i];
        if (port && port->name && lookup->m_port == port->name) {
            lookup->m_sink = info->name;
            return;
        }
    }
}

void
defaultSinkNameCallback(pa_context * /* context */, const pa_server_info *info, void *userdata)
{
    if (info && info->default_sink_name) {
        static_cast<ServerLookup *>(userdata)->m_defaultSink = info->default_sink_name;
    }
}

void
cardInfoLookupCallback(pa_context * /* context */, const pa_card_info *info, int eol, void *userdata)
{
    if (eol || !info || !info->name) {
        return;
    }
    auto *lookup = static_cast<CardLookup *>(userdata);
    lookup->m_found = true;
    if (info->active_profile2 && info->active_profile2->name) {
        lookup->m_activeProfile = info->active_profile2->name;
    }
    if (info->profiles2) {
        for (uint32_t i = 0; info->profiles2[i]; i++) {
            const pa_card_profile_info2 *profile = info->profiles2[i];
            if (profile->name && lookup->m_profile == profile->name) {
                lookup->m_profileFound = true;
            }
        }
    }
    if (const char *value = pa_proplist_gets(info->proplist, "alsa.card")) {
        lookup->m_alsaCard = atoi(value);
    }
    for (uint32_t i = 0; i < info->n_ports; i++) {
        const pa_card_port_info *port = info->ports[i];
        if (port && port->name && lookup->m_port == port->name) {
            lookup->m_portType = static_cast<int>(port->type);
        }
    }
}

int
availabilityRank(int available)
{
    switch (available) {
    case PA_PORT_AVAILABLE_YES:
        return 2;
    case PA_PORT_AVAILABLE_UNKNOWN:
        return 1;
    default:
        return 0;
    }
}

/*
 * Devices of an external card (a Bluetooth headset exposes the very same "Headphones" port as the
 * internal sound card) are prefixed with their device description, and so is any label that would
 * otherwise be shared by more than one device, so that every entry stays readable.
 */
void
labelDevices(std::vector<Candidate> &candidates)
{
    std::map<std::string, int> labels;
    for (auto &candidate : candidates) {
        if (candidate.m_name.compare(0, 10, "alsa_card.") != 0 && !candidate.m_cardLabel.empty()) {
            candidate.m_label = candidate.m_cardLabel + ": " + candidate.m_label;
        }
        labels[candidate.m_label]++;
    }
    for (auto &candidate : candidates) {
        if (labels[candidate.m_label] > 1 && !candidate.m_cardLabel.empty()) {
            const std::string label = candidate.m_cardLabel + ": " + candidate.m_label;
            labels[label]++;
            candidate.m_label = label;
        }
    }
    labels.clear();
    for (const auto &candidate : candidates) {
        labels[candidate.m_label]++;
    }
    for (auto &candidate : candidates) {
        if (labels[candidate.m_label] > 1 && !candidate.m_portName.empty()) {
            candidate.m_label = candidate.m_label + " (" + candidate.m_portName + ")";
        }
    }
}

bool
containsDevice(const AudioOutputDeviceList &devices, const std::string &name)
{
    for (const auto &device : devices) {
        if (device.m_name == name) {
            return true;
        }
    }
    return false;
}

#endif /* NANOEM_ENABLE_PULSEAUDIO */
} /* namespace anonymous */

bool
enumerateAudioOutputDevices(AudioOutputDeviceList &devices)
{
#if defined(NANOEM_ENABLE_PULSEAUDIO)
    Session session;
    if (openSession(session)) {
        EnumerationResult result;
        waitForOperation(session, pa_context_get_card_info_list(session.m_context, cardInfoCallback, &result));
        waitForOperation(session, pa_context_get_server_info(session.m_context, serverInfoCallback, &result));
        if (!result.m_defaultSink.empty()) {
            waitForOperation(session,
                pa_context_get_sink_info_by_name(session.m_context, result.m_defaultSink.c_str(), defaultSinkCallback,
                    &result));
        }
        closeSession(session);
        if (!result.m_candidates.empty()) {
            for (auto &candidate : result.m_candidates) {
                candidate.m_current = candidate.m_cardIndex == result.m_defaultCard &&
                    !result.m_defaultPort.empty() && candidate.m_portName == result.m_defaultPort;
            }
            labelDevices(result.m_candidates);
            std::stable_sort(result.m_candidates.begin(), result.m_candidates.end(),
                [](const Candidate &lhs, const Candidate &rhs) {
                    if (lhs.m_current != rhs.m_current) {
                        return lhs.m_current;
                    }
                    const int lhsRank = availabilityRank(lhs.m_available), rhsRank = availabilityRank(rhs.m_available);
                    if (lhsRank != rhsRank) {
                        return lhsRank > rhsRank;
                    }
                    return lhs.m_priority > rhs.m_priority;
                });
            devices.push_back(AudioOutputDevice("default", "System default output device"));
            for (const auto &candidate : result.m_candidates) {
                if (!containsDevice(devices, candidate.m_name)) {
                    devices.push_back(AudioOutputDevice(candidate.m_name, candidate.m_label));
                }
            }
            return true;
        }
    }
#endif /* NANOEM_ENABLE_PULSEAUDIO */
    enumerateAlsaAudioOutputDevices(devices);
    return false;
}

std::string
applyAudioOutputDevice(const char *device)
{
    if (!device || device[0] == '\0') {
        return soundServerPlaybackDevice();
    }
    const std::string value(device);
    if (value.find('|') == std::string::npos) {
        /*
         * The system default device follows wherever the sound server sends its streams, any other
         * plain ALSA device is opened as it is so that the hardware can still be driven directly.
         */
        return value == "default" ? soundServerPlaybackDevice() : value;
    }
#if defined(NANOEM_ENABLE_PULSEAUDIO)
    const size_t cardDelimiter = value.find('|');
    const size_t profileDelimiter = value.find('|', cardDelimiter + 1);
    if (profileDelimiter == std::string::npos) {
        return soundServerPlaybackDevice();
    }
    const std::string card = value.substr(0, cardDelimiter),
                      profile = value.substr(cardDelimiter + 1, profileDelimiter - cardDelimiter - 1),
                      port = value.substr(profileDelimiter + 1);
    if (card.empty() || profile.empty() || port.empty()) {
        return soundServerPlaybackDevice();
    }
    CardLookup cardLookup;
    cardLookup.m_profile = profile;
    cardLookup.m_port = port;
    Session session;
    if (openSession(session)) {
        waitForOperation(session,
            pa_context_get_card_info_by_name(session.m_context, card.c_str(), cardInfoLookupCallback, &cardLookup));
        bool profileSwitched = false;
        if (cardLookup.m_found && cardLookup.m_profileFound && !cardLookup.m_activeProfile.empty() &&
            cardLookup.m_activeProfile != profile) {
            OperationResult result;
            waitForOperation(session,
                pa_context_set_card_profile_by_name(session.m_context, card.c_str(), profile.c_str(), successCallback,
                    &result));
            profileSwitched = result.m_succeeded;
        }
        std::string sink;
        for (int elapsed = 0; sink.empty(); elapsed += kPollIntervalMillis) {
            SinkLookup sinkLookup;
            sinkLookup.m_port = port;
            waitForOperation(session, pa_context_get_sink_info_list(session.m_context, sinkPortCallback, &sinkLookup));
            sink = sinkLookup.m_sink;
            if (sink.empty() && (!profileSwitched || elapsed >= kSinkSearchTimeoutMillis)) {
                break;
            }
            pump(session, kPollIntervalMillis);
        }
        /* the samples reach the selected sink as long as they go through the sound server */
        std::string playback(soundServerPlaybackDevice());
        if (!sink.empty()) {
            ServerLookup serverLookup;
            waitForOperation(session,
                pa_context_get_server_info(session.m_context, defaultSinkNameCallback, &serverLookup));
            if (serverLookup.m_defaultSink != sink) {
                OperationResult result;
                waitForOperation(session,
                    pa_context_set_default_sink(session.m_context, sink.c_str(), successCallback, &result));
            }
            waitForOperation(session,
                pa_context_set_sink_port_by_name(session.m_context, sink.c_str(), port.c_str(), nullptr, nullptr));
        }
        else if (cardLookup.m_alsaCard >= 0 && isAnalogOutputPort(cardLookup.m_portType)) {
            const std::string fallback = alsaCardDeviceName(cardLookup.m_alsaCard);
            if (!fallback.empty()) {
                playback = fallback;
            }
        }
        closeSession(session);
        return playback;
    }
    return soundServerPlaybackDevice();
#else
    /* the device belongs to a sound server that this build cannot talk to, follow its default sink */
    return soundServerPlaybackDevice();
#endif /* NANOEM_ENABLE_PULSEAUDIO */
}

} /* namespace sapp */
} /* namespace nanoem */
