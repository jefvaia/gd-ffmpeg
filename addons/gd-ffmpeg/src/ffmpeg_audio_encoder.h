#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
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

    // Convenience overloads for Godot-native data.
    PackedByteArray encode_audio_frames(const Array &p_frames);
    PackedByteArray encode_audio_stream(const Ref<AudioStream> &p_stream);

    // File output helpers.
    int encode_pcm_to_file(const PackedFloat32Array &p_pcm_interleaved, const String &p_path);
    int encode_audio_frames_to_file(const Array &p_frames, const String &p_path);
    int encode_audio_stream_to_file(const Ref<AudioStream> &p_stream, const String &p_path);

    // Flush any remaining buffered data from the encoder.
    PackedByteArray flush();
};

} // namespace godot
