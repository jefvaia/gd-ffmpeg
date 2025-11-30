#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/stream_peer.hpp>
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

    // Streaming state
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    AVStream *stream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    SwsContext *sws_ctx = nullptr;
    AVIOContext *custom_io = nullptr;
    uint8_t *custom_io_buffer = nullptr;
    int64_t pts_counter = 0;
    bool collecting_output = false;
    bool header_written = false;
    PackedByteArray pending_output;
    PackedByteArray full_output;
    Ref<StreamPeer> output_stream_peer;
    Ref<FileAccess> output_file_access;
    String output_path;
    String muxer_name = "mp4";

    int initialize_encoder(int p_width, int p_height, AVPixelFormat p_src_format);
    void reset_state();
    PackedByteArray encode_frame_internal(const uint8_t *p_src, int p_src_size, int p_width, int p_height, AVPixelFormat p_src_format);
    int flush_internal(PackedByteArray &r_output);
    static int write_callback(void *p_opaque, uint8_t *p_buf, int p_buf_size);

    int encode_internal(const Vector<Ref<Image>> &p_frames, const String &p_path, PackedByteArray *r_bytes);
    int encode_internal(const Array &p_frames, const String &p_path, PackedByteArray *r_bytes);
    static Ref<Image> image_from_any(const Variant &p_value);
    static AVPixelFormat pixel_format_from_string(const String &p_name);
    static String pixel_format_to_string(AVPixelFormat p_fmt);

protected:
    static void _bind_methods();

public:
    ~FFmpegVideoEncoder();

    void set_codec_name(const String &p_name);
    String get_codec_name() const;

    void set_pixel_format(const String &p_name);
    String get_pixel_format() const;

    void set_frame_rate(int p_rate);
    int get_frame_rate() const;

    void set_resolution(int p_width, int p_height);
    void set_resolution_vec(const Vector2i &p_size);
    Vector2i get_resolution() const;

    void set_bit_rate(int64_t p_bit_rate);
    int64_t get_bit_rate() const;

    void set_rate_control_mode(const String &p_mode);
    String get_rate_control_mode() const;

    void set_quality(int p_quality);
    int get_quality() const;

    void set_preset(const String &p_preset);
    String get_preset() const;

    void set_profile(const String &p_profile);
    String get_profile() const;

    void set_keyframe_interval(int p_interval);
    int get_keyframe_interval() const;

    int begin(const String &p_path = String(), const Ref<StreamPeer> &p_stream_peer = Ref<StreamPeer>(), const Ref<FileAccess> &p_file_access = Ref<FileAccess>());
    PackedByteArray push_image(const Ref<Image> &p_image);
    PackedByteArray push_frame_bytes(const PackedByteArray &p_bytes, int p_width, int p_height, const String &p_format = "rgba");
    PackedByteArray push_frame_stream_peer(const Ref<StreamPeer> &p_stream_peer, int p_bytes, int p_width, int p_height, const String &p_format = "rgba");
    PackedByteArray end();

    // Encode an array of Image or ImageTexture frames into a video file.
    // Returns 0 on success.
    int encode_images_to_file(const Array &p_frames, const String &p_path);

    // Encode frames to an in-memory container. Returns an empty array on failure.
    PackedByteArray encode_images(const Array &p_frames);
};

} // namespace godot
