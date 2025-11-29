#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/variant/audio_frame.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
}

namespace godot {

class FFmpegAudioDecoder : public RefCounted {
    GDCLASS(FFmpegAudioDecoder, RefCounted);

private:
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    SwrContext *swr_ctx = nullptr;
    AVIOContext *avio_ctx = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;

    int audio_stream_index = -1;
    int target_sample_rate = 0;
    int target_channels = 0;
    AVSampleFormat target_format = AV_SAMPLE_FMT_FLT;
    String input_codec_name;

    PackedByteArray source_bytes;
    size_t source_pos = 0;

    int open_input_internal(const char *p_path);
    int setup_resampler(const AVChannelLayout &p_src_layout);
    void clear_resources();

protected:
    static void _bind_methods();

public:
    FFmpegAudioDecoder();
    ~FFmpegAudioDecoder();

    void set_input_codec(const String &p_codec_name);
    void set_output_sample_rate(int p_rate);
    void set_output_channels(int p_channels);

    int load_file(const String &p_path);
    int load_bytes(const PackedByteArray &p_bytes);

    PackedFloat32Array decode_pcm();
    Array decode_audio_frames();
    Ref<AudioStreamWAV> decode_audio_stream();

    PackedFloat32Array decode_pcm_from_file(const String &p_path);
    Array decode_audio_frames_from_file(const String &p_path);
    Ref<AudioStreamWAV> decode_audio_stream_from_file(const String &p_path);

    int get_sample_rate() const { return target_sample_rate; }
    int get_channels() const { return target_channels; }
};

class FFmpegAudioTranscoder : public RefCounted {
    GDCLASS(FFmpegAudioTranscoder, RefCounted);

private:
    String input_codec;
    String output_codec;
    int output_sample_rate = 0;
    int output_channels = 0;

protected:
    static void _bind_methods();

public:
    void set_input_codec(const String &p_codec_name);
    void set_output_codec(const String &p_codec_name);
    void set_output_sample_rate(int p_rate);
    void set_output_channels(int p_channels);

    // Returns 0 on success.
    int transcode_file(const String &p_input_path, const String &p_output_path);
};

} // namespace godot
