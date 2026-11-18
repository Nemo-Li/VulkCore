#include "vulkcore/Export/OhosExporter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << value;
}
std::string read(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main() {
    const auto root = fs::temp_directory_path() / ("vulkcore-export-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto source = root / "source";
        const auto model = root / "external model/mesh.gltf";
        write(model, "model");
        write(model.parent_path() / "buffer.bin", "glTF sidecar");
        write(model.parent_path() / "tex/albedo.png", "texture");
        write(source / "assets/textures/spark.png", "spark");
        write(source / "assets/terrain/MainField.tscb", "terrain");
        write(source / "src/main.cpp", "// runtime");
        write(source / "include/engine.hpp", "// interface");
        write(source / "third_party/stb/stb_image.h", "// dependency");
        write(source / "shaders/src/shader_base.vert", "// shader");
        write(source / "CMakeLists.txt", "# runtime build");
        write(source / "cmake/OhosPhysX.cmake", "# platform compatibility");
        write(root / "spirv/shader_base.vert.spv", std::string("\x03\x02\x23\x07", 4));
        fs::create_directories(source / "platform/ohos");
        fs::copy(fs::path(__FILE__).parent_path().parent_path() / "platform/ohos/template",
            source / "platform/ohos/template", fs::copy_options::recursive);
        const auto scene = source / "scenes/tornado.scene";
        const std::string original = "model_resource_path \"" + model.generic_string() + "\" 42\n"
            "terrain_resource \"../assets/terrain\"\n"
            "material 1 \"Named material\" 1 1 1 1 0 1 1 0 1 0 \"" +
            (source / "assets/textures/spark.png").generic_string() + "\" - - - - - -\n"
            "material_textures 1 \"missing-normal.png\" \"\"\n"
            "particle_system_object 99 1 \"assets/textures/spark.png\"\n"
            "camera_position 1 2 3\n";
        write(scene, original);
        VulkCore::OhosExportOptions options;
        options.sourceDirectory = source;
        options.shaderDirectory = root / "spirv";
        options.scene = scene;
        options.outputDirectory = root / "exported app";
        options.appName = "Game \"Mobile\"";
        VulkCore::exportOhosProject(options);
        require(fs::is_regular_file(options.outputDirectory / "engine/cmake/OhosPhysX.cmake"), "Native compatibility source was lost");
        const auto payload = options.outputDirectory / "entry/src/main/resources/rawfile/vulkcore";
        const auto exported = read(payload / "scenes/tornado.scene");
        require(exported.find(root.generic_string()) == std::string::npos, "Host absolute paths leaked into scene");
        require(exported.find("../assets/external/0/external model/mesh.gltf\" 42") != std::string::npos, "Model IDs/path were not preserved");
        require(exported.find("\"Named material\"") != std::string::npos, "Material name was changed");
        require(exported.find("camera_position 1 2 3") != std::string::npos, "Scene settings were changed");
        require(exported.find("material_textures 1 \"\" \"\"") != std::string::npos, "Missing optional texture did not retain editor fallback");
        require(fs::exists(payload / "assets/external/0/external model/buffer.bin"), "Model sidecar was lost");
        require(read(payload / "startup.txt") == "scenes/tornado.scene\n", "Special scene filename was lost");
        require(read(options.outputDirectory / "AppScope/resources/base/element/string.json").find("Game \\\"Mobile\\\"") != std::string::npos, "App name JSON was not escaped");
        {
            std::ifstream manifest(payload / "manifest.txt");
            std::string name;
            while (std::getline(manifest, name)) {
                require(fs::is_regular_file(payload / name), "Manifest resource is missing");
            }
        }
        bool failed = false;
        try { VulkCore::exportOhosProject(options); } catch (...) { failed = true; }
        require(failed && read(scene) == original, "Existing export or source scene was overwritten");
        options.outputDirectory = root / "broken";
        write(scene, "model_resource_path \"missing.fbx\" 42\n");
        failed = false;
        try { VulkCore::exportOhosProject(options); } catch (...) { failed = true; }
        require(failed && !fs::exists(options.outputDirectory), "Failed export left a partial project");
        write(scene, "sun_present 0\n");
        fs::remove(root / "spirv/shader_base.vert.spv");
        failed = false;
        try { VulkCore::exportOhosProject(options); } catch (...) { failed = true; }
        require(failed && !fs::exists(options.outputDirectory), "Missing shader was silently accepted");
        fs::remove_all(root);
        std::cout << "HarmonyOS export smoke checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
