/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#define NOMINMAX
#include <algorithm>
#include <vector>

#include "emapp/sdk/Decoder.h"
#include "emapp/sdk/Encoder.h"
#include "emapp/sdk/UI.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}

#include "emapp/src/protoc/common.pb-c.c"
#include "emapp/src/protoc/plugin.pb-c.c"

namespace {

using namespace nanoem::application::plugin;

static void
handleFFmpegLog(void *ptr, int level, const char *fmt, va_list vl)
{
#if !defined(NDEBUG)
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, vl);
#if defined(_WIN32)
    OutputDebugStringA(buffer);
#else
    (void) ptr;
    (void) level;
    fputs(buffer, stderr);
#endif
#else
    av_log_default_callback(ptr, level, fmt, vl);
#endif
}

static const AVCodec *
findEncoderByName(const char *const *names, nanoem_rsize_t length)
{
    for (nanoem_rsize_t i = 0; i < length; i++) {
        if (const AVCodec *codec = avcodec_find_encoder_by_name(names[i])) {
            return codec;
        }
    }
    return nullptr;
}

/* the registry order of avcodec_find_encoder() differs between ffmpeg builds, so resolve the
 * lossy codecs by their actual encoder name first and only fall back to the registry */
static const AVCodec *
findVideoEncoder(AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_AV1: {
        static const char *kNames[] = { "libaom-av1", "libsvtav1", "librav1e" };
        if (const AVCodec *codec = findEncoderByName(kNames, sizeof(kNames) / sizeof(kNames[0]))) {
            return codec;
        }
        break;
    }
    case AV_CODEC_ID_H264: {
        static const char *kNames[] = { "libx264" };
        if (const AVCodec *codec = findEncoderByName(kNames, sizeof(kNames) / sizeof(kNames[0]))) {
            return codec;
        }
        break;
    }
    default:
        break;
    }
    return avcodec_find_encoder(id);
}

static AVSampleFormat
preferredSampleFormat(const AVCodec *codec, AVSampleFormat fallback)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    /* AVCodec::sample_fmts was replaced by avcodec_get_supported_config() in ffmpeg 7.1 */
    const void *configs = nullptr;
    int numConfigs = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &numConfigs) >= 0 &&
        configs && numConfigs > 0) {
        const AVSampleFormat *formats = static_cast<const AVSampleFormat *>(configs);
        if (formats[0] != AV_SAMPLE_FMT_NONE) {
            return formats[0];
        }
    }
#else
    nanoem_mark_unused(codec);
#endif
    return fallback;
}

/* the containers that can store each video codec (verified against ffmpeg's muxers); raw RGB
 * video is rejected by both Matroska and MP4, and UT Video has no MP4 sample entry */
static const char *const *
containerExtensionsFor(AVCodecID id, nanoem_u32_t *length)
{
    static const char *kMP4AndMKVAndAVI[] = { "mp4", "mkv", "avi" };
    static const char *kMKVAndAVI[] = { "mkv", "avi" };
    static const char *kAVI[] = { "avi" };
    switch (id) {
    case AV_CODEC_ID_RAWVIDEO:
        *length = sizeof(kAVI) / sizeof(kAVI[0]);
        return kAVI;
    case AV_CODEC_ID_UTVIDEO:
        *length = sizeof(kMKVAndAVI) / sizeof(kMKVAndAVI[0]);
        return kMKVAndAVI;
    default:
        *length = sizeof(kMP4AndMKVAndAVI) / sizeof(kMP4AndMKVAndAVI[0]);
        return kMP4AndMKVAndAVI;
    }
}

static const char *
availableContainerExtensions(AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_RAWVIDEO:
        return "avi";
    case AV_CODEC_ID_UTVIDEO:
        return "mkv, avi";
    default:
        return "mp4, mkv, avi";
    }
}

struct FFmpegEncoder {
    static const char kAudioCodecComponentID[];
    static const char kVideoCodecComponentID[];
    static const char kVideoPixelFormatComponentID[];
    static const int kMinimumSampleRate;
    static const int kMinimumNumChannels;

    FFmpegEncoder()
        : m_formatContext(nullptr)
        , m_audioStream(nullptr)
        , m_videoStream(nullptr)
        , m_audioCodecContext(nullptr)
        , m_videoCodecContext(nullptr)
        , m_resampleContext(nullptr)
        , m_scaleContext(nullptr)
        , m_tempAudioBuffer(nullptr)
        , m_audioCodecID(AV_CODEC_ID_PCM_S16LE)
        , m_videoCodecID(AV_CODEC_ID_AV1)
        , m_videoPixelFormat(AV_PIX_FMT_YUV420P)
        , m_videoCodecIndex(0)
        , m_videoPixelFormatIndex(0)
        , m_fps(0)
        , m_duration(0)
        , m_numFrequency(0)
        , m_numChannels(0)
        , m_numBits(0)
        , m_width(0)
        , m_height(0)
        , m_yflip(0)
        , m_nextAudioPTS(0)
        , m_failed(false)
    {
        *m_reason = 0;
    }
    ~FFmpegEncoder()
    {
        clearUIWindowLayout();
    }

    int
    openAudioCodec()
    {
        const AVCodec *codec = avcodec_find_encoder(m_audioCodecID);
        if (!codec) {
            setFailureReason("The selected audio codec is not available in this FFmpeg build");
            return AVERROR(ENOSYS);
        }
        if ((m_audioStream = avformat_new_stream(m_formatContext, codec)) != nullptr) {
            m_audioCodecContext = avcodec_alloc_context3(codec);
            m_audioCodecContext->sample_fmt = preferredSampleFormat(codec, AV_SAMPLE_FMT_S16);
            m_audioCodecContext->sample_rate = std::max(m_numFrequency, kMinimumSampleRate);
            av_channel_layout_default(&m_audioCodecContext->ch_layout, std::max(m_numChannels, kMinimumNumChannels));
            m_audioCodecContext->time_base.num = 1;
            m_audioCodecContext->time_base.den = m_audioCodecContext->sample_rate;
            AVSampleFormat inputSampleFormat;
            switch (m_numBits) {
            case 32: {
                inputSampleFormat = AV_SAMPLE_FMT_S32;
                break;
            }
            case 24: {
                /* needs conversion S24 to FLT */
                inputSampleFormat = AV_SAMPLE_FMT_FLT;
                break;
            }
            case 16: {
                inputSampleFormat = AV_SAMPLE_FMT_S16;
                break;
            }
            case 8: {
                inputSampleFormat = AV_SAMPLE_FMT_U8;
                break;
            }
            default:
                inputSampleFormat = AV_SAMPLE_FMT_S16;
                break;
            }
            if ((m_formatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
                m_audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            AVChannelLayout inputChannelLayout;
            av_channel_layout_default(&inputChannelLayout, m_numChannels);
            int rc = swr_alloc_set_opts2(&m_resampleContext, &m_audioCodecContext->ch_layout,
                m_audioCodecContext->sample_fmt, m_audioCodecContext->sample_rate, &inputChannelLayout,
                inputSampleFormat, m_numFrequency, 0, nullptr);
            if (rc == 0) {
                rc = swr_init(m_resampleContext);
            }
            if (rc < 0) {
                return rc;
            }
        }
        if (!m_audioCodecContext) {
            return AVERROR(ENOMEM);
        }
        int rc = avcodec_open2(m_audioCodecContext, codec, nullptr);
        if (rc == 0 && m_audioStream) {
            rc = avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecContext);
        }
        return rc;
    }
    int
    openVideoCodec()
    {
        const AVCodec *codec = findVideoEncoder(m_videoCodecID);
        if (!codec) {
            setFailureReason("The selected video codec is not available in this FFmpeg build");
            return AVERROR(ENOSYS);
        }
        if ((m_videoStream = avformat_new_stream(m_formatContext, codec)) != nullptr) {
            m_videoCodecContext = avcodec_alloc_context3(codec);
            m_videoCodecContext->width = static_cast<int>(m_width);
            m_videoCodecContext->height = static_cast<int>(m_height);
            m_videoCodecContext->pix_fmt = m_videoPixelFormat;
            m_videoCodecContext->time_base.num = 1;
            m_videoCodecContext->time_base.den = static_cast<int>(m_fps);
            m_videoCodecContext->framerate = av_make_q(static_cast<int>(m_fps), 1);
            if ((m_formatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
                m_videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            if (m_videoCodecID == AV_CODEC_ID_AV1 || m_videoCodecID == AV_CODEC_ID_H264) {
                /* without an explicit bit rate both libaom-av1 and libx264 fall back to a quality
                 * mode that either ignores the requested size or produces effectively lossless
                 * output, so derive ~0.1 bits per pixel from the configured frame rate */
                const nanoem_u64_t fps = m_fps > 0 ? m_fps : 60;
                const nanoem_u64_t bitRate =
                    std::max<nanoem_u64_t>(static_cast<nanoem_u64_t>(m_width) * m_height * fps / 10, 200000);
                m_videoCodecContext->bit_rate = static_cast<int64_t>(bitRate);
                m_videoCodecContext->gop_size = static_cast<int>(std::min<nanoem_u64_t>(fps, 300));
            }
            /* nanoem hands encoder plugins the viewport as BGRA on every platform */
            const AVPixelFormat sourcePixelFormat = AV_PIX_FMT_BGRA;
            m_scaleContext = sws_getContext(m_width, m_height, sourcePixelFormat, m_width, m_height,
                m_videoCodecContext->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
        }
        if (!m_videoCodecContext) {
            return AVERROR(ENOMEM);
        }
        int rc = avcodec_open2(m_videoCodecContext, codec, nullptr);
        if (rc == 0 && m_videoStream) {
            rc = avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecContext);
        }
        return rc;
    }

    int
    open(const char *filePath, nanoem_application_plugin_status_t *status)
    {
        bool succeeded = true;
        *m_reason = 0;
        /* the encoder object may be reused for multiple exports */
        m_nextAudioPTS = 0;
        m_failed = false;
        const int rc = avformat_alloc_output_context2(&m_formatContext, nullptr, nullptr, filePath);
        if (rc < 0 || !m_formatContext) {
            if (m_formatContext) {
                avformat_free_context(m_formatContext);
                m_formatContext = nullptr;
            }
            setFailureReason("The container cannot be derived from the output file name");
            if (status) {
                *status = NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_REFER_REASON;
            }
            return 0;
        }
        if (m_numChannels > 0 && m_numFrequency > 0) {
            succeeded = wrapCall(openAudioCodec(), status);
        }
        succeeded = succeeded && wrapCall(openVideoCodec(), status) &&
            wrapCall(avio_open(&m_formatContext->pb, filePath, AVIO_FLAG_WRITE), status);
        if (succeeded && !wrapCall(avformat_write_header(m_formatContext, nullptr), status)) {
            avio_closep(&m_formatContext->pb);
            succeeded = false;
        }
        if (!succeeded) {
            avformat_free_context(m_formatContext);
            m_formatContext = nullptr;
        }
        return succeeded ? 1 : 0;
    }
    void
    setOption(nanoem_u32_t key, const void *value, nanoem_u32_t size, nanoem_application_plugin_status_t *status)
    {
        switch (key) {
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_FPS: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_fps = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_LOCATION: {
            /* unused */
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_DURATION: {
            if (Inline::validateArgument<nanoem_frame_index_t>(value, size, status)) {
                m_duration = *static_cast<const nanoem_frame_index_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_AUDIO_NUM_FREQUENCY: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_numFrequency = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_AUDIO_NUM_CHANNELS: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_numChannels = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_AUDIO_NUM_BITS: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_numBits = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_VIDEO_WIDTH: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_width = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_VIDEO_HEIGHT: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_height = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_ENCODER_OPTION_VIDEO_YFLIP: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_yflip = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        default:
            nanoem_application_plugin_status_assign_error(
                status, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_UNKNOWN_OPTION);
            break;
        }
    }
    void
    drainPackets(AVCodecContext *context, AVStream *stream, nanoem_application_plugin_status_t *status)
    {
        AVPacket *packet = av_packet_alloc();
        if (!packet) {
            return;
        }
        int rc = 0;
        if (context && stream && m_formatContext) {
            while ((rc = avcodec_receive_packet(context, packet)) == 0) {
                av_packet_rescale_ts(packet, context->time_base, stream->time_base);
                packet->stream_index = stream->index;
                if (packet->duration <= 0) {
                    /* flushed packets from some encoders carry no duration; leaving it at zero
                     * makes the MP4 muxer derive a track duration that is one frame short, which
                     * in turn drops the final frame when the file is played back */
                    packet->duration = av_rescale_q(1, context->time_base, stream->time_base);
                }
                const int writeRC = av_interleaved_write_frame(m_formatContext, packet);
                av_packet_unref(packet);
                if (writeRC < 0) {
                    rc = writeRC;
                    break;
                }
            }
        }
        av_packet_free(&packet);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            rc = 0;
        }
        if (rc < 0) {
            m_failed = true;
            makeFailureReason(rc, status);
        }
    }
    void
    drainAudioPackets(nanoem_application_plugin_status_t *status)
    {
        drainPackets(m_audioCodecContext, m_audioStream, status);
    }
    void
    drainVideoPackets(nanoem_application_plugin_status_t *status)
    {
        drainPackets(m_videoCodecContext, m_videoStream, status);
    }
    void
    flushCodec(AVCodecContext *context, AVStream *stream)
    {
        if (context && stream) {
            avcodec_send_frame(context, nullptr);
            drainPackets(context, stream, nullptr);
        }
    }
    void
    encodeAudioFrame(nanoem_frame_index_t /* currentFrameIndex */, const nanoem_u8_t *data, nanoem_u32_t size,
        nanoem_application_plugin_status_t *status)
    {
        int inputSampleCount = size / (m_numChannels * (m_numBits / 8)),
            outputSampleCount =
                av_rescale_rnd(inputSampleCount, m_audioCodecContext->sample_rate, m_numFrequency, AV_ROUND_UP);
        ScopedAudioFrame output(m_audioStream->codecpar, outputSampleCount, m_nextAudioPTS);
        if (!wrapCall(av_frame_get_buffer(output, 0), status) || !wrapCall(av_frame_make_writable(output), status)) {
            return;
        }
        const nanoem_u8_t *dataPtr = data;
        if (m_numBits == 24) {
            const nanoem_rsize_t numComponents = size / 3;
            if (!m_tempAudioBuffer) {
                m_tempAudioBuffer = new float[numComponents];
            }
            for (nanoem_rsize_t i = 0; i < numComponents; i++) {
                const nanoem_u8_t *ptr = &dataPtr[i * 3];
                int v = 0;
                if ((ptr[2] & 0x80) != 0) {
                    v = (0xff << 24) | (ptr[2] << 16) | (ptr[1] << 8) | ptr[0];
                }
                else {
                    v = (ptr[2] << 16) | (ptr[1] << 8) | ptr[0];
                }
                m_tempAudioBuffer[i] = v / 8388607.0f;
            }
            dataPtr = reinterpret_cast<const nanoem_u8_t *>(m_tempAudioBuffer);
        }
        const int numConverted = swr_convert(
            m_resampleContext, &output.m_opaque->data[0], outputSampleCount, &dataPtr, inputSampleCount);
        if (numConverted < 0) {
            m_failed = true;
            makeFailureReason(numConverted, status);
            return;
        }
        if (numConverted > 0) {
            output.m_opaque->nb_samples = numConverted;
            if (wrapCall(avcodec_send_frame(m_audioCodecContext, output), status)) {
                /* the timestamps advance with the samples handed to the encoder, not with the
                 * packets it emits: codecs with a delay emit them later than they are sent */
                m_nextAudioPTS += numConverted;
                drainAudioPackets(status);
            }
        }
    }
    void
    encodeVideoFrame(nanoem_frame_index_t currentFrameIndex, const nanoem_u8_t *data, nanoem_u32_t size,
        nanoem_application_plugin_status_t *status)
    {
        const AVCodecParameters *parameters = m_videoStream->codecpar;
        ScopedVideoFrame frame(parameters, currentFrameIndex);
        if (!wrapCall(av_frame_get_buffer(frame, 0), status) || !wrapCall(av_frame_make_writable(frame), status)) {
            return;
        }
        const nanoem_u8_t *dataPtr[] = { 0, 0, 0, 0 };
        int lineSizePtr[] = { 0, 0, 0, 0 }, stride = size / m_height;
        if (m_yflip) {
            const nanoem_u8_t *ptr = data + stride * (m_height - 1);
            lineSizePtr[0] = -stride;
            dataPtr[0] = ptr;
        }
        else {
            lineSizePtr[0] = stride;
            dataPtr[0] = data;
        }
        sws_scale(m_scaleContext, dataPtr, lineSizePtr, 0, m_height, frame.m_opaque->data, frame.m_opaque->linesize);
        int rc = avcodec_send_frame(m_videoCodecContext, frame);
        if (rc == AVERROR(EAGAIN)) {
            /* the encoder ran out of internal slots: write out what it has and retry */
            drainVideoPackets(status);
            rc = avcodec_send_frame(m_videoCodecContext, frame);
        }
        if (wrapCall(rc, status)) {
            drainVideoPackets(status);
        }
    }
    int
    interrupt(nanoem_application_plugin_status_t *status)
    {
        return close(status);
    }
    int
    close(nanoem_application_plugin_status_t *status)
    {
        if (m_formatContext) {
            /* encoders with a lookahead (AV1, H.264, ...) buffer frames and only emit the packets
             * for them when they are flushed, so drain them before writing the trailer */
            flushCodec(m_videoCodecContext, m_videoStream);
            flushCodec(m_audioCodecContext, m_audioStream);
            makeFailureReason(av_write_trailer(m_formatContext), status);
        }
        if (m_audioCodecContext) {
            avcodec_free_context(&m_audioCodecContext);
        }
        if (m_videoCodecContext) {
            avcodec_free_context(&m_videoCodecContext);
        }
        if (m_failed && status) {
            *status = NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_REFER_REASON;
        }
        if (m_resampleContext) {
            swr_close(m_resampleContext);
            swr_free(&m_resampleContext);
        }
        if (m_scaleContext) {
            sws_freeContext(m_scaleContext);
            m_scaleContext = nullptr;
        }
        if (m_tempAudioBuffer) {
            delete[] m_tempAudioBuffer;
            m_tempAudioBuffer = nullptr;
        }
        if (m_formatContext) {
            makeFailureReason(avio_closep(&m_formatContext->pb), status);
            avformat_free_context(m_formatContext);
            m_formatContext = nullptr;
        }
        return 1;
    }
    void
    buildSelectableVideoCodecs()
    {
        struct Candidate {
            const char *label;
            AVCodecID id;
        };
        static const Candidate kCandidates[] = {
            { "AV1", AV_CODEC_ID_AV1 },
            { "H.264", AV_CODEC_ID_H264 },
            { "Raw Video", AV_CODEC_ID_RAWVIDEO },
            { "UT Codec Video", AV_CODEC_ID_UTVIDEO },
        };
        m_videoCodecLabels.clear();
        m_videoCodecIDs.clear();
        for (nanoem_rsize_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); i++) {
            /* only offer codecs the running ffmpeg build can actually encode with */
            if (findVideoEncoder(kCandidates[i].id)) {
                m_videoCodecLabels.push_back(kCandidates[i].label);
                m_videoCodecIDs.push_back(kCandidates[i].id);
            }
        }
        if (m_videoCodecIDs.empty()) {
            m_videoCodecLabels.push_back("Raw Video");
            m_videoCodecIDs.push_back(AV_CODEC_ID_RAWVIDEO);
        }
        if (m_videoCodecIndex >= m_videoCodecIDs.size()) {
            m_videoCodecIndex = 0;
        }
    }
    void
    buildSelectablePixelFormats()
    {
        struct Candidate {
            const char *label;
            AVPixelFormat format;
        };
        static const Candidate kYUVPixelFormats[] = {
            { "YUV420P", AV_PIX_FMT_YUV420P },
            { "YUV422P", AV_PIX_FMT_YUV422P },
            { "YUV444P", AV_PIX_FMT_YUV444P },
        };
        static const Candidate kRawVideoPixelFormats[] = {
            { "RGB", AV_PIX_FMT_BGR24 },
            { "RGBA", AV_PIX_FMT_BGRA },
            { "YUV420P", AV_PIX_FMT_YUV420P },
            { "YUV422P", AV_PIX_FMT_YUV422P },
            { "YUV444P", AV_PIX_FMT_YUV444P },
        };
        static const Candidate kUTVideoPixelFormats[] = {
            { "RGB", AV_PIX_FMT_GBRP },
            { "RGBA", AV_PIX_FMT_GBRAP },
            { "YUV420P", AV_PIX_FMT_YUV420P },
            { "YUV422P", AV_PIX_FMT_YUV422P },
            { "YUV444P", AV_PIX_FMT_YUV444P },
        };
        const AVCodecID codecID = m_videoCodecIDs[m_videoCodecIndex];
        const Candidate *candidates = nullptr;
        nanoem_rsize_t numCandidates = 0;
        if (codecID == AV_CODEC_ID_AV1 || codecID == AV_CODEC_ID_H264) {
            /* both encoders only take YUV input */
            candidates = kYUVPixelFormats;
            numCandidates = sizeof(kYUVPixelFormats) / sizeof(kYUVPixelFormats[0]);
        }
        else if (codecID == AV_CODEC_ID_RAWVIDEO) {
            candidates = kRawVideoPixelFormats;
            numCandidates = sizeof(kRawVideoPixelFormats) / sizeof(kRawVideoPixelFormats[0]);
        }
        else {
            candidates = kUTVideoPixelFormats;
            numCandidates = sizeof(kUTVideoPixelFormats) / sizeof(kUTVideoPixelFormats[0]);
        }
        m_pixelFormatLabels.clear();
        m_pixelFormats.clear();
        for (nanoem_rsize_t i = 0; i < numCandidates; i++) {
            m_pixelFormatLabels.push_back(candidates[i].label);
            m_pixelFormats.push_back(candidates[i].format);
        }
        if (m_videoPixelFormatIndex >= m_pixelFormats.size()) {
            m_videoPixelFormatIndex = 0;
        }
    }
    void
    loadUIWindowLayout(nanoem_application_plugin_status_t *status)
    {
        static const char *kAudioCodecs[] = { "PCM" };
        clearUIWindowLayout();
        buildSelectableVideoCodecs();
        buildSelectablePixelFormats();
        m_videoCodecID = m_videoCodecIDs[m_videoCodecIndex];
        m_videoPixelFormat = m_pixelFormats[m_videoPixelFormatIndex];
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Container (chosen when saving): %s",
            availableContainerExtensions(m_videoCodecID));
        m_components.push_back(createLabel(buffer));
        m_components.push_back(createLabel("Video Codec"));
        m_components.push_back(createCombobox(
            kVideoCodecComponentID, m_videoCodecLabels.data(), m_videoCodecLabels.size(), m_videoCodecIndex));
        m_components.push_back(createLabel("Video Pixel Format"));
        m_components.push_back(createCombobox(kVideoPixelFormatComponentID, m_pixelFormatLabels.data(),
            m_pixelFormatLabels.size(), m_videoPixelFormatIndex));
        m_components.push_back(createLabel("Audio Codec"));
        m_components.push_back(createCombobox(kAudioCodecComponentID, kAudioCodecs, 1, 0));
        nanoem_application_plugin_status_assign_success(status);
    }
    void
    clearUIWindowLayout()
    {
        for (auto component : m_components) {
            destroyComponent(component);
        }
        m_components.clear();
    }
    nanoem_u32_t
    getUIWindowLayoutDataSize() const
    {
        Nanoem__Application__Plugin__UIWindow window = NANOEM__APPLICATION__PLUGIN__UIWINDOW__INIT;
        window.n_items = m_components.size();
        window.items = const_cast<Nanoem__Application__Plugin__UIComponent **>(m_components.data());
        return nanoem__application__plugin__uiwindow__get_packed_size(&window);
    }
    void
    getUIWindowLayoutData(nanoem_u8_t *data, nanoem_u32_t length, nanoem_application_plugin_status_t *status)
    {
        Nanoem__Application__Plugin__UIWindow window = NANOEM__APPLICATION__PLUGIN__UIWINDOW__INIT;
        window.n_items = m_components.size();
        window.items = const_cast<Nanoem__Application__Plugin__UIComponent **>(m_components.data());
        if (nanoem__application__plugin__uiwindow__get_packed_size(&window) <= length) {
            nanoem__application__plugin__uiwindow__pack(&window, data);
        }
        else {
            nanoem_application_plugin_status_assign_error(status, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
        }
    }
    void
    setUIComponentData(const char *id, const nanoem_u8_t *data, nanoem_u32_t length, int *reloadLayout)
    {
        if (Nanoem__Application__Plugin__UIComponent *component =
                nanoem__application__plugin__uicomponent__unpack(nullptr, length, data)) {
            if (StringUtils::equals(id, kAudioCodecComponentID) && component->combo_box->selected_index == 0) {
                m_audioCodecID = AV_CODEC_ID_PCM_S16LE;
            }
            else if (StringUtils::equals(id, kVideoCodecComponentID)) {
                const nanoem_u32_t index = component->combo_box->selected_index;
                if (index != m_videoCodecIndex) {
                    m_videoCodecIndex = index;
                    if (index < m_videoCodecIDs.size()) {
                        m_videoCodecID = m_videoCodecIDs[index];
                    }
                    /* the pixel format candidates depend on the codec, so reset the selection and
                     * let the dialog rebuild this window with the new list */
                    m_videoPixelFormatIndex = 0;
                    if (m_videoPixelFormatIndex < m_pixelFormats.size()) {
                        m_videoPixelFormat = m_pixelFormats[m_videoPixelFormatIndex];
                    }
                    if (reloadLayout) {
                        *reloadLayout = 1;
                    }
                }
            }
            else if (StringUtils::equals(id, kVideoPixelFormatComponentID)) {
                const nanoem_u32_t index = component->combo_box->selected_index;
                if (index < m_pixelFormats.size()) {
                    m_videoPixelFormatIndex = index;
                    m_videoPixelFormat = m_pixelFormats[index];
                }
            }
            nanoem__application__plugin__uicomponent__free_unpacked(component, nullptr);
        }
    }
    const char *
    failureReason() const
    {
        return strlen(m_reason) > 0 ? m_reason : nullptr;
    }
    const char *
    recoverySuggestion() const
    {
        return nullptr;
    }
    void
    setFailureReason(const char *message)
    {
        snprintf(m_reason, sizeof(m_reason), "%s", message);
    }
    AVCodecID
    videoCodecID() const
    {
        return m_videoCodecID;
    }

    int
    makeFailureReason(int rc, nanoem_application_plugin_status_t *status)
    {
        bool result = true;
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            rc = 0;
        }
        else if (rc < 0) {
            /* keep the message that was set by the failing operation itself when there is one */
            if (!m_reason[0]) {
                av_make_error_string(m_reason, sizeof(m_reason), rc);
            }
            result = false;
        }
        if (status) {
            *status =
                result ? NANOEM_APPLICATION_PLUGIN_STATUS_SUCCESS : NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_REFER_REASON;
        }
        return rc;
    }
    bool
    wrapCall(int rc, nanoem_application_plugin_status_t *status)
    {
        return makeFailureReason(rc, status) >= 0;
    }

    struct ScopedAudioFrame {
        ScopedAudioFrame(const AVCodecParameters *parameters, int numSamplesPerFrame, nanoem_frame_index_t pts)
            : m_opaque(av_frame_alloc())
        {
            m_opaque->format = parameters->format;
            av_channel_layout_copy(&m_opaque->ch_layout, &parameters->ch_layout);
            m_opaque->sample_rate = parameters->sample_rate;
            m_opaque->nb_samples = numSamplesPerFrame;
            m_opaque->pts = pts;
        }
        ~ScopedAudioFrame()
        {
            av_frame_free(&m_opaque);
        }

        operator AVFrame *()
        {
            return m_opaque;
        }

        AVFrame *m_opaque;
    };
    struct ScopedVideoFrame {
        ScopedVideoFrame(const AVCodecParameters *parameters, nanoem_frame_index_t pts)
            : m_opaque(av_frame_alloc())
        {
            m_opaque->format = parameters->format;
            m_opaque->width = parameters->width;
            m_opaque->height = parameters->height;
            m_opaque->pts = pts;
        }
        ~ScopedVideoFrame()
        {
            av_frame_free(&m_opaque);
        }

        operator AVFrame *()
        {
            return m_opaque;
        }

        AVFrame *m_opaque;
    };

    typedef std::vector<Nanoem__Application__Plugin__UIComponent *> ComponentList;

    char m_reason[1024];
    ComponentList m_components;
    std::vector<const char *> m_videoCodecLabels;
    std::vector<AVCodecID> m_videoCodecIDs;
    std::vector<const char *> m_pixelFormatLabels;
    std::vector<AVPixelFormat> m_pixelFormats;
    AVFormatContext *m_formatContext;
    AVStream *m_audioStream;
    AVStream *m_videoStream;
    AVCodecContext *m_audioCodecContext;
    AVCodecContext *m_videoCodecContext;
    SwrContext *m_resampleContext;
    SwsContext *m_scaleContext;
    nanoem_f32_t *m_tempAudioBuffer;
    AVCodecID m_audioCodecID;
    AVCodecID m_videoCodecID;
    AVPixelFormat m_videoPixelFormat;
    nanoem_u32_t m_videoCodecIndex;
    nanoem_u32_t m_videoPixelFormatIndex;
    nanoem_u32_t m_fps;
    nanoem_u32_t m_duration;
    int m_numFrequency;
    int m_numChannels;
    int m_numBits;
    nanoem_u32_t m_width;
    nanoem_u32_t m_height;
    nanoem_u32_t m_yflip;
    nanoem_i64_t m_nextAudioPTS;
    bool m_failed;
};
const char FFmpegEncoder::kAudioCodecComponentID[] = "ffmpeg.audio-codec";
const char FFmpegEncoder::kVideoCodecComponentID[] = "ffmpeg.video-codec";
const char FFmpegEncoder::kVideoPixelFormatComponentID[] = "ffmpeg.video-pixel-format";
const int FFmpegEncoder::kMinimumSampleRate = 44100;
const int FFmpegEncoder::kMinimumNumChannels = 2;

struct FFmpegDecoder {
    FFmpegDecoder()
        : m_audioFormatContext(nullptr)
        , m_videoFormatContext(nullptr)
        , m_audioCodecContext(nullptr)
        , m_videoCodecContext(nullptr)
        , m_decodedPixelFormat(AV_PIX_FMT_RGBA)
        , m_scaleContext(nullptr)
        , m_fps(0)
        , m_audioStreamIndex(-1)
        , m_videoStreamIndex(-1)
    {
        *m_reason = 0;
    }
    ~FFmpegDecoder()
    {
        nanoem_application_plugin_status_t status = NANOEM_APPLICATION_PLUGIN_STATUS_SUCCESS;
        closeAudio(&status);
        closeVideo(&status);
    }

    int
    open(const char *filePath, nanoem_application_plugin_status_t *status)
    {
        int audioStreamIndex = -1, videoStreamIndex = -1;
        if (m_audioStreamIndex < 0) {
            if (!wrapCall(avformat_open_input(&m_audioFormatContext, filePath, nullptr, nullptr), status)) {
                return 0;
            }
            else if (!wrapCall(avformat_find_stream_info(m_audioFormatContext, nullptr), status)) {
                return 0;
            }
            int rc = av_find_best_stream(m_audioFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (rc >= 0) {
                audioStreamIndex = rc;
                const AVStream *stream = m_audioFormatContext->streams[audioStreamIndex];
                const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
                m_audioCodecContext = avcodec_alloc_context3(codec);
                if (!wrapCall(avcodec_parameters_to_context(m_audioCodecContext, stream->codecpar), status)) {
                    return 0;
                }
                else if (!wrapCall(avcodec_open2(m_audioCodecContext, codec, nullptr), status)) {
                    return 0;
                }
                m_audioStreamIndex = audioStreamIndex;
            }
            else if (rc != AVERROR_STREAM_NOT_FOUND) {
                makeFailureReason(rc, status);
                return 0;
            }
        }
        if (m_videoStreamIndex < 0) {
            if (!wrapCall(avformat_open_input(&m_videoFormatContext, filePath, nullptr, nullptr), status)) {
                return 0;
            }
            else if (!wrapCall(avformat_find_stream_info(m_videoFormatContext, nullptr), status)) {
                return 0;
            }
            int rc = av_find_best_stream(m_videoFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (rc >= 0) {
                videoStreamIndex = rc;
                const AVStream *stream = m_videoFormatContext->streams[videoStreamIndex];
                const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
                m_videoCodecContext = avcodec_alloc_context3(codec);
                if (!wrapCall(avcodec_parameters_to_context(m_videoCodecContext, stream->codecpar), status)) {
                    return 0;
                }
                else if (!wrapCall(avcodec_open2(m_videoCodecContext, codec, nullptr), status)) {
                    return 0;
                }
                int width = m_videoCodecContext->width, height = m_videoCodecContext->height;
                m_scaleContext = sws_getContext(width, height, m_videoCodecContext->pix_fmt, width, height,
                    m_decodedPixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr);
                m_videoStreamIndex = videoStreamIndex;
            }
            else if (rc != AVERROR_STREAM_NOT_FOUND) {
                makeFailureReason(rc, status);
                return 0;
            }
        }
        return m_audioStreamIndex != -1 || m_videoStreamIndex != -1;
    }
    void
    setOption(nanoem_u32_t key, const void *value, nanoem_u32_t size, nanoem_application_plugin_status_t *status)
    {
        switch (key) {
        case NANOEM_APPLICATION_PLUGIN_DECODER_OPTION_FPS: {
            if (Inline::validateArgument<nanoem_u32_t>(value, size, status)) {
                m_fps = *static_cast<const nanoem_u32_t *>(value);
            }
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_OPTION_AUDIO_LOCATION: {
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_OPTION_VIDEO_LOCATION: {
            break;
        }
        default:
            nanoem_application_plugin_status_assign_error(
                status, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_UNKNOWN_OPTION);
            break;
        }
    }
    void
    audioFormatValue(nanoem_u32_t key, void *value, nanoem_u32_t *size, nanoem_application_plugin_status_t *status)
    {
        switch (key) {
        case NANOEM_APPLICATION_PLUGIN_DECODER_AUDIO_FORMAT_NUM_BITS: {
            nanoem_u32_t bits = m_audioCodecContext ? nanoem_u32_t(m_audioCodecContext->bits_per_raw_sample) : 0;
            Inline::assignOption(bits, value, size, status);
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_AUDIO_FORMAT_NUM_CHANNELS: {
            nanoem_u32_t channels = m_audioCodecContext ? nanoem_u32_t(m_audioCodecContext->ch_layout.nb_channels) : 0;
            Inline::assignOption(channels, value, size, status);
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_AUDIO_FORMAT_FREQUENCY: {
            nanoem_u32_t frequency = m_audioCodecContext ? nanoem_u32_t(m_audioCodecContext->sample_rate) : 0;
            Inline::assignOption(frequency, value, size, status);
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_AUDIO_FORMAT_DURATION: {
            nanoem_frame_index_t duration =
                static_cast<nanoem_frame_index_t>(av_rescale(m_videoFormatContext->duration, 30, AV_TIME_BASE));
            Inline::assignOption(duration, value, size, status);
            break;
        }
        default:
            nanoem_application_plugin_status_assign_error(
                status, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_UNKNOWN_OPTION);
            if (size) {
                *size = 0;
            }
            break;
        }
    }
    void
    videoFormatValue(nanoem_u32_t key, void *value, nanoem_u32_t *size, nanoem_application_plugin_status_t *status)
    {
        switch (key) {
        case NANOEM_APPLICATION_PLUGIN_DECODER_VIDEO_FORMAT_WIDTH: {
            nanoem_u32_t width = m_videoCodecContext ? nanoem_u32_t(m_videoCodecContext->width) : 0;
            Inline::assignOption(width, value, size, status);
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_VIDEO_FORMAT_HEIGHT: {
            nanoem_u32_t height = m_videoCodecContext ? nanoem_u32_t(m_videoCodecContext->height) : 0;
            Inline::assignOption(height, value, size, status);
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_VIDEO_FORMAT_STRIDE: {
            nanoem_u32_t stride = 0;
            Inline::assignOption(stride, value, size, status);
            break;
        }
        case NANOEM_APPLICATION_PLUGIN_DECODER_VIDEO_FORMAT_DURATION: {
            nanoem_frame_index_t duration =
                static_cast<nanoem_frame_index_t>(av_rescale(m_videoFormatContext->duration, 30, AV_TIME_BASE));
            Inline::assignOption(duration, value, size, status);
            break;
        }
        default:
            nanoem_application_plugin_status_assign_error(
                status, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_UNKNOWN_OPTION);
            if (size) {
                *size = 0;
            }
            break;
        }
    }
    void
    decodeAudioFrame(nanoem_frame_index_t currentFrameIndex, nanoem_u8_t **data, nanoem_u32_t *size,
        nanoem_application_plugin_status_t *status)
    {
        const AVStream *stream = m_audioFormatContext->streams[m_audioStreamIndex];
        AVFrame *frame = av_frame_alloc();
        int result =
            av_seek_frame(m_audioFormatContext, m_audioStreamIndex, convertTimestamp(currentFrameIndex, stream), 0);
        if (result >= 0 && decodeFrame(m_audioFormatContext, m_audioCodecContext, m_audioStreamIndex, frame, result)) {
            nanoem_u32_t length = nanoem_u32_t((nanoem_u32_t(m_audioCodecContext->sample_rate) / m_fps) *
                nanoem_u32_t(av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format))));
            nanoem_u8_t *dataPtr = *data = new nanoem_u8_t[length];
            memcpy(dataPtr, frame->data[0], length);
            *size = length;
        }
        else {
            makeFailureReason(result, status);
            *data = nullptr;
            *size = 0;
        }
        av_frame_free(&frame);
    }
    void
    decodeVideoFrame(nanoem_frame_index_t currentFrameIndex, nanoem_u8_t **data, nanoem_u32_t *size,
        nanoem_application_plugin_status_t *status)
    {
        const AVStream *stream = m_videoFormatContext->streams[m_videoStreamIndex];
        AVFrame *frame = av_frame_alloc();
        int result =
            av_seek_frame(m_videoFormatContext, m_videoStreamIndex, convertTimestamp(currentFrameIndex, stream), 0);
        if (result >= 0 && decodeFrame(m_videoFormatContext, m_videoCodecContext, m_videoStreamIndex, frame, result)) {
            nanoem_u8_t *frameData[4];
            int frameLineSize[4], height = frame->height;
            makeFailureReason(
                av_image_alloc(frameData, frameLineSize, frame->width, height, m_decodedPixelFormat, 1), status);
            makeFailureReason(
                sws_scale(m_scaleContext, frame->data, frame->linesize, 0, height, frameData, frameLineSize), status);
            nanoem_u32_t length = frameLineSize[0] * height;
            nanoem_u8_t *dataPtr = *data = new nanoem_u8_t[length];
            memcpy(dataPtr, frameData[0], length);
            av_freep(&frameData[0]);
            *size = length;
        }
        else {
            makeFailureReason(result, status);
            *data = nullptr;
            *size = 0;
        }
        av_frame_free(&frame);
    }
    void
    destroyAudioFrame(nanoem_frame_index_t /* currentFrameIndex */, nanoem_u8_t *data, nanoem_u32_t /* size */)
    {
        delete[] data;
    }
    void
    destroyVideoFrame(nanoem_frame_index_t /* currentFrameIndex */, nanoem_u8_t *data, nanoem_u32_t /* size */)
    {
        delete[] data;
    }
    const char *
    failureReason() const
    {
        return strlen(m_reason) > 0 ? m_reason : nullptr;
    }
    const char *
    recoverySuggestion() const
    {
        return nullptr;
    }
    int
    close(nanoem_application_plugin_status_t *status)
    {
        closeAudio(status);
        closeVideo(status);
        m_scaleContext = nullptr;
        return 1;
    }

    bool
    decodeFrame(AVFormatContext *format, AVCodecContext *codec, int streamIndex, AVFrame *&frame, int &result)
    {
        AVPacket *packet = av_packet_alloc();
        if (!packet) {
            result = AVERROR(ENOMEM);
            return false;
        }
        while (true) {
            result = av_read_frame(format, packet);
            if (result != 0) {
                break;
            }
            else if (packet->stream_index != streamIndex) {
                av_packet_unref(packet);
                continue;
            }
            result = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (result != 0 && result != AVERROR(EAGAIN)) {
                break;
            }
            result = avcodec_receive_frame(codec, frame);
            if (result == 0 || result != AVERROR(EAGAIN)) {
                break;
            }
        }
        av_packet_free(&packet);
        return result == 0;
    }
    nanoem_i64_t
    convertTimestamp(nanoem_frame_index_t currentFrameIndex, const AVStream *stream) const
    {
        const AVRational r = { 1, static_cast<int>(m_fps) };
        return av_rescale_q(currentFrameIndex, r, stream->time_base);
    }
    void
    closeAudio(nanoem_application_plugin_status_t *status)
    {
        nanoem_mark_unused(status);
        if (m_audioCodecContext && m_audioStreamIndex >= 0) {
            m_audioStreamIndex = -1;
        }
        avformat_close_input(&m_audioFormatContext);
    }
    void
    closeVideo(nanoem_application_plugin_status_t *status)
    {
        nanoem_mark_unused(status);
        if (m_videoCodecContext && m_videoStreamIndex >= 0) {
            m_videoStreamIndex = -1;
        }
        avformat_close_input(&m_videoFormatContext);
        if (m_scaleContext) {
            sws_freeContext(m_scaleContext);
            m_scaleContext = nullptr;
        }
    }
    int
    makeFailureReason(int rc, nanoem_application_plugin_status_t *status)
    {
        bool result = true;
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            rc = 0;
        }
        else if (rc < 0) {
            av_make_error_string(m_reason, sizeof(m_reason), rc);
            result = false;
        }
        if (status) {
            *status =
                result ? NANOEM_APPLICATION_PLUGIN_STATUS_SUCCESS : NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_REFER_REASON;
        }
        return rc;
    }
    bool
    wrapCall(int rc, nanoem_application_plugin_status_t *status)
    {
        return makeFailureReason(rc, status) >= 0;
    }

    char m_reason[1024];
    AVFormatContext *m_audioFormatContext;
    AVFormatContext *m_videoFormatContext;
    AVCodecContext *m_audioCodecContext;
    AVCodecContext *m_videoCodecContext;
    AVPixelFormat m_decodedPixelFormat;
    SwsContext *m_scaleContext;
    nanoem_u32_t m_fps;
    int m_audioStreamIndex;
    int m_videoStreamIndex;
};

} /* namespace anonymous */

struct nanoem_application_plugin_encoder_t : FFmpegEncoder { };

struct nanoem_application_plugin_decoder_t : FFmpegDecoder { };

nanoem_u32_t
nanoemApplicationPluginEncoderGetABIVersion()
{
    return NANOEM_APPLICATION_PLUGIN_ENCODER_ABI_VERSION;
}

void APIENTRY
nanoemApplicationPluginEncoderInitialize()
{
    av_log_set_callback(handleFFmpegLog);
}

nanoem_application_plugin_encoder_t *APIENTRY
nanoemApplicationPluginEncoderCreate()
{
    return new nanoem_application_plugin_encoder_t();
}

int APIENTRY
nanoemApplicationPluginEncoderOpen(
    nanoem_application_plugin_encoder_t *encoder, const char *filePath, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    int result = 0;
    if (nanoem_is_not_null(encoder)) {
        result = encoder->open(filePath, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
    return result;
}

void APIENTRY
nanoemApplicationPluginEncoderSetOption(nanoem_application_plugin_encoder_t *encoder, nanoem_u32_t key,
    const void *value, nanoem_u32_t size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(encoder)) {
        encoder->setOption(key, value, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginEncoderEncodeAudioFrame(nanoem_application_plugin_encoder_t *encoder,
    nanoem_frame_index_t currentFrameIndex, const nanoem_u8_t *data, nanoem_u32_t size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(encoder)) {
        encoder->encodeAudioFrame(currentFrameIndex, data, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginEncoderEncodeVideoFrame(nanoem_application_plugin_encoder_t *encoder,
    nanoem_frame_index_t currentFrameIndex, const nanoem_u8_t *data, nanoem_u32_t size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(encoder)) {
        encoder->encodeVideoFrame(currentFrameIndex, data, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginEncoderInterrupt(nanoem_application_plugin_encoder_t *encoder, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(encoder)) {
        encoder->interrupt(statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

const char *const *APIENTRY
nanoemApplicationPluginEncoderGetAllAvailableVideoFormatExtensions(
    const nanoem_application_plugin_encoder_t *encoder, nanoem_u32_t *length)
{
    static const char *kFormatExtensions[] = { "mp4", "mkv", "avi" };
    if (!length) {
        return kFormatExtensions;
    }
    const AVCodecID codecID = nanoem_is_not_null(encoder) ? encoder->videoCodecID() : AV_CODEC_ID_RAWVIDEO;
    return containerExtensionsFor(codecID, length);
}

void APIENTRY
nanoemApplicationPluginEncoderLoadUIWindowLayout(nanoem_application_plugin_encoder_t *plugin, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_likely(plugin)) {
        plugin->loadUIWindowLayout(statusPtr);
    }
    else if (status) {
        *status = NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT;
    }
}

void APIENTRY
nanoemApplicationPluginEncoderGetUIWindowLayoutDataSize(
    nanoem_application_plugin_encoder_t *plugin, nanoem_u32_t *length)
{
    if (nanoem_likely(plugin && length)) {
        *length = plugin->getUIWindowLayoutDataSize();
    }
}

void APIENTRY
nanoemApplicationPluginEncoderGetUIWindowLayoutData(
    nanoem_application_plugin_encoder_t *plugin, nanoem_u8_t *data, nanoem_u32_t length, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_likely(plugin && data)) {
        plugin->getUIWindowLayoutData(data, length, statusPtr);
    }
    else if (status) {
        *status = NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT;
    }
}

void APIENTRY
nanoemApplicationPluginEncoderSetUIComponentLayoutData(nanoem_application_plugin_encoder_t *plugin, const char *id,
    const nanoem_u8_t *data, nanoem_u32_t length, int *reloadLayout, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_likely(plugin && id && data && reloadLayout)) {
        plugin->setUIComponentData(id, data, length, reloadLayout);
    }
    else if (status) {
        *statusPtr = NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT;
    }
}

const char *APIENTRY
nanoemApplicationPluginEncoderGetFailureReason(const nanoem_application_plugin_encoder_t *encoder)
{
    return nanoem_is_not_null(encoder) ? encoder->failureReason() : nullptr;
}

const char *APIENTRY
nanoemApplicationPluginEncoderGetRecoverySuggestion(const nanoem_application_plugin_encoder_t *encoder)
{
    return nanoem_is_not_null(encoder) ? encoder->recoverySuggestion() : nullptr;
}

int APIENTRY
nanoemApplicationPluginEncoderClose(nanoem_application_plugin_encoder_t *encoder, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    int result = 0;
    if (nanoem_is_not_null(encoder)) {
        result = encoder->close(statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
    return result;
}

void APIENTRY
nanoemApplicationPluginEncoderDestroy(nanoem_application_plugin_encoder_t *encoder)
{
    delete encoder;
}

void APIENTRY
nanoemApplicationPluginEncoderTerminate()
{
}

nanoem_u32_t
nanoemApplicationPluginDecoderGetABIVersion()
{
    return NANOEM_APPLICATION_PLUGIN_DECODER_ABI_VERSION;
}

void APIENTRY
nanoemApplicationPluginDecoderInitialize()
{
    av_log_set_callback(handleFFmpegLog);
}

nanoem_application_plugin_decoder_t *APIENTRY
nanoemApplicationPluginDecoderCreate()
{
    return new nanoem_application_plugin_decoder_t();
}

int APIENTRY
nanoemApplicationPluginDecoderOpen(
    nanoem_application_plugin_decoder_t *decoder, const char *filePath, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    int result = 0;
    if (nanoem_is_not_null(decoder)) {
        result = decoder->open(filePath, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
    return result;
}

void APIENTRY
nanoemApplicationPluginDecoderSetOption(nanoem_application_plugin_decoder_t *decoder, nanoem_u32_t key,
    const void *value, nanoem_u32_t size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(decoder)) {
        decoder->setOption(key, value, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginDecoderGetAudioFormatValue(nanoem_application_plugin_decoder_t *decoder, nanoem_u32_t key,
    void *value, nanoem_u32_t *size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(decoder)) {
        decoder->audioFormatValue(key, value, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginDecoderGetVideoFormatValue(nanoem_application_plugin_decoder_t *decoder, nanoem_u32_t key,
    void *value, nanoem_u32_t *size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(decoder)) {
        decoder->videoFormatValue(key, value, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginDecoderDecodeAudioFrame(nanoem_application_plugin_decoder_t *decoder,
    nanoem_frame_index_t currentFrameIndex, nanoem_u8_t **data, nanoem_u32_t *size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(decoder)) {
        decoder->decodeAudioFrame(currentFrameIndex, data, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginDecoderDecodeVideoFrame(nanoem_application_plugin_decoder_t *decoder,
    nanoem_frame_index_t currentFrameIndex, nanoem_u8_t **data, nanoem_u32_t *size, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    if (nanoem_is_not_null(decoder)) {
        decoder->decodeVideoFrame(currentFrameIndex, data, size, statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
}

void APIENTRY
nanoemApplicationPluginDecoderDestroyAudioFrame(nanoem_application_plugin_decoder_t *decoder,
    nanoem_frame_index_t currentFrameIndex, nanoem_u8_t *data, nanoem_u32_t size)
{
    if (nanoem_is_not_null(decoder)) {
        decoder->destroyAudioFrame(currentFrameIndex, data, size);
    }
}

void APIENTRY
nanoemApplicationPluginDecoderDestroyVideoFrame(nanoem_application_plugin_decoder_t *decoder,
    nanoem_frame_index_t currentFrameIndex, nanoem_u8_t *data, nanoem_u32_t size)
{
    if (nanoem_is_not_null(decoder)) {
        decoder->destroyVideoFrame(currentFrameIndex, data, size);
    }
}

const char *const *APIENTRY
nanoemApplicationPluginDecoderGetAllAvailableAudioFormatExtensions(
    nanoem_application_plugin_decoder_t * /* decoder */, nanoem_u32_t *length)
{
    static const char *kFormatExtensions[] = { "alac", "flac" };
    *length = sizeof(kFormatExtensions) / sizeof(kFormatExtensions[0]);
    return kFormatExtensions;
}

const char *const *APIENTRY
nanoemApplicationPluginDecoderGetAllAvailableVideoFormatExtensions(
    nanoem_application_plugin_decoder_t * /* decoder */, nanoem_u32_t *length)
{
    static const char *kFormatExtensions[] = { "avi", "mkv" };
    *length = sizeof(kFormatExtensions) / sizeof(kFormatExtensions[0]);
    return kFormatExtensions;
}

const char *APIENTRY
nanoemApplicationPluginDecoderGetFailureReason(nanoem_application_plugin_decoder_t *decoder)
{
    return nanoem_is_not_null(decoder) ? decoder->failureReason() : nullptr;
}

const char *APIENTRY
nanoemApplicationPluginDecoderGetRecoverySuggestion(nanoem_application_plugin_decoder_t *decoder)
{
    return nanoem_is_not_null(decoder) ? decoder->recoverySuggestion() : nullptr;
}

int APIENTRY
nanoemApplicationPluginDecoderClose(nanoem_application_plugin_decoder_t *decoder, nanoem_i32_t *status)
{
    nanoem_application_plugin_status_t *statusPtr = reinterpret_cast<nanoem_application_plugin_status_t *>(status);
    int result = 0;
    if (nanoem_is_not_null(decoder)) {
        result = decoder->close(statusPtr);
    }
    else {
        nanoem_application_plugin_status_assign_error(statusPtr, NANOEM_APPLICATION_PLUGIN_STATUS_ERROR_NULL_OBJECT);
    }
    return result;
}

void APIENTRY
nanoemApplicationPluginDecoderDestroy(nanoem_application_plugin_decoder_t *decoder)
{
    delete decoder;
}

void APIENTRY
nanoemApplicationPluginDecoderTerminate()
{
}
