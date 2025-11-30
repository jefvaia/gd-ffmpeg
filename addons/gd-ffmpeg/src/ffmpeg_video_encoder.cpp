#include "ffmpeg_video_encoder.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/dictionary.hpp>
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

FFmpegVideoEncoder::~FFmpegVideoEncoder() {
    reset_state();
}

void FFmpegVideoEncoder::_bind_methods() {
    // Configuration setters
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

    // Streaming controls
    ClassDB::bind_method(D_METHOD("begin", "path", "stream_peer", "file_access"), &FFmpegVideoEncoder::begin);
    ClassDB::bind_method(D_METHOD("push_image", "image"), &FFmpegVideoEncoder::push_image);
    ClassDB::bind_method(D_METHOD("push_frame_bytes", "bytes", "width", "height", "format"), &FFmpegVideoEncoder::push_frame_bytes);
    ClassDB::bind_method(D_METHOD("push_frame_bytes_strided", "bytes", "width", "height", "line_sizes", "format"), &FFmpegVideoEncoder::push_frame_bytes_strided);
    ClassDB::bind_method(D_METHOD("push_frame_stream_peer", "stream_peer", "bytes", "width", "height", "format"), &FFmpegVideoEncoder::push_frame_stream_peer);
    ClassDB::bind_method(D_METHOD("end"), &FFmpegVideoEncoder::end);

    ClassDB::bind_method(D_METHOD("set_packet_callback", "callable"), &FFmpegVideoEncoder::set_packet_callback);
    ClassDB::bind_method(D_METHOD("get_packet_callback"), &FFmpegVideoEncoder::get_packet_callback);
    ClassDB::bind_method(D_METHOD("drain_packets"), &FFmpegVideoEncoder::drain_packets);

    // Encoding entry points
    ClassDB::bind_method(D_METHOD("encode_images_to_file", "frames", "path"),
        &FFmpegVideoEncoder::encode_images_to_file);
    ClassDB::bind_method(D_METHOD("encode_images", "frames"),
        &FFmpegVideoEncoder::encode_images);

    // (No ADD_PROPERTY calls here, so Godot won't complain about missing getters
    //  or wrong setter signatures. If you later add getters + Vector2i setters,
    //  you can reintroduce properties properly.)
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
            if (Texture2D *base_tex = Object::cast_to<Texture2D>(obj)) {
                return base_tex->get_image();
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

void FFmpegVideoEncoder::reset_state() {
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (pkt) {
        av_packet_free(&pkt);
        pkt = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (custom_io) {
        av_freep(&custom_io->buffer);
        avio_context_free(&custom_io);
    }
    custom_io_buffer = nullptr;
    stream = nullptr;
    header_written = false;
    pts_counter = 0;

    if (format_ctx) {
        if (format_ctx->pb && !output_path.is_empty()) {
            avio_closep(&format_ctx->pb);
        }
        format_ctx->pb = nullptr;
        avformat_free_context(format_ctx);
        format_ctx = nullptr;
    }

    pending_output.clear();
    full_output.clear();
    buffered_packets.clear();
    collecting_output = false;
    output_stream_peer = Ref<StreamPeer>();
    output_file_access = Ref<FileAccess>();
    output_path = String();
}

int FFmpegVideoEncoder::write_callback(void *p_opaque, uint8_t *p_buf, int p_buf_size) {
    FFmpegVideoEncoder *encoder = reinterpret_cast<FFmpegVideoEncoder *>(p_opaque);
    if (!encoder || p_buf_size <= 0) {
        return 0;
    }

    PackedByteArray chunk;
    chunk.resize(p_buf_size);
    memcpy(chunk.ptrw(), p_buf, p_buf_size);

    const int old_size = encoder->pending_output.size();
    encoder->pending_output.resize(old_size + p_buf_size);
    memcpy(encoder->pending_output.ptrw() + old_size, p_buf, p_buf_size);

    if (encoder->collecting_output) {
        const int base = encoder->full_output.size();
        encoder->full_output.resize(base + p_buf_size);
        memcpy(encoder->full_output.ptrw() + base, p_buf, p_buf_size);
    }

    if (encoder->output_stream_peer.is_valid()) {
        encoder->output_stream_peer->put_data(chunk);
    }

    if (encoder->output_file_access.is_valid()) {
        encoder->output_file_access->store_buffer(chunk);
    }

    return p_buf_size;
}

int FFmpegVideoEncoder::initialize_encoder(int p_width, int p_height, AVPixelFormat p_src_format) {
    if (format_ctx) {
        return 0;
    }

    const int target_width = width > 0 ? width : p_width;
    const int target_height = height > 0 ? height : p_height;
    if (target_width <= 0 || target_height <= 0) {
        log_video_encoder("Invalid dimensions for encoder initialization");
        return 1;
    }

    const CharString codec_utf8 = codec_name.utf8();
    const AVCodec *codec = avcodec_find_encoder_by_name(codec_utf8.get_data());
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec) {
        log_video_encoder("Encoder not found");
        return 2;
    }

    const bool use_custom_io = output_stream_peer.is_valid() || output_file_access.is_valid() || output_path.is_empty();
    AVOutputFormat *output_format = nullptr;
    if (!use_custom_io) {
        if (avformat_alloc_output_context2(&format_ctx, nullptr, nullptr, output_path.utf8().get_data()) < 0) {
            log_video_encoder("Failed to allocate output context from path");
            return 3;
        }
    } else {
        const CharString muxer_utf8 = muxer_name.utf8();
        output_format = av_guess_format(muxer_utf8.get_data(), nullptr, nullptr);
        if (!output_format) {
            log_video_encoder("Could not guess muxer format: " + muxer_name);
            return 4;
        }
        format_ctx = avformat_alloc_context();
        format_ctx->oformat = output_format;
    }

    if (!format_ctx) {
        log_video_encoder("Failed to create format context");
        return 5;
    }

    stream = avformat_new_stream(format_ctx, nullptr);
    if (!stream) {
        log_video_encoder("Failed to create stream");
        return 6;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        log_video_encoder("Failed to allocate codec context");
        return 7;
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

    if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        log_video_encoder("Failed to open codec");
        return 8;
    }

    if (avcodec_parameters_from_context(stream->codecpar, codec_ctx) < 0) {
        log_video_encoder("Failed to copy codec parameters");
        return 9;
    }
    stream->time_base = codec_ctx->time_base;

    if (!use_custom_io) {
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&format_ctx->pb, output_path.utf8().get_data(), AVIO_FLAG_WRITE) < 0) {
                log_video_encoder("Could not open output file");
                return 10;
            }
        }
    } else {
        const int buffer_size = 4 * 1024;
        custom_io_buffer = static_cast<uint8_t *>(av_malloc(buffer_size));
        if (!custom_io_buffer) {
            log_video_encoder("Failed to allocate custom IO buffer");
            return 11;
        }
        custom_io = avio_alloc_context(custom_io_buffer, buffer_size, 1, this, nullptr, &FFmpegVideoEncoder::write_callback, nullptr);
        if (!custom_io) {
            log_video_encoder("Failed to allocate custom IO context");
            return 12;
        }
        format_ctx->pb = custom_io;
        format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
        collecting_output = output_path.is_empty() && output_stream_peer.is_null() && output_file_access.is_null();
    }

    if (avformat_write_header(format_ctx, nullptr) < 0) {
        log_video_encoder("Failed to write header");
        return 13;
    }
    header_written = true;

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) {
        log_video_encoder("Failed to allocate frame/packet");
        return 14;
    }

    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;

    if (av_frame_get_buffer(frame, 32) < 0) {
        log_video_encoder("Failed to allocate frame buffer");
        return 15;
    }

    sws_ctx = sws_getCachedContext(
        nullptr,
        p_width,
        p_height,
        p_src_format,
        codec_ctx->width,
        codec_ctx->height,
        codec_ctx->pix_fmt,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );

    if (!sws_ctx && (p_src_format != codec_ctx->pix_fmt || p_width != codec_ctx->width || p_height != codec_ctx->height)) {
        log_video_encoder("Failed to create scale context");
        return 16;
    }

    return 0;
}

PackedByteArray FFmpegVideoEncoder::encode_frame_internal(const uint8_t *p_src, int p_src_size, int p_width, int p_height, AVPixelFormat p_src_format, const int *p_linesizes, int p_linesize_count) {
    PackedByteArray output;
    pending_output.clear();

    const int final_width = p_width > 0 ? p_width : width;
    const int final_height = p_height > 0 ? p_height : height;

    if (!p_src || final_width <= 0 || final_height <= 0) {
        log_video_encoder("Invalid frame payload");
        return output;
    }

    const int init_status = initialize_encoder(final_width, final_height, p_src_format);
    if (init_status != 0) {
        return output;
    }

    AVFrame src_frame;
    memset(&src_frame, 0, sizeof(src_frame));
    src_frame.format = p_src_format;
    src_frame.width = final_width;
    src_frame.height = final_height;

    int linesizes[AV_NUM_DATA_POINTERS] = {0};
    const bool custom_linesizes = p_linesizes && p_linesize_count > 0;
    if (custom_linesizes) {
        const int to_copy = p_linesize_count < AV_NUM_DATA_POINTERS ? p_linesize_count : AV_NUM_DATA_POINTERS;
        for (int i = 0; i < to_copy; i++) {
            linesizes[i] = p_linesizes[i];
        }
    } else {
        if (av_image_fill_linesizes(linesizes, p_src_format, final_width) < 0) {
            log_video_encoder("Could not compute line sizes for frame");
            return output;
        }
    }

    uint8_t *temp_data[AV_NUM_DATA_POINTERS] = {nullptr};
    const int required_size = av_image_fill_pointers(temp_data, p_src_format, final_height, const_cast<uint8_t *>(p_src), linesizes);
    if (required_size < 0 || required_size > p_src_size) {
        log_video_encoder("Invalid buffer/stride combination for frame");
        return output;
    }

    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        src_frame.data[i] = temp_data[i];
        src_frame.linesize[i] = linesizes[i];
    }

    SwsContext *active_sws = sws_ctx;
    if (p_src_format != codec_ctx->pix_fmt || p_width != codec_ctx->width || p_height != codec_ctx->height) {
        active_sws = sws_getCachedContext(
            sws_ctx,
            final_width,
            final_height,
            p_src_format,
            codec_ctx->width,
            codec_ctx->height,
            codec_ctx->pix_fmt,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
        );
        sws_ctx = active_sws;
    }

    if (av_frame_make_writable(frame) < 0) {
        log_video_encoder("Frame not writable");
        return output;
    }

    if (active_sws) {
        sws_scale(
            active_sws,
            src_frame.data,
            src_frame.linesize,
            0,
            final_height,
            frame->data,
            frame->linesize
        );
    } else {
        av_image_copy(frame->data, frame->linesize, (const uint8_t **)src_frame.data, src_frame.linesize, codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height);
    }

    frame->pts = pts_counter++;

    if (avcodec_send_frame(codec_ctx, frame) < 0) {
        log_video_encoder("Failed to send frame to encoder");
        return output;
    }

    while (true) {
        const int receive_ret = avcodec_receive_packet(codec_ctx, pkt);
        if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
            break;
        }
        if (receive_ret < 0) {
            log_video_encoder("Failed to receive packet");
            break;
        }
        dispatch_packet(pkt);
        pkt->stream_index = stream->index;
        av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
        if (av_interleaved_write_frame(format_ctx, pkt) < 0) {
            log_video_encoder("Failed to write frame");
            av_packet_unref(pkt);
            break;
        }
        av_packet_unref(pkt);
    }

    output = pending_output;
    return output;
}

int FFmpegVideoEncoder::flush_internal(PackedByteArray &r_output) {
    pending_output.clear();
    if (!codec_ctx || !pkt) {
        return 0;
    }

    avcodec_send_frame(codec_ctx, nullptr);
    while (true) {
        const int receive_ret = avcodec_receive_packet(codec_ctx, pkt);
        if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
            break;
        }
        if (receive_ret < 0) {
            break;
        }
        dispatch_packet(pkt);
        pkt->stream_index = stream->index;
        av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
        av_interleaved_write_frame(format_ctx, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(format_ctx);
    r_output = pending_output;
    return 0;
}

int FFmpegVideoEncoder::begin(const String &p_path, const Ref<StreamPeer> &p_stream_peer, const Ref<FileAccess> &p_file_access) {
    reset_state();
    output_path = p_path;
    output_stream_peer = p_stream_peer;
    output_file_access = p_file_access;
    return 0;
}

PackedByteArray FFmpegVideoEncoder::push_image(const Ref<Image> &p_image) {
    PackedByteArray output;
    if (p_image.is_null()) {
        log_video_encoder("push_image received null image");
        return output;
    }

    Ref<Image> img = p_image;
    if (width > 0 && height > 0 && (p_image->get_width() != width || p_image->get_height() != height)) {
        img = p_image->duplicate();
        img->resize(width, height, Image::INTERPOLATE_LANCZOS);
    }

    Ref<Image> converted = img->duplicate();
    converted->convert(Image::FORMAT_RGBA8);
    PackedByteArray rgba = converted->get_data();
    return encode_frame_internal(rgba.ptr(), rgba.size(), converted->get_width(), converted->get_height(), AV_PIX_FMT_RGBA);
}

PackedByteArray FFmpegVideoEncoder::push_frame_bytes(const PackedByteArray &p_bytes, int p_width, int p_height, const String &p_format) {
    PackedByteArray output;
    if (p_bytes.is_empty()) {
        return output;
    }
    const AVPixelFormat src_fmt = pixel_format_from_string(p_format);
    if (src_fmt == AV_PIX_FMT_NONE) {
        log_video_encoder("Unknown pixel format: " + p_format);
        return output;
    }
    return encode_frame_internal(p_bytes.ptr(), p_bytes.size(), p_width, p_height, src_fmt);
}

PackedByteArray FFmpegVideoEncoder::push_frame_bytes_strided(const PackedByteArray &p_bytes, int p_width, int p_height, const PackedInt32Array &p_line_sizes, const String &p_format) {
    PackedByteArray output;
    if (p_bytes.is_empty()) {
        return output;
    }

    const AVPixelFormat src_fmt = pixel_format_from_string(p_format);
    if (src_fmt == AV_PIX_FMT_NONE) {
        log_video_encoder("Unknown pixel format: " + p_format);
        return output;
    }

    return encode_frame_internal(p_bytes.ptr(), p_bytes.size(), p_width, p_height, src_fmt, p_line_sizes.ptr(), p_line_sizes.size());
}

PackedByteArray FFmpegVideoEncoder::push_frame_stream_peer(const Ref<StreamPeer> &p_stream_peer, int p_bytes, int p_width, int p_height, const String &p_format) {
    PackedByteArray output;
    if (p_stream_peer.is_null()) {
        log_video_encoder("StreamPeer is null");
        return output;
    }

    int to_read = p_bytes;
    if (to_read <= 0) {
        to_read = p_stream_peer->get_available_bytes();
    }
    if (to_read <= 0) {
        return output;
    }

    PackedByteArray raw = p_stream_peer->get_data(to_read);
    return push_frame_bytes(raw, p_width, p_height, p_format);
}

PackedByteArray FFmpegVideoEncoder::end() {
    PackedByteArray output;
    if (!format_ctx) {
        return output;
    }

    flush_internal(output);
    if (collecting_output) {
        output = full_output;
    }

    reset_state();
    return output;
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

    begin(p_path);
    for (int i = 0; i < p_frames.size(); i++) {
        const PackedByteArray chunk = push_image(p_frames[i]);
        if (chunk.is_empty() && header_written == false) {
            return 1;
        }
    }

    const bool started = header_written || format_ctx != nullptr;
    PackedByteArray final = end();
    if (r_bytes) {
        *r_bytes = final;
    }
    return started ? 0 : 1;
}

int FFmpegVideoEncoder::encode_internal(const Array &p_frames, const String &p_path, PackedByteArray *r_bytes) {
    Vector<Ref<Image>> frames;
    frames.resize(p_frames.size());
    for (int i = 0; i < p_frames.size(); i++) {
        frames.write[i] = image_from_any(p_frames[i]);
    }
    return encode_internal(frames, p_path, r_bytes);
}

void FFmpegVideoEncoder::dispatch_packet(const AVPacket *p_packet) {
    if (!p_packet) {
        return;
    }

    Dictionary payload;
    PackedByteArray data;
    if (p_packet->size > 0 && p_packet->data) {
        data.resize(p_packet->size);
        memcpy(data.ptrw(), p_packet->data, p_packet->size);
    }

    payload["data"] = data;
    payload["pts"] = p_packet->pts;
    payload["dts"] = p_packet->dts;
    payload["duration"] = p_packet->duration;
    payload["is_key"] = (p_packet->flags & AV_PKT_FLAG_KEY) != 0;
    payload["time_base_num"] = codec_ctx ? codec_ctx->time_base.num : 0;
    payload["time_base_den"] = codec_ctx ? codec_ctx->time_base.den : 0;
    payload["stream_index"] = stream ? stream->index : -1;

    if (packet_callback.is_valid()) {
        packet_callback.call(payload);
    } else {
        buffered_packets.append(payload);
    }
}

void FFmpegVideoEncoder::set_packet_callback(const Callable &p_callable) {
    packet_callback = p_callable;
}

Callable FFmpegVideoEncoder::get_packet_callback() const {
    return packet_callback;
}

Array FFmpegVideoEncoder::drain_packets() {
    Array out = buffered_packets;
    buffered_packets.clear();
    return out;
}

String FFmpegVideoEncoder::get_codec_name() const {
    return codec_name;
}

String FFmpegVideoEncoder::get_pixel_format() const {
    return pixel_format_to_string(target_pix_fmt);
}

int FFmpegVideoEncoder::get_frame_rate() const {
    return frame_rate;
}

void FFmpegVideoEncoder::set_resolution_vec(const Vector2i &p_size) {
    set_resolution(p_size.x, p_size.y);
}

Vector2i FFmpegVideoEncoder::get_resolution() const {
    return Vector2i(width, height);
}

int64_t FFmpegVideoEncoder::get_bit_rate() const {
    return bit_rate;
}

String FFmpegVideoEncoder::get_rate_control_mode() const {
    return rate_control_mode;
}

int FFmpegVideoEncoder::get_quality() const {
    return quality;
}

String FFmpegVideoEncoder::get_preset() const {
    return preset;
}

String FFmpegVideoEncoder::get_profile() const {
    return profile;
}

int FFmpegVideoEncoder::get_keyframe_interval() const {
    return keyframe_interval;
}

} // namespace godot
