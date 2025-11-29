#include "ffmpeg_audio_encoder.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>

namespace godot {

static void log_ffmpeg(const String &msg) {
    UtilityFunctions::print("[FFmpegAudioEncoder] ", msg);
}

FFmpegAudioEncoder::FFmpegAudioEncoder() {
    av_log_set_level(AV_LOG_ERROR);
}

FFmpegAudioEncoder::~FFmpegAudioEncoder() {
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    initialized = false;
}

void FFmpegAudioEncoder::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("setup_encoder", "codec_name", "sample_rate", "channels", "bit_rate"),
        &FFmpegAudioEncoder::setup_encoder
    );
    ClassDB::bind_method(
        D_METHOD("encode", "pcm_interleaved"),
        &FFmpegAudioEncoder::encode
    );
    ClassDB::bind_method(
        D_METHOD("flush"),
        &FFmpegAudioEncoder::flush
    );
}

int FFmpegAudioEncoder::setup_encoder(const String &p_codec_name, int p_sample_rate, int p_channels, int p_bit_rate) {
    // Clean any previous encoder state
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    initialized = false;

    if (p_sample_rate <= 0 || p_channels <= 0 || p_bit_rate <= 0) {
        log_ffmpeg("Invalid encoder parameters");
        return 1;
    }

    const CharString codec_name_utf8 = p_codec_name.utf8();
    const AVCodec *codec = avcodec_find_encoder_by_name(codec_name_utf8.get_data());
    if (!codec) {
        log_ffmpeg("Encoder not found: " + p_codec_name);
        return 2;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        log_ffmpeg("Failed to allocate codec context");
        return 3;
    }

    sample_rate = p_sample_rate;
    channels = p_channels;

    codec_ctx->sample_rate = sample_rate;
    codec_ctx->bit_rate = p_bit_rate;

    // ---------- Channel layout (FFmpeg 5+/8 style: AVChannelLayout) ----------
    // Set codec_ctx->ch_layout, not codec_ctx->channels / channel_layout.
    if (channels == 1) {
        codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else if (channels == 2) {
        codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    } else {
        // Simple, safe behavior: only support mono/stereo for now.
        log_ffmpeg("Only mono and stereo channel layouts are supported in this example");
        return 4;
    }

    // ---------- Sample format (simplified, FFmpeg 8) ----------
    if (codec->sample_fmts && codec->sample_fmts[0] != AV_SAMPLE_FMT_NONE) {
        // Just use the first supported format the encoder reports.
        sample_fmt = codec->sample_fmts[0];
        log_ffmpeg("Using encoder sample format: " + String::num_int64(sample_fmt));
    } else {
        // Fallback: float32 interleaved (may or may not be supported by every encoder)
        sample_fmt = AV_SAMPLE_FMT_FLT;
        log_ffmpeg("Encoder did not report sample_fmts; assuming AV_SAMPLE_FMT_FLT");
    }

    codec_ctx->sample_fmt = sample_fmt;


    // Open encoder
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        log_ffmpeg("Failed to open codec");
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
        return 5;
    }

    frame = av_frame_alloc();
    if (!frame) {
        log_ffmpeg("Failed to allocate frame");
        return 6;
    }

    packet = av_packet_alloc();
    if (!packet) {
        log_ffmpeg("Failed to allocate packet");
        av_frame_free(&frame);
        frame = nullptr;
        return 7;
    }

    initialized = true;
    return 0;
}

PackedByteArray FFmpegAudioEncoder::encode(const PackedFloat32Array &p_pcm_interleaved) {
    PackedByteArray output;

    if (!initialized || !codec_ctx || !frame || !packet) {
        log_ffmpeg("Encoder not initialized");
        return output;
    }

    const int total_floats = p_pcm_interleaved.size();
    if (total_floats <= 0 || channels <= 0) {
        return output;
    }

    if (total_floats % channels != 0) {
        log_ffmpeg("PCM array size is not divisible by channel count; truncating last partial frame");
    }

    const int total_samples = total_floats / channels;
    if (total_samples <= 0) {
        return output;
    }

    // Configure frame
    frame->nb_samples  = total_samples;
    frame->format      = codec_ctx->sample_fmt;
    frame->sample_rate = codec_ctx->sample_rate;
    frame->ch_layout   = codec_ctx->ch_layout;

    if (av_frame_get_buffer(frame, 0) < 0) {
        log_ffmpeg("Failed to allocate frame buffer");
        return output;
    }

    if (av_frame_make_writable(frame) < 0) {
        log_ffmpeg("Frame not writable");
        av_frame_unref(frame);
        return output;
    }

    const float *src = p_pcm_interleaved.ptr();

    // ---------- Fill frame depending on sample format ----------
    if (sample_fmt == AV_SAMPLE_FMT_FLT) {
        // Interleaved float: data[0] holds all channels interleaved.
        float *dst = reinterpret_cast<float *>(frame->data[0]);
        const int samples_interleaved = total_samples * channels;
        std::memcpy(dst, src, samples_interleaved * sizeof(float));

    } else if (sample_fmt == AV_SAMPLE_FMT_FLTP) {
        // Planar float: data[c] contains all samples for channel c.
        for (int s = 0; s < total_samples; ++s) {
            for (int c = 0; c < channels; ++c) {
                float *dst_ch = reinterpret_cast<float *>(frame->data[c]);
                const int src_index = s * channels + c;
                dst_ch[s] = src[src_index];
            }
        }

    } else {
        log_ffmpeg("Only FLT and FLTP formats are implemented in this example");
        av_frame_unref(frame);
        return output;
    }

    // ---------- Send frame to encoder ----------
    int ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        log_ffmpeg("Error sending frame to encoder");
        av_frame_unref(frame);
        return output;
    }

    // Read all available packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            log_ffmpeg("Error receiving packet from encoder");
            break;
        }

        const int old_size = output.size();
        output.resize(old_size + packet->size);
        std::memcpy(output.ptrw() + old_size, packet->data, packet->size);

        av_packet_unref(packet);
    }

    av_frame_unref(frame);
    return output;
}

PackedByteArray FFmpegAudioEncoder::flush() {
    PackedByteArray output;

    if (!initialized || !codec_ctx || !packet) {
        return output;
    }

    int ret = avcodec_send_frame(codec_ctx, nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        log_ffmpeg("Error sending flush frame");
        return output;
    }

    while (true) {
        ret = avcodec_receive_packet(codec_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            log_ffmpeg("Error receiving packet during flush");
            break;
        }

        const int old_size = output.size();
        output.resize(old_size + packet->size);
        std::memcpy(output.ptrw() + old_size, packet->data, packet->size);

        av_packet_unref(packet);
    }

    return output;
}

} // namespace godot
