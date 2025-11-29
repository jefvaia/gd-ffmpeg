#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "ffmpeg_audio_encoder.h"
#include "ffmpeg_audio_decoder.h"

using namespace godot;

void initialize_ffmpeg_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_class<FFmpegAudioEncoder>();
    ClassDB::register_class<FFmpegAudioDecoder>();
    ClassDB::register_class<FFmpegAudioTranscoder>();
}

void uninitialize_ffmpeg_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    // Nothing to do for now
}

extern "C" {

GDExtensionBool GDE_EXPORT gdffmpeg_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization
) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_ffmpeg_module);
    init_obj.register_terminator(uninitialize_ffmpeg_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}

} // extern "C"
