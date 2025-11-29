#include "ffmpeg_video_encoder.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <cstring>

namespace godot {

static void log_video_encoder(const String &p_msg) {
    UtilityFunctions::print("[FFmpegVideoEncoder] ", p_msg);
}

static void apply_video_option_to_target(void *p_target, const char *p_key, const Variant &p_value) {
    if (!p_target || p_value.get_type() == Variant::NIL) {
        return;
    }

    int err = 0;
    switch (p_value.get_type()) {
        case Variant::INT:
            err = av_opt_set_int(p_target, p_key, static_cast<int64_t>(p_value), 0);
            break;
        case Variant::FLOAT:
            err = av_opt_set_double(p_target, p_key, static_cast<double>(p_value), 0);
            break;
        case Variant::BOOL:
            err = av_opt_set_int(p_target, p_key, bool(p_value) ? 1 : 0, 0);
            break;
        default: {
            const CharString utf8_value = String(p_value).utf8();
            err = av_opt_set(p_target, p_key, utf8_value.get_data(), 0);
            break;
        }
    }

    if (err < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(err, err_buf, sizeof(err_buf));
        log_video_encoder("Could not apply option '" + String(p_key) + "' (" + String(err_buf) + ")");
    }
}

static void apply_video_option(AVCodecContext *p_ctx, const char *p_key, const Variant &p_value) {
    if (!p_ctx) {
        return;
    }
    apply_video_option_to_target(p_ctx, p_key, p_value);
    if (p_ctx->priv_data) {
        apply_video_option_to_target(p_ctx->priv_data, p_key, p_value);
    }
}

void FFmpegVideoEncoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_codec_name", "name"), &FFmpegVideoEncoder::set_codec_name);
    ClassDB::bind_method(D_METHOD("set_pixel_format", "fmt"), &FFmpegVideoEncoder::set_pixel_format);
    ClassDB::bind_method(D_METHOD("set_frame_rate", "fps"), &FFmpegVideoEncoder::set_frame_rate);
    ClassDB::bind_method(D_METHOD("set_resolution", "width", "height"), &FFmpegVideoEncoder::set_resolution);
    ClassDB::bind_method(D_METHOD("set_bit_rate", "bps"), &FFmpegVideoEncoder::set_bit_rate);
    ClassDB::bind_method(D_METHOD("set_rate_control_mode", "mode"), &FFmpegVideoEncoder::set_rate_control_mode);
    ClassDB::bind_method(D_METHOD("set_quality", "value"), &FFmpegVideoEncoder::set_quality);
    ClassDB::bind_method(D_METHOD("set_preset", "preset"), &FFmpegVideoEncoder::set_preset);
    ClassDB::bind_method(D_METHOD("set_profile", "profile"), &FFmpegVideoEncoder::set_profile);
    ClassDB::bind_method(D_METHOD("set_keyframe_interval", "interval"), &FFmpegVideoEncoder::set_keyframe_interval);
    ClassDB::bind_method(D_METHOD("encode_images_to_file", "frames", "path"), &FFmpegVideoEncoder::encode_images_to_file);
    ClassDB::bind_method(D_METHOD("encode_images", "frames"), &FFmpegVideoEncoder::encode_images);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "codec_name"), "set_codec_name", String());
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "pixel_format"), "set_pixel_format", String());
    ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_rate"), "set_frame_rate", Variant());
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "resolution"), "set_resolution", Variant());
    ADD_PROPERTY(PropertyInfo(Variant::INT, "bit_rate"), "set_bit_rate", Variant());
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "rate_control_mode"), "set_rate_control_mode", String());
    ADD_PROPERTY(PropertyInfo(Variant::INT, "quality"), "set_quality", Variant());
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "preset"), "set_preset", String());
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "profile"), "set_profile", String());
    ADD_PROPERTY(PropertyInfo(Variant::INT, "keyframe_interval"), "set_keyframe_interval", Variant());
}

void FFmpegVideoEncoder::set_codec_name(const String &p_name) {
    if (!p_name.is_empty()) {
        codec_name = p_name;
    }
}

void FFmpegVideoEncoder::set_pixel_format(const String &p_name) {
    const AVPixelFormat fmt = pixel_format_from_string(p_name);
    if (fmt != AV_PIX_FMT_NONE) {
        target_pix_fmt = fmt;
    }
}

void FFmpegVideoEncoder::set_frame_rate(int p_rate) {
    if (p_rate > 0) {
        frame_rate = p_rate;
    }
}

void FFmpegVideoEncoder::set_resolution(int p_width, int p_height) {
    if (p_width > 0 && p_height > 0) {
        width = p_width;
        height = p_height;
    }
}

void FFmpegVideoEncoder::set_bit_rate(int64_t p_bit_rate) {
    if (p_bit_rate > 0) {
        bit_rate = p_bit_rate;
    }
}

void FFmpegVideoEncoder::set_rate_control_mode(const String &p_mode) {
    const String lower = p_mode.to_lower();
    if (lower == "cbr" || lower == "vbr") {
        rate_control_mode = lower;
    }
}

void FFmpegVideoEncoder::set_quality(int p_quality) {
    if (p_quality >= 0) {
        quality = p_quality;
    }
}

void FFmpegVideoEncoder::set_preset(const String &p_preset) {
    if (!p_preset.is_empty()) {
        preset = p_preset;
    }
}

void FFmpegVideoEncoder::set_profile(const String &p_profile) {
    if (!p_profile.is_empty()) {
        profile = p_profile;
    }
}

void FFmpegVideoEncoder::set_keyframe_interval(int p_interval) {
    if (p_interval > 0) {
        keyframe_interval = p_interval;
    }
}

Ref<Image> FFmpegVideoEncoder::image_from_any(const Variant &p_value) {
    if (p_value.get_type() == Variant::OBJECT) {
        Object *obj = p_value;
        if (obj) {
            if (Image *raw = Object::cast_to<Image>(obj)) {
                return Ref<Image>(raw);
            }
            if (ImageTexture *tex = Object::cast_to<ImageTexture>(obj)) {
                return tex->get_image();
            }
        }
    }
    if (p_value.get_type() == Variant::STRING) {
        Ref<Image> img;
        img.instantiate();
        const String path = p_value;
        const Error err = img->load(path);
        if (err == OK) {
            return img;
        }
    }
    return Ref<Image>();
}

AVPixelFormat FFmpegVideoEncoder::pixel_format_from_string(const String &p_name) {
    const String lower = p_name.to_lower();
    if (lower == "yuv420p") {
        return AV_PIX_FMT_YUV420P;
    }
    if (lower == "yuv422p") {
        return AV_PIX_FMT_YUV422P;
    }
    if (lower == "rgb24") {
        return AV_PIX_FMT_RGB24;
    }
    if (lower == "rgba") {
        return AV_PIX_FMT_RGBA;
    }
    if (lower == "nv12") {
        return AV_PIX_FMT_NV12;
    }
    return AV_PIX_FMT_NONE;
}

String FFmpegVideoEncoder::pixel_format_to_string(AVPixelFormat p_fmt) {
    switch (p_fmt) {
        case AV_PIX_FMT_YUV420P: return "yuv420p";
        case AV_PIX_FMT_YUV422P: return "yuv422p";
        case AV_PIX_FMT_RGB24: return "rgb24";
        case AV_PIX_FMT_RGBA: return "rgba";
        case AV_PIX_FMT_NV12: return "nv12";
        default: break;
    }
    return "unknown";
}

int FFmpegVideoEncoder::encode_images_to_file(const Array &p_frames, const String &p_path) {
    PackedByteArray dummy;
    return encode_internal(p_frames, p_path, &dummy);
}

PackedByteArray FFmpegVideoEncoder::encode_images(const Array &p_frames) {
    PackedByteArray data;
    encode_internal(p_frames, String(), &data);
    return data;
}

int FFmpegVideoEncoder::encode_internal(const Vector<Ref<Image>> &p_frames, const String &p_path, PackedByteArray *r_bytes) {
    if (p_frames.is_empty()) {
        log_video_encoder("No frames provided");
        return 1;
    }

    const Ref<Image> first = p_frames[0];
    if (first.is_null()) {
        log_video_encoder("First frame is invalid");
        return 2;
    }

    int target_width = width > 0 ? width : first->get_width();
    int target_height = height > 0 ? height : first->get_height();
    if (target_width <= 0 || target_height <= 0) {
        log_video_encoder("Invalid dimensions");
        return 3;
    }

    const CharString codec_utf8 = codec_name.utf8();
    const AVCodec *codec = avcodec_find_encoder_by_name(codec_utf8.get_data());
    if (!codec) {
        // Fallback to id-based discovery for common codecs
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec) {
        log_video_encoder("Encoder not found");
        return 4;
    }

    AVFormatContext *fmt_ctx = nullptr;
    AVStream *stream = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    SwsContext *sws_ctx = nullptr;
    int ret = 0;

    do {
        // Choose format
        if (!p_path.is_empty()) {
            if (avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, p_path.utf8().get_data()) < 0) {
                log_video_encoder("Failed to allocate output context");
                ret = 5;
                break;
            }
        } else {
            if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "mp4", nullptr) < 0) {
                log_video_encoder("Failed to allocate memory output context");
                ret = 6;
                break;
            }
        }

        stream = avformat_new_stream(fmt_ctx, nullptr);
        if (!stream) {
            log_video_encoder("Failed to create stream");
            ret = 7;
            break;
        }

        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
            log_video_encoder("Failed to allocate codec context");
            ret = 8;
            break;
        }

        codec_ctx->codec_id = codec->id;
        codec_ctx->width = target_width;
        codec_ctx->height = target_height;
        codec_ctx->pix_fmt = target_pix_fmt;
        codec_ctx->time_base = AVRational{1, frame_rate};
        codec_ctx->framerate = AVRational{frame_rate, 1};
        codec_ctx->gop_size = keyframe_interval;

        if (rate_control_mode == "cbr") {
            codec_ctx->bit_rate = bit_rate;
        } else {
            codec_ctx->bit_rate = 0;
            apply_video_option(codec_ctx, "crf", quality);
        }

        apply_video_option(codec_ctx, "preset", preset);
        if (!profile.is_empty()) {
            apply_video_option(codec_ctx, "profile", profile);
        }

        if (codec_ctx->codec_id == AV_CODEC_ID_H265) {
            codec_ctx->profile = FF_PROFILE_HEVC_MAIN;
        }

        if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
            log_video_encoder("Failed to open codec");
            ret = 9;
            break;
        }

        if (avcodec_parameters_from_context(stream->codecpar, codec_ctx) < 0) {
            log_video_encoder("Failed to copy codec parameters");
            ret = 10;
            break;
        }
        stream->time_base = codec_ctx->time_base;

        // Open IO
        if (!p_path.is_empty()) {
            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&fmt_ctx->pb, p_path.utf8().get_data(), AVIO_FLAG_WRITE) < 0) {
                    log_video_encoder("Could not open output file");
                    ret = 11;
                    break;
                }
            }
        } else {
            if (avio_open_dyn_buf(&fmt_ctx->pb) < 0) {
                log_video_encoder("Failed to open dynamic buffer");
                ret = 12;
                break;
            }
        }

        if (avformat_write_header(fmt_ctx, nullptr) < 0) {
            log_video_encoder("Failed to write header");
            ret = 13;
            break;
        }

        frame = av_frame_alloc();
        pkt = av_packet_alloc();
        if (!frame || !pkt) {
            log_video_encoder("Failed to allocate frame/packet");
            ret = 14;
            break;
        }

        frame->format = codec_ctx->pix_fmt;
        frame->width = codec_ctx->width;
        frame->height = codec_ctx->height;

        if (av_frame_get_buffer(frame, 32) < 0) {
            log_video_encoder("Failed to allocate frame buffer");
            ret = 15;
            break;
        }

        sws_ctx = sws_getContext(
            target_width,
            target_height,
            AV_PIX_FMT_RGBA,
            codec_ctx->width,
            codec_ctx->height,
            codec_ctx->pix_fmt,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
        );

        if (!sws_ctx) {
            log_video_encoder("Failed to create scale context");
            ret = 16;
            break;
        }

        int64_t pts = 0;
        for (int i = 0; i < p_frames.size(); i++) {
            Ref<Image> img = p_frames[i];
            if (img.is_null()) {
                continue;
            }

            Ref<Image> scaled = img;
            if (img->get_width() != target_width || img->get_height() != target_height) {
                scaled = img->duplicate();
                scaled->resize(target_width, target_height, Image::INTERPOLATE_LANCZOS);
            }

            PackedByteArray rgba = scaled->get_data();
            AVFrame src_frame;
            memset(&src_frame, 0, sizeof(src_frame));
            src_frame.format = AV_PIX_FMT_RGBA;
            src_frame.width = target_width;
            src_frame.height = target_height;
            av_image_fill_arrays(src_frame.data, src_frame.linesize, rgba.ptr(), AV_PIX_FMT_RGBA, target_width, target_height, 1);

            if (av_frame_make_writable(frame) < 0) {
                log_video_encoder("Frame not writable");
                ret = 17;
                break;
            }

            sws_scale(
                sws_ctx,
                src_frame.data,
                src_frame.linesize,
                0,
                target_height,
                frame->data,
                frame->linesize
            );

            frame->pts = pts++;

            if (avcodec_send_frame(codec_ctx, frame) < 0) {
                log_video_encoder("Failed to send frame to encoder");
                ret = 18;
                break;
            }

            while (true) {
                const int receive_ret = avcodec_receive_packet(codec_ctx, pkt);
                if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
                    break;
                }
                if (receive_ret < 0) {
                    log_video_encoder("Failed to receive packet");
                    ret = 19;
                    break;
                }
                pkt->stream_index = stream->index;
                av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                if (av_interleaved_write_frame(fmt_ctx, pkt) < 0) {
                    log_video_encoder("Failed to write frame");
                    ret = 20;
                    break;
                }
                av_packet_unref(pkt);
            }

            if (ret != 0) {
                break;
            }
        }

        if (ret == 0) {
            // flush
            avcodec_send_frame(codec_ctx, nullptr);
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                pkt->stream_index = stream->index;
                av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
        }

        if (ret == 0) {
            av_write_trailer(fmt_ctx);
        }

        if (ret == 0 && r_bytes) {
            if (p_path.is_empty()) {
                uint8_t *buffer = nullptr;
                int size = avio_close_dyn_buf(fmt_ctx->pb, &buffer);
                if (size > 0 && buffer) {
                    r_bytes->resize(size);
                    memcpy(r_bytes->ptrw(), buffer, size);
                }
                av_free(buffer);
                fmt_ctx->pb = nullptr;
            }
        }

    } while (false);

    if (sws_ctx) {
        sws_freeContext(sws_ctx);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (pkt) {
        av_packet_free(&pkt);
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (fmt_ctx) {
        if (fmt_ctx->pb) {
            if (!p_path.is_empty()) {
                avio_closep(&fmt_ctx->pb);
            } else {
                uint8_t *discard = nullptr;
                avio_close_dyn_buf(fmt_ctx->pb, &discard);
                av_free(discard);
            }
        }
        avformat_free_context(fmt_ctx);
    }

    return ret;
}

int FFmpegVideoEncoder::encode_internal(const Array &p_frames, const String &p_path, PackedByteArray *r_bytes) {
    Vector<Ref<Image>> frames;
    frames.resize(p_frames.size());
    for (int i = 0; i < p_frames.size(); i++) {
        frames.write[i] = image_from_any(p_frames[i]);
    }
    return encode_internal(frames, p_path, r_bytes);
}

} // namespace godot
