#pragma once

#include <filesystem>
#include <string>

namespace VulkCore {

struct OhosExportOptions {
    std::filesystem::path scene;
    std::filesystem::path outputDirectory;
    std::string appName = "VulkCore Player";
    std::string bundleName = "com.vulkcore.player";
    // Empty paths use the editor's source tree and compiled shaders.
    std::filesystem::path sourceDirectory;
    std::filesystem::path shaderDirectory;
};

// Produces a self-contained DevEco project. Throws with an actionable error;
// existing nonempty output directories are never overwritten.
std::filesystem::path exportOhosProject(const OhosExportOptions& options);

} // namespace VulkCore
