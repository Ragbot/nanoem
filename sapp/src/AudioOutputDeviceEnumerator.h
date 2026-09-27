#ifndef NANOEM_SAPP_AUDIO_OUTPUT_DEVICE_ENUMERATOR_H
#define NANOEM_SAPP_AUDIO_OUTPUT_DEVICE_ENUMERATOR_H

#include <string>
#include <vector>

namespace nanoem {
namespace sapp {

struct AudioOutputDevice {
    AudioOutputDevice()
    {
    }
    AudioOutputDevice(const std::string &name, const std::string &description)
        : m_name(name)
        , m_description(description)
    {
    }

    /* Name stored in the configuration. Devices owned by the sound server are encoded as
     * "<card>|<profile>|<port>" so that the routing can be reproduced without guessing, plain
     * ALSA devices are their PCM names (e.g. "plughw:CARD=sofhdadsp,DEV=0"). */
    std::string m_name;
    /* Label shown by the preference dialog. */
    std::string m_description;
};

typedef std::vector<AudioOutputDevice> AudioOutputDeviceList;

/*
 * Collects every playback destination of the machine as { name, description } pairs.
 *
 * When a sound server (PipeWire or PulseAudio) is reachable, its output ports are reported the way
 * the desktop does (Speaker, Headphones, HDMI / DisplayPort, plus every profile of external devices
 * such as Bluetooth headsets) prefixed by an entry that lets the sound server keep deciding where
 * the samples go. Otherwise the list falls back to the raw ALSA playback devices of the installed
 * cards, which is the best that can be done without a sound server.
 *
 * Returns true when the sound server provided the devices.
 */
bool enumerateAudioOutputDevices(AudioOutputDeviceList &devices);

/*
 * Reproduces the routing of a device reported by enumerateAudioOutputDevices() and returns the ALSA
 * PCM the samples have to be written to.
 *
 * Plain ALSA devices are returned unchanged. Devices owned by the sound server are applied by
 * activating the card profile they belong to, selecting their port and making the sink that exposes
 * it the default one, so that the samples played through the system default ALSA device reach the
 * requested destination. When the sound server does not expose the requested output at all (the
 * analog output of a card is typically missing) the playback falls back to the corresponding ALSA
 * device directly, which keeps the requested device working.
 */
std::string applyAudioOutputDevice(const char *device);

} /* namespace sapp */
} /* namespace nanoem */

#endif
