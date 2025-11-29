extends Node

@export var input_wav_path: String = "res://test.wav"

func _ready() -> void:
	convert_wav_to_mp3_and_opus()


func convert_wav_to_mp3_and_opus() -> void:
	var wav_res = ResourceLoader.load(input_wav_path)
	if wav_res == null:
		push_error("Failed to load WAV at path: %s" % input_wav_path)
		return

	var wav = wav_res as AudioStreamWAV
	if wav == null:
		push_error("Resource is not an AudioStreamWAV: %s" % input_wav_path)
		return

	print("WAV mix rate:  ", wav.mix_rate)
	print("WAV is stereo: ", wav.stereo)
	print("WAV format:    ", wav.format)

	var sample_rate = wav.mix_rate
	var channels = 2 if wav.stereo else 1

	var mp3_path = input_wav_path.get_basename() + ".mp3"
	var opus_path = input_wav_path.get_basename() + ".opus"

	var mp3_err = encode_stream_to_file(
		wav,
		"libmp3lame",
		sample_rate,
		channels,
		320000,
		{},
		mp3_path
	)
	if mp3_err != 0:
		push_error("MP3 encode failed with code: %d" % mp3_err)
	else:
		print("MP3 written to: ", mp3_path)

	# Opus, e.g. 128 kbps
	var opus_options = {
		# "application": "audio",  # optional, depends on your FFmpeg build
		# "vbr": "on"             # optional
	}

	var opus_err = encode_stream_to_file(
		wav,
		"libopus",
		sample_rate,
		channels,
		128000,
		opus_options,
		opus_path
	)
	if opus_err != 0:
		push_error("Opus encode failed with code: %d" % opus_err)
	else:
		print("Opus written to: ", opus_path)


func encode_stream_to_file(
		stream: AudioStream,
		codec_name: String,
		sample_rate: int,
		channels: int,
		bit_rate: int,
		options: Dictionary,
		output_path: String
	) -> int:
	var encoder = FFmpegAudioEncoder.new()
	var setup_err = encoder.setup_encoder(codec_name, sample_rate, channels, bit_rate, options)
	if setup_err != 0:
		push_error("setup_encoder(%s) failed with code: %d" % [codec_name, setup_err])
		return setup_err

	var encode_err = encoder.encode_audio_stream_to_file(stream, output_path)
	if encode_err != 0:
		push_error("encode_audio_stream_to_file(%s) failed with code: %d" % [codec_name, encode_err])
	return encode_err
