#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/error.h>
}

namespace godot {

class FFmpegAudioEncoder : public RefCounted {
    GDCLASS(FFmpegAudioEncoder, RefCounted);

private:
    AVCodecContext *codec_ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;

    int sample_rate = 0;
    int channels = 0;
    AVSampleFormat sample_fmt = AV_SAMPLE_FMT_NONE;
    bool initialized = false;

protected:
    static void _bind_methods();

public:
    FFmpegAudioEncoder();
    ~FFmpegAudioEncoder();

    // Returns 0 on success, non-zero on error.
    int setup_encoder(const String &p_codec_name, int p_sample_rate, int p_channels, int p_bit_rate, const Dictionary &p_options = Dictionary());

    // Input: interleaved float32 PCM (L, R, L, R, ...) with the same
    // sample rate and channel count passed to setup_encoder.
    PackedByteArray encode(const PackedFloat32Array &p_pcm_interleaved);

    // Flush any remaining buffered data from the encoder.
    PackedByteArray flush();
};

} // namespace godot
