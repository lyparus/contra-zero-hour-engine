# miniaudio v0.11.25 (https://github.com/mackron/miniaudio), public domain or MIT-0, see the end of miniaudio.h.
# Audio backend of the x64 build, where the Miles Sound System does not exist.
# The implementation is compiled in Core/GameEngineDevice/Source/MiniAudioDevice/MiniAudioManager.cpp.
add_library(deps_miniaudio INTERFACE)

target_include_directories(deps_miniaudio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
