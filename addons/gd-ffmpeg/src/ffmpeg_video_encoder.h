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
    #include <libavutil/error.h>
    #include <libavutil/opt.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

namespace godot {

class FFmpegVideoEncoder : public RefCounted {
    GDCLASS(FFmpegVideoEncoder, RefCounted);

private:
    String codec_name = "libx264";
    AVPixelFormat target_pix_fmt = AV_PIX_FMT_YUV420P;
    int frame_rate = 30;
    int width = 0;
    int height = 0;
    int64_t bit_rate = 4000000;
    int quality = 23;
    String rate_control_mode = "vbr";
    String preset = "medium";
    String profile;
    int keyframe_interval = 12;

    int encode_internal(const Vector<Ref<Image>> &p_frames, const String &p_path, PackedByteArray *r_bytes);
    int encode_internal(const Array &p_frames, const String &p_path, PackedByteArray *r_bytes);
    static Ref<Image> image_from_any(const Variant &p_value);
    static AVPixelFormat pixel_format_from_string(const String &p_name);
    static String pixel_format_to_string(AVPixelFormat p_fmt);

protected:
    static void _bind_methods();

public:
    void set_codec_name(const String &p_name);
    void set_pixel_format(const String &p_name);
    void set_frame_rate(int p_rate);
    void set_resolution(int p_width, int p_height);
    void set_bit_rate(int64_t p_bit_rate);
    void set_rate_control_mode(const String &p_mode);
    void set_quality(int p_quality);
    void set_preset(const String &p_preset);
    void set_profile(const String &p_profile);
    void set_keyframe_interval(int p_interval);

    // Encode an array of Image or ImageTexture frames into a video file.
    // Returns 0 on success.
    int encode_images_to_file(const Array &p_frames, const String &p_path);

    // Encode frames to an in-memory container. Returns an empty array on failure.
    PackedByteArray encode_images(const Array &p_frames);
};

} // namespace godot
