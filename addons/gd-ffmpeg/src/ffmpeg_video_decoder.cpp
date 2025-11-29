#include "ffmpeg_video_decoder.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>

namespace godot {

static void log_video_decoder(const String &p_msg) {
    UtilityFunctions::print("[FFmpegVideoDecoder] ", p_msg);
}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
    clear_resources();
}

void FFmpegVideoDecoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_preferred_codec", "name"), &FFmpegVideoDecoder::set_preferred_codec);
    ClassDB::bind_method(D_METHOD("set_output_pixel_format", "fmt"), &FFmpegVideoDecoder::set_output_pixel_format);
    ClassDB::bind_method(D_METHOD("set_output_resolution", "width", "height"), &FFmpegVideoDecoder::set_output_resolution);
    ClassDB::bind_method(D_METHOD("load_file", "path"), &FFmpegVideoDecoder::load_file);
    ClassDB::bind_method(D_METHOD("load_bytes", "data"), &FFmpegVideoDecoder::load_bytes);
    ClassDB::bind_method(D_METHOD("decode_frames"), &FFmpegVideoDecoder::decode_frames);
    ClassDB::bind_method(D_METHOD("decode_frame_bytes"), &FFmpegVideoDecoder::decode_frame_bytes);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "preferred_codec"), "set_preferred_codec", String());
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "output_pixel_format"), "set_output_pixel_format", String());
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "output_resolution"), "set_output_resolution", Variant());
}

void FFmpegVideoDecoder::set_preferred_codec(const String &p_name) {
    preferred_codec = p_name;
}

void FFmpegVideoDecoder::set_output_pixel_format(const String &p_fmt) {
    const AVPixelFormat fmt = pixel_format_from_string(p_fmt);
    if (fmt != AV_PIX_FMT_NONE) {
        output_pix_fmt = fmt;
    }
}

void FFmpegVideoDecoder::set_output_resolution(int p_width, int p_height) {
    output_width = p_width;
    output_height = p_height;
}

AVPixelFormat FFmpegVideoDecoder::pixel_format_from_string(const String &p_name) {
    const String lower = p_name.to_lower();
    if (lower == "rgba") {
        return AV_PIX_FMT_RGBA;
    }
    if (lower == "rgb24") {
        return AV_PIX_FMT_RGB24;
    }
    if (lower == "yuv420p") {
        return AV_PIX_FMT_YUV420P;
    }
    return AV_PIX_FMT_NONE;
}

String FFmpegVideoDecoder::pixel_format_to_string(AVPixelFormat p_fmt) {
    switch (p_fmt) {
        case AV_PIX_FMT_RGBA: return "rgba";
        case AV_PIX_FMT_RGB24: return "rgb24";
        case AV_PIX_FMT_YUV420P: return "yuv420p";
        default: break;
    }
    return "unknown";
}

int FFmpegVideoDecoder::load_file(const String &p_path) {
    clear_resources();
    return open_input_internal(p_path.utf8().get_data());
}

int FFmpegVideoDecoder::load_bytes(const PackedByteArray &p_bytes) {
    clear_resources();
    source_bytes = p_bytes;
    source_pos = 0;
    uint8_t *buffer = static_cast<uint8_t *>(av_malloc(source_bytes.size()));
    if (!buffer) {
        return 1;
    }
    memcpy(buffer, source_bytes.ptr(), source_bytes.size());

    AVIOContext *avio_ctx = avio_alloc_context(
        buffer,
        source_bytes.size(),
        0,
        this,
        [](void *opaque, uint8_t *buf, int buf_size) -> int {
            FFmpegVideoDecoder *self = reinterpret_cast<FFmpegVideoDecoder *>(opaque);
            int64_t remaining = self->source_bytes.size() - self->source_pos;
            int to_copy = buf_size < remaining ? buf_size : static_cast<int>(remaining);
            if (to_copy <= 0) {
                return AVERROR_EOF;
            }
            memcpy(buf, self->source_bytes.ptr() + self->source_pos, to_copy);
            self->source_pos += to_copy;
            return to_copy;
        },
        nullptr,
        nullptr
    );

    if (!avio_ctx) {
        av_free(buffer);
        return 2;
    }

    format_ctx = avformat_alloc_context();
    format_ctx->pb = avio_ctx;
    format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    int ret = avformat_open_input(&format_ctx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        log_video_decoder("Failed to open input from memory");
        clear_resources();
        return 3;
    }

    return avformat_find_stream_info(format_ctx, nullptr);
}

int FFmpegVideoDecoder::open_input_internal(const char *p_path) {
    if (avformat_open_input(&format_ctx, p_path, nullptr, nullptr) < 0) {
        log_video_decoder("Failed to open input file");
        return 1;
    }
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        log_video_decoder("Failed to find stream info");
        return 2;
    }
    return 0;
}

void FFmpegVideoDecoder::clear_resources() {
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    sws_src_w = 0;
    sws_src_h = 0;
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (format_ctx) {
        if (format_ctx->pb && (format_ctx->flags & AVFMT_FLAG_CUSTOM_IO)) {
            av_free(format_ctx->pb->buffer);
            avio_context_free(&format_ctx->pb);
        }
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }
    video_stream_index = -1;
    source_bytes.clear();
    source_pos = 0;
}

Ref<Image> FFmpegVideoDecoder::convert_frame(AVFrame *p_src) {
    const int dst_width = output_width > 0 ? output_width : p_src->width;
    const int dst_height = output_height > 0 ? output_height : p_src->height;

    if (!sws_ctx || sws_src_w != p_src->width || sws_src_h != p_src->height) {
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
        }
        sws_ctx = sws_getContext(
            p_src->width,
            p_src->height,
            static_cast<AVPixelFormat>(p_src->format),
            dst_width,
            dst_height,
            output_pix_fmt,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
        );
        sws_src_w = p_src->width;
        sws_src_h = p_src->height;
    }

    if (!sws_ctx) {
        return Ref<Image>();
    }

    AVFrame *dst_frame = av_frame_alloc();
    if (!dst_frame) {
        return Ref<Image>();
    }
    dst_frame->format = output_pix_fmt;
    dst_frame->width = dst_width;
    dst_frame->height = dst_height;
    if (av_frame_get_buffer(dst_frame, 32) < 0) {
        av_frame_free(&dst_frame);
        return Ref<Image>();
    }

    sws_scale(
        sws_ctx,
        p_src->data,
        p_src->linesize,
        0,
        p_src->height,
        dst_frame->data,
        dst_frame->linesize
    );

    Ref<Image> img;
    if (output_pix_fmt == AV_PIX_FMT_RGBA) {
        PackedByteArray data;
        const int size = dst_frame->linesize[0] * dst_height;
        data.resize(size);
        memcpy(data.ptrw(), dst_frame->data[0], size);
        img.instantiate();
        img->create_from_data(dst_width, dst_height, false, Image::FORMAT_RGBA8, data);
    } else if (output_pix_fmt == AV_PIX_FMT_RGB24) {
        PackedByteArray data;
        const int size = dst_frame->linesize[0] * dst_height;
        data.resize(size);
        memcpy(data.ptrw(), dst_frame->data[0], size);
        img.instantiate();
        img->create_from_data(dst_width, dst_height, false, Image::FORMAT_RGB8, data);
    } else {
        // Fallback: convert to RGBA for Godot consumption
        PackedByteArray data;
        const int size = dst_frame->linesize[0] * dst_height;
        data.resize(size);
        memcpy(data.ptrw(), dst_frame->data[0], size);
        img.instantiate();
        img->create_from_data(dst_width, dst_height, false, Image::FORMAT_RGBA8, data);
    }

    av_frame_free(&dst_frame);
    return img;
}

Array FFmpegVideoDecoder::decode_frames() {
    Array frames;
    if (!format_ctx) {
        log_video_decoder("No input loaded");
        return frames;
    }

    if (video_stream_index < 0) {
        for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
            if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_index = static_cast<int>(i);
                break;
            }
        }
    }

    if (video_stream_index < 0) {
        log_video_decoder("No video stream found");
        return frames;
    }

    const AVStream *video_stream = format_ctx->streams[video_stream_index];

    const AVCodec *codec = nullptr;
    if (!preferred_codec.is_empty()) {
        codec = avcodec_find_decoder_by_name(preferred_codec.utf8().get_data());
    }
    if (!codec) {
        codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    }
    if (!codec) {
        log_video_decoder("Decoder not found");
        return frames;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        return frames;
    }
    avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        log_video_decoder("Failed to open codec");
        return frames;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();

    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(codec_ctx, packet) < 0) {
            av_packet_unref(packet);
            break;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(codec_ctx, frame) == 0) {
            Ref<Image> img = convert_frame(frame);
            if (img.is_valid()) {
                frames.append(img);
            }
            av_frame_unref(frame);
        }
    }

    // Flush
    avcodec_send_packet(codec_ctx, nullptr);
    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        Ref<Image> img = convert_frame(frame);
        if (img.is_valid()) {
            frames.append(img);
        }
        av_frame_unref(frame);
    }

    return frames;
}

Array FFmpegVideoDecoder::decode_frame_bytes() {
    Array frames;
    Array images = decode_frames();
    for (int i = 0; i < images.size(); i++) {
        Ref<Image> img = images[i];
        if (img.is_valid()) {
            frames.append(img->get_data());
        }
    }
    return frames;
}

} // namespace godot
