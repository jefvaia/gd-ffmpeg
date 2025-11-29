#include "ffmpeg_audio_decoder.h"
#include "ffmpeg_audio_encoder.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/math.hpp>

#include <algorithm>
#include <cstring>

namespace godot {

static void log_ffmpeg_dec(const String &p_msg) {
    UtilityFunctions::print("[FFmpegAudioDecoder] ", p_msg);
}

FFmpegAudioDecoder::FFmpegAudioDecoder() {
    av_log_set_level(AV_LOG_ERROR);
}

FFmpegAudioDecoder::~FFmpegAudioDecoder() {
    clear_resources();
}

void FFmpegAudioDecoder::clear_resources() {
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
    if (format_ctx) {
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }
    if (swr_ctx) {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        avio_context_free(&avio_ctx);
        avio_ctx = nullptr;
    }
    source_bytes.clear();
    source_pos = 0;
    audio_stream_index = -1;
    target_sample_rate = 0;
    target_channels = 0;
}

void FFmpegAudioDecoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_input_codec", "codec_name"), &FFmpegAudioDecoder::set_input_codec);
    ClassDB::bind_method(D_METHOD("set_output_sample_rate", "sample_rate"), &FFmpegAudioDecoder::set_output_sample_rate);
    ClassDB::bind_method(D_METHOD("set_output_channels", "channels"), &FFmpegAudioDecoder::set_output_channels);
    ClassDB::bind_method(D_METHOD("load_file", "path"), &FFmpegAudioDecoder::load_file);
    ClassDB::bind_method(D_METHOD("load_bytes", "data"), &FFmpegAudioDecoder::load_bytes);
    ClassDB::bind_method(D_METHOD("decode_pcm"), &FFmpegAudioDecoder::decode_pcm);
    ClassDB::bind_method(D_METHOD("decode_audio_frames"), &FFmpegAudioDecoder::decode_audio_frames);
    ClassDB::bind_method(D_METHOD("decode_audio_stream"), &FFmpegAudioDecoder::decode_audio_stream);
    ClassDB::bind_method(D_METHOD("decode_pcm_from_file", "path"), &FFmpegAudioDecoder::decode_pcm_from_file);
    ClassDB::bind_method(D_METHOD("decode_audio_frames_from_file", "path"), &FFmpegAudioDecoder::decode_audio_frames_from_file);
    ClassDB::bind_method(D_METHOD("decode_audio_stream_from_file", "path"), &FFmpegAudioDecoder::decode_audio_stream_from_file);
    ClassDB::bind_method(D_METHOD("get_sample_rate"), &FFmpegAudioDecoder::get_sample_rate);
    ClassDB::bind_method(D_METHOD("get_channels"), &FFmpegAudioDecoder::get_channels);
}

void FFmpegAudioDecoder::set_input_codec(const String &p_codec_name) {
    input_codec_name = p_codec_name;
}

void FFmpegAudioDecoder::set_output_sample_rate(int p_rate) {
    target_sample_rate = p_rate;
}

void FFmpegAudioDecoder::set_output_channels(int p_channels) {
    target_channels = p_channels;
}

int FFmpegAudioDecoder::setup_resampler(const AVChannelLayout &p_src_layout) {
    if (!codec_ctx) {
        return 1;
    }

    AVChannelLayout dst_layout = p_src_layout;
    if (target_channels > 0) {
        if (target_channels == 1) {
            dst_layout = AV_CHANNEL_LAYOUT_MONO;
        } else if (target_channels == 2) {
            dst_layout = AV_CHANNEL_LAYOUT_STEREO;
        }
        // (you can add more layouts later if needed)
    }

    const int dst_rate = target_sample_rate > 0 ? target_sample_rate : codec_ctx->sample_rate;

    // New FFmpeg 5+/8 style: swr_alloc_set_opts2 returns int and takes SwrContext**
    int ret = swr_alloc_set_opts2(
        &swr_ctx,
        &dst_layout,
        target_format,
        dst_rate,
        &p_src_layout,
        codec_ctx->sample_fmt,
        codec_ctx->sample_rate,
        0,
        nullptr
    );
    if (ret < 0 || !swr_ctx) {
        return 2;
    }

    if (swr_init(swr_ctx) < 0) {
        swr_free(&swr_ctx);
        return 3;
    }

    target_sample_rate = dst_rate;
    target_channels = dst_layout.nb_channels;
    return 0;
}

int FFmpegAudioDecoder::open_input_internal(const char *p_path) {
    if (!format_ctx) {
        format_ctx = avformat_alloc_context();
    }

    if (!source_bytes.is_empty()) {
        const int avio_buffer_size = 4096;
        unsigned char *avio_buffer = static_cast<unsigned char *>(av_malloc(avio_buffer_size));
        avio_ctx = avio_alloc_context(
    avio_buffer,
    avio_buffer_size,
    0,
    this,
    &FFmpegAudioDecoder::read_packet,
    nullptr,
    nullptr
);
        format_ctx->pb = avio_ctx;
        format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
    }

    int ret = AVERROR(EIO);
    if (!source_bytes.is_empty()) {
        ret = avformat_open_input(&format_ctx, "", nullptr, nullptr);
    } else {
        ret = avformat_open_input(&format_ctx, p_path, nullptr, nullptr);
    }

    if (ret < 0) {
        log_ffmpeg_dec("Failed to open input");
        return 1;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        log_ffmpeg_dec("Failed to find stream info");
        return 2;
    }

    const AVCodec *codec = nullptr;
    if (!input_codec_name.is_empty()) {
        CharString cstr = input_codec_name.utf8();
        codec = avcodec_find_decoder_by_name(cstr.get_data());
    }

    // locate audio stream
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = static_cast<int>(i);
            if (!codec) {
                codec = avcodec_find_decoder(format_ctx->streams[i]->codecpar->codec_id);
            }
            break;
        }
    }

    if (audio_stream_index < 0 || !codec) {
        log_ffmpeg_dec("No audio stream found");
        return 3;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, format_ctx->streams[audio_stream_index]->codecpar);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        log_ffmpeg_dec("Could not open decoder");
        return 4;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        return 5;
    }

    if (setup_resampler(codec_ctx->ch_layout) != 0) {
        log_ffmpeg_dec("Failed to configure resampler");
        return 6;
    }

    return 0;
}

int FFmpegAudioDecoder::load_file(const String &p_path) {
    clear_resources();
    format_ctx = avformat_alloc_context();
    if (!format_ctx) {
        return 1;
    }
    CharString utf8 = p_path.utf8();
    return open_input_internal(utf8.get_data());
}

int FFmpegAudioDecoder::load_bytes(const PackedByteArray &p_bytes) {
    clear_resources();
    source_bytes = p_bytes;
    source_pos = 0;
    format_ctx = avformat_alloc_context();
    if (!format_ctx) {
        return 1;
    }
    return open_input_internal(nullptr);
}

PackedFloat32Array FFmpegAudioDecoder::decode_pcm() {
    PackedFloat32Array pcm;
    if (!codec_ctx || !format_ctx || !packet || !frame || !swr_ctx) {
        return pcm;
    }

    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index != audio_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        int ret = avcodec_send_packet(codec_ctx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            log_ffmpeg_dec("Error sending packet to decoder");
            break;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                log_ffmpeg_dec("Error receiving frame");
                break;
            }

            const int dst_nb_channels = target_channels > 0 ? target_channels : frame->ch_layout.nb_channels;
            const int dst_nb_samples = av_rescale_rnd(
                swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
                target_sample_rate,
                frame->sample_rate,
                AV_ROUND_UP
            );

            int out_linesize = 0;
            float *out_buffer = nullptr;
            if (av_samples_alloc(
                    reinterpret_cast<uint8_t **>(&out_buffer),
                    &out_linesize,
                    dst_nb_channels,
                    dst_nb_samples,
                    AV_SAMPLE_FMT_FLT,
                    0) < 0) {
                log_ffmpeg_dec("Failed to allocate output samples");
                break;
            }

            const int converted = swr_convert(
                swr_ctx,
                reinterpret_cast<uint8_t **>(&out_buffer),
                dst_nb_samples,
                const_cast<const uint8_t **>(frame->extended_data),
                frame->nb_samples
            );
            const int samples_written = converted * dst_nb_channels;
            const int old_size = pcm.size();
            pcm.resize(old_size + samples_written);
            std::memcpy(pcm.ptrw() + old_size, out_buffer, samples_written * sizeof(float));

            av_freep(&out_buffer);
            av_frame_unref(frame);
        }
    }

    // Flush decoder
    int ret = avcodec_send_packet(codec_ctx, nullptr);
    if (ret >= 0) {
        while (true) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            const int dst_nb_channels = target_channels > 0 ? target_channels : frame->ch_layout.nb_channels;
            const int dst_nb_samples = av_rescale_rnd(
                swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
                target_sample_rate,
                frame->sample_rate,
                AV_ROUND_UP
            );

            int out_linesize = 0;
            float *out_buffer = nullptr;
            if (av_samples_alloc(
                    reinterpret_cast<uint8_t **>(&out_buffer),
                    &out_linesize,
                    dst_nb_channels,
                    dst_nb_samples,
                    AV_SAMPLE_FMT_FLT,
                    0) < 0) {
                break;
            }

            const int converted = swr_convert(
                swr_ctx,
                reinterpret_cast<uint8_t **>(&out_buffer),
                dst_nb_samples,
                const_cast<const uint8_t **>(frame->extended_data),
                frame->nb_samples
            );
            const int samples_written = converted * dst_nb_channels;
            const int old_size = pcm.size();
            pcm.resize(old_size + samples_written);
            std::memcpy(pcm.ptrw() + old_size, out_buffer, samples_written * sizeof(float));

            av_freep(&out_buffer);
            av_frame_unref(frame);
        }
    }

    return pcm;
}

Array FFmpegAudioDecoder::decode_audio_frames() {
    Array frames;
    PackedFloat32Array pcm = decode_pcm();
    const int ch = target_channels > 0 ? target_channels : 2;

    // We now return an Array of Dictionary { "left": float, "right": float }
    for (int i = 0; i + ch - 1 < pcm.size(); i += ch) {
        Dictionary f;
        const float left = pcm[i];
        const float right = (ch > 1) ? pcm[i + 1] : left;
        f["left"] = left;
        f["right"] = right;
        frames.push_back(f);
    }
    return frames;
}

Ref<AudioStreamWAV> FFmpegAudioDecoder::decode_audio_stream() {
    PackedFloat32Array pcm = decode_pcm();
    if (pcm.is_empty()) {
        return Ref<AudioStreamWAV>();
    }

    const int ch = target_channels > 0 ? target_channels : 2;
    Ref<AudioStreamWAV> stream;
    stream.instantiate();
    stream->set_mix_rate(target_sample_rate);
    stream->set_stereo(ch > 1);
    stream->set_format(AudioStreamWAV::FORMAT_16_BITS);

    PackedByteArray data;
    data.resize(pcm.size() * static_cast<int>(sizeof(int16_t)));
    int16_t *dst = reinterpret_cast<int16_t *>(data.ptrw());
    const float *src = pcm.ptr();
    for (int i = 0; i < pcm.size(); i++) {
        const float clamped = Math::clamp(src[i], -1.0f, 1.0f);
        dst[i] = static_cast<int16_t>(clamped * 32767.0f);
    }

    stream->set_data(data);
    return stream;
}

PackedFloat32Array FFmpegAudioDecoder::decode_pcm_from_file(const String &p_path) {
    if (load_file(p_path) != 0) {
        return PackedFloat32Array();
    }
    PackedFloat32Array pcm = decode_pcm();
    clear_resources();
    return pcm;
}

Array FFmpegAudioDecoder::decode_audio_frames_from_file(const String &p_path) {
    if (load_file(p_path) != 0) {
        return Array();
    }
    Array frames = decode_audio_frames();
    clear_resources();
    return frames;
}

Ref<AudioStreamWAV> FFmpegAudioDecoder::decode_audio_stream_from_file(const String &p_path) {
    if (load_file(p_path) != 0) {
        return Ref<AudioStreamWAV>();
    }
    Ref<AudioStreamWAV> stream = decode_audio_stream();
    clear_resources();
    return stream;
}

// ----------------------------- Transcoder -----------------------------

void FFmpegAudioTranscoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_input_codec", "codec_name"), &FFmpegAudioTranscoder::set_input_codec);
    ClassDB::bind_method(D_METHOD("set_output_codec", "codec_name"), &FFmpegAudioTranscoder::set_output_codec);
    ClassDB::bind_method(D_METHOD("set_output_sample_rate", "sample_rate"), &FFmpegAudioTranscoder::set_output_sample_rate);
    ClassDB::bind_method(D_METHOD("set_output_channels", "channels"), &FFmpegAudioTranscoder::set_output_channels);
    ClassDB::bind_method(D_METHOD("transcode_file", "input_path", "output_path"), &FFmpegAudioTranscoder::transcode_file);
}

void FFmpegAudioTranscoder::set_input_codec(const String &p_codec_name) {
    input_codec = p_codec_name;
}

void FFmpegAudioTranscoder::set_output_codec(const String &p_codec_name) {
    output_codec = p_codec_name;
}

void FFmpegAudioTranscoder::set_output_sample_rate(int p_rate) {
    output_sample_rate = p_rate;
}

void FFmpegAudioTranscoder::set_output_channels(int p_channels) {
    output_channels = p_channels;
}

int FFmpegAudioTranscoder::transcode_file(const String &p_input_path, const String &p_output_path) {
    Ref<FFmpegAudioDecoder> decoder = memnew(FFmpegAudioDecoder);
    if (!input_codec.is_empty()) {
        decoder->set_input_codec(input_codec);
    }

    if (decoder->load_file(p_input_path) != 0) {
        log_ffmpeg_dec("Failed to load input for transcoding");
        return 1;
    }

    if (output_sample_rate > 0) {
        decoder->set_output_sample_rate(output_sample_rate);
    }
    if (output_channels > 0) {
        decoder->set_output_channels(output_channels);
    }

    PackedFloat32Array pcm = decoder->decode_pcm();

    Ref<FFmpegAudioEncoder> encoder = memnew(FFmpegAudioEncoder);
    const String codec_to_use = output_codec.is_empty() ? String("aac") : output_codec;
    const int sr = output_sample_rate > 0 ? output_sample_rate : decoder->get_sample_rate();
    const int ch = output_channels > 0 ? output_channels : decoder->get_channels();

    if (encoder->setup_encoder(codec_to_use, sr, ch, 128000, Dictionary()) != 0) {
        log_ffmpeg_dec("Failed to setup encoder");
        return 2;
    }

    PackedByteArray encoded = encoder->encode(pcm);
    PackedByteArray tail = encoder->flush();
    encoded.append_array(tail);

    // FileAccess::open only takes (path, mode) in your binding: fix here
    Ref<FileAccess> file = FileAccess::open(p_output_path, FileAccess::WRITE);
    if (file.is_null()) {
        log_ffmpeg_dec("Could not open output file for writing");
        return 3;
    }
    file->store_buffer(encoded);
    return 0;
}

int FFmpegAudioDecoder::read_packet(void *opaque, uint8_t *buf, int buf_size) {
    FFmpegAudioDecoder *self = reinterpret_cast<FFmpegAudioDecoder *>(opaque);
    if (!self) {
        return AVERROR(EIO);
    }
    const PackedByteArray &bytes = self->source_bytes;
    if (self->source_pos >= static_cast<size_t>(bytes.size())) {
        return AVERROR_EOF;
    }

    const int remaining = bytes.size() - static_cast<int>(self->source_pos);
    const int to_copy = std::min(buf_size, remaining);
    std::memcpy(buf, bytes.ptr() + self->source_pos, to_copy);
    self->source_pos += to_copy;
    return to_copy;
}


} // namespace godot
