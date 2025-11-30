# gd-ffmpeg

This repository exposes a small set of FFmpeg-powered encoders to Godot through a GDExtension module. Both the audio and video encoders now accept bitrate and quality controls so users can balance file size and fidelity per project.

## Audio encoding

`FFmpegAudioEncoder.setup_encoder(codec_name, sample_rate, channels, bit_rate, options = {})` accepts an optional `Dictionary` of encoder hints that are forwarded to FFmpeg via `av_opt_set` before opening the codec:

- `bit_rate` (int): Overrides the function argument when present.
- `bitrate_mode` (String): `"cbr"` (default) or `"vbr"`. VBR enables codec-specific VBR knobs.
- `quality` (int): Passed to the encoder as both `compression_level` and `q` when `bitrate_mode` is `"vbr"`.
- `profile` (String): Sets the encoder profile if the codec supports it.
- `preset` (String): Sets the encoder preset when available.

Example:

```gdscript
var audio = FFmpegAudioEncoder.new()
var options = {
    "bitrate_mode": "vbr",
    "quality": 4,
    "profile": "aac_low"
}
var result = audio.setup_encoder("aac", 48000, 2, 128000, options)
```

Godot-native inputs are supported alongside raw PCM buffers:

```gdscript
# Encode from AudioFrame arrays
var encoded = audio.encode_audio_frames(my_frames)
encoded.append_array(audio.flush())

# Encode from an AudioStreamWAV, muxed directly to a file
audio.encode_audio_stream_to_file(my_wav_stream, "user://encoded.aac")
```

When feeding data incrementally (for example, microphone captures or streamed samples), pass raw float32 PCM bytes directly:

```gdscript
var bytes = my_stream_peer.get_data(my_stream_peer.get_available_bytes())
var packet_chunk = audio.encode_bytes(bytes)
packet_chunk.append_array(audio.flush())

# StreamPeer input/output is also supported for live pipelines
var packet_chunk_2 = audio.encode_stream_peer(my_stream_peer)
audio.encode_pcm_bytes_to_stream_peer(bytes, my_output_stream)
```

If you want to write to a custom `FileAccess` (for example, a sandboxed or virtual filesystem), use the dedicated helpers instead of path-based writes:

```gdscript
var file := FileAccess.open("user://live.opus", FileAccess.WRITE)
audio.encode_pcm_to_file_access(my_pcm_floats, file)
```

## Video encoding

`FFmpegVideoEncoder` exposes setters for common rate control options that are applied to the codec context and its private data before the encoder is opened and parameters are copied to the muxed stream:

- `set_bit_rate(bps)`: Used when `rate_control_mode` is `"cbr"` (default `4_000_000`).
- `set_rate_control_mode(mode)`: `"vbr"` (default, CRF-based) or `"cbr"`.
- `set_quality(value)`: CRF value used when `rate_control_mode` is `"vbr"` (default `23`).
- `set_preset(preset)`: Forwarded to the encoder (default `"medium"`).
- `set_profile(profile)`: Optional encoder profile string.
- `set_keyframe_interval(interval)`: GOP size/keyframe spacing (default `12`).

Usage example:

```gdscript
var video = FFmpegVideoEncoder.new()
video.set_codec_name("libx264")
video.set_resolution(1920, 1080)
video.set_frame_rate(30)
video.set_rate_control_mode("vbr")
video.set_quality(20)
video.set_preset("faster")
video.set_keyframe_interval(60)
var bytes = video.encode_images(frame_array)
```

`encode_images` accepts `Image`, `Texture2D`, and filesystem paths (loaded into an Image) so Godot textures can be passed directly. Use `encode_images_to_file` to mux frames to disk while keeping `encode_images` for in-memory buffers.

Defaults aim for a reasonable balance between file size and quality but can be tuned per stream to match project requirements.

## Decoding helpers

Audio and video decoders expose Godot-friendly outputs in addition to raw buffers:

```gdscript
var audio_decoder = FFmpegAudioDecoder.new()
var audio_stream = audio_decoder.decode_audio_stream_from_file("res://input.ogg")

var video_decoder = FFmpegVideoDecoder.new()
var textures = video_decoder.decode_textures_from_file("res://clip.mp4")
```

Raw PCM (`decode_pcm`) and RGBA byte arrays (`decode_frame_bytes`) are still available for advanced control.
