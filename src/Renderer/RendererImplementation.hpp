#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkcore/Renderer/Renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkcore/Renderer/Buffer.hpp"
#include "vulkcore/Renderer/SwapChain.hpp"
#include "vulkcore/Renderer/Texture.hpp"
#include "vulkcore/Renderer/UniformBuffer.hpp"
#include "vulkcore/Renderer/Vertex.hpp"
#include "vulkcore/Renderer/VulkanContext.hpp"
#include "vulkcore/Utils/FileUtils.hpp"
#include "vulkcore/Utils/ModelLoader.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "RendererPrivate.hpp"

namespace VulkCore {

using namespace renderer_detail;

} // namespace VulkCore
