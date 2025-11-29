@tool
extends EditorPlugin

var is_active = false

func _enter_tree():
	# The GDExtension (.gdextension file) loads automatically when the plugin is enabled.
	is_active = true
	print("GD FFmpeg plugin enabled.")

func _exit_tree():
	# Plugin disabled in the editor
	is_active = false
	print("GD FFmpeg plugin disabled.")
