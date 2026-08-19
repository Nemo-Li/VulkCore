#pragma once

#include <cstdint>

#ifndef VULKCORE_SHADER_DIR
#define VULKCORE_SHADER_DIR "shaders"
#endif

#ifndef VULKCORE_TEXTURE_PATH
#define VULKCORE_TEXTURE_PATH "assets/textures/vulkcore_demo_texture.png"
#endif

namespace VulkCore {

inline constexpr uint32_t kWindowWidth = 800;
inline constexpr uint32_t kWindowHeight = 600;
inline constexpr uint32_t kMaxFramesInFlight = 2;
inline constexpr char kShaderBinaryDirectory[] = VULKCORE_SHADER_DIR;
inline constexpr char kTexturePath[] = VULKCORE_TEXTURE_PATH;

} // namespace VulkCore
