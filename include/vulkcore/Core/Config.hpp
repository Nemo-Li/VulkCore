#pragma once

#include <cstdint>

#ifndef VULKCORE_SHADER_DIR
#define VULKCORE_SHADER_DIR "shaders"
#endif

#ifndef VULKCORE_MODEL_PATH
#define VULKCORE_MODEL_PATH "assets/models/mill-scene.fbx"
#endif

#ifndef VULKCORE_CONFIG_PATH
#define VULKCORE_CONFIG_PATH "vulkcore.ini"
#endif

namespace VulkCore {

inline constexpr uint32_t kWindowWidth = 1920;
inline constexpr uint32_t kWindowHeight = 1080;
inline constexpr uint32_t kMaxFramesInFlight = 2;
inline constexpr char kShaderBinaryDirectory[] = VULKCORE_SHADER_DIR;
inline constexpr char kModelPath[] = VULKCORE_MODEL_PATH;
inline constexpr char kConfigPath[] = VULKCORE_CONFIG_PATH;

} // namespace VulkCore
