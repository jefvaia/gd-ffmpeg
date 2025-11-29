#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

namespace godot {

class FFmpegVideoDecoder : public RefCounted {
    GDCLASS(FFmpegVideoDecoder, RefCounted);

private:
    String preferred_codec;
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *sws_ctx = nullptr;
    int sws_src_w = 0;
    int sws_src_h = 0;
    AVPixelFormat output_pix_fmt = AV_PIX_FMT_RGBA;
    int output_width = 0;
    int output_height = 0;
    int video_stream_index = -1;

    PackedByteArray source_bytes;
    size_t source_pos = 0;

    int open_input_internal(const char *p_path);
    void clear_resources();
    Ref<Image> convert_frame(AVFrame *p_src);
    static AVPixelFormat pixel_format_from_string(const String &p_name);
    static String pixel_format_to_string(AVPixelFormat p_fmt);

protected:
    static void _bind_methods();

public:
    ~FFmpegVideoDecoder();

    void set_preferred_codec(const String &p_name);
    void set_output_pixel_format(const String &p_fmt);
    void set_output_resolution(int p_width, int p_height);

    int load_file(const String &p_path);
    int load_bytes(const PackedByteArray &p_bytes);

    // Decode all frames to Images.
    Array decode_frames();

    // Convenience: decode and pack frames as raw RGBA bytes per frame.
    Array decode_frame_bytes();

    // Convenience: decode frames into Texture2D resources.
    Array decode_textures();

    Array decode_frames_from_file(const String &p_path);
    Array decode_frame_bytes_from_file(const String &p_path);
    Array decode_textures_from_file(const String &p_path);
};

} // namespace godot
