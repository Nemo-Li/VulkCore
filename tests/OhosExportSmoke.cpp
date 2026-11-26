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
    std::ifstream stream(path, std::ios::binary);
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
        write(source / "assets/fonts/SourceHanSansCN-Regular.otf", "bundled font");
        const auto atlas = root / "retired/atlas.png";
        write(atlas, "retired atlas");
        const auto effect = root / "external VFX/fire.vfx3d";
        write(effect, "vfx3d package");
        const auto layout = root / "external roads/roads.yml";
        write(layout, "Rails: []\n");
        const auto tree = root / "external forest/tree.sbfres";
        write(tree, "model archive");
        write(tree.parent_path() / "tree.Tex.sbfres", "texture archive");
        write(tree.parent_path() / "unused.sbfres", "unreferenced archive");
        write(source / "assets/terrain/MainField.tscb", "terrain");
        write(source / "src/main.cpp", "// runtime");
        write(source / "include/engine.hpp", "// interface");
        write(source / "third_party/stb/stb_image.h", "// dependency");
        write(source / "shaders/src/shader_base.vert", "// shader");
        write(source / "shaders/src/shader_particle.frag", "// shader with a sun coverage variant");
        write(source / "shaders/src/shader_terrain.frag", "// shader with a HarmonyOS variant");
        write(source / "CMakeLists.txt", "# runtime build");
        write(source / "cmake/OhosPhysX.cmake", "# platform compatibility");
        write(root / "spirv/shader_base.vert.spv", std::string("\x03\x02\x23\x07", 4));
        write(root / "spirv/shader_particle.frag.spv", std::string("\x03\x02\x23\x07", 4));
        write(root / "spirv/sun_caster_particle.frag.spv", std::string("\x03\x02\x23\x07", 4));
        const auto spirvMagic = std::string("\x03\x02\x23\x07", 4);
        const auto mobileTerrain = spirvMagic + "mobile terrain";
        write(root / "spirv/shader_terrain.frag.spv", spirvMagic + "desktop terrain");
        write(root / "spirv/ohos/shader_terrain.frag.spv", mobileTerrain);
        fs::create_directories(source / "platform/ohos");
        fs::copy(fs::path(__FILE__).parent_path().parent_path() / "platform/ohos/template",
            source / "platform/ohos/template", fs::copy_options::recursive);
        const auto sdk = fs::path("engine/third_party/physx");
        const auto binaryProbe = sdk / "lib/arm64-v8a/binary-probe.a";
        const auto probeBytes = std::string(1, '\0') + "@APP_NAME_JSON@@BUNDLE_NAME_JSON@";
        write(source / "platform/ohos/template" / binaryProbe, probeBytes);
        const auto scene = source / "scenes/tornado.scene";
        const std::string original = "model_resource_path \"" + model.generic_string() + "\" 42\n"
            "terrain_resource \"../assets/terrain\"\n"
            "material 1 \"Named material\" 1 1 1 1 0 1 1 0 1 0 \"" +
            (source / "assets/textures/spark.png").generic_string() + "\" - - - - - -\n"
            "material_textures 1 \"missing-normal.png\" \"\"\n"
            "particle_system_object 99 1 \"assets/textures/spark.png\"\n"
            "particle_system_object 98 1 \"" + atlas.generic_string() + "\" flipbook 2 2 4 192 4 2\n"
            "realtime_effect 100 \"" + effect.generic_string() + "\" 1 0\n"
            "camera_position 1 2 3\n"
            "terrain_road_layout \"" + layout.generic_string() + "\"\n"
            "terrain_road_path 4.8 3 20 25 25 29 31 32\n"
            "forest_patch 25 12 9 185\n"
            "forest_species \"" + tree.generic_string() + "\" \"Broadleaf\" 0.6 0.9 1\n";
        const std::string roads = "terrain_road_ochre_ratio 0.8\n"
            "  terrain_road 1 3.75 2 0 0 1 1\n"
            "terrain_road 0 3.75 1 1 0\n";
        write(scene, original + roads);
        VulkCore::OhosExportOptions options;
        options.sourceDirectory = source;
        options.shaderDirectory = root / "spirv";
        options.scene = scene;
        options.outputDirectory = root / "exported app";
        options.appName = "Game \"Mobile\"";
        VulkCore::exportOhosProject(options);
        require(fs::is_regular_file(options.outputDirectory / "engine/cmake/OhosPhysX.cmake"), "Native compatibility source was lost");
        require(read(options.outputDirectory / binaryProbe) == probeBytes, "Template substitution modified binary data");
        require(fs::is_regular_file(options.outputDirectory / sdk / "include/PxPhysicsAPI.h"), "PhysX headers were lost");
        for (const std::string component : {"PhysX", "PhysXCommon", "PhysXCooking",
                 "PhysXExtensions", "PhysXFoundation", "PhysXPvdSDK"}) {
            require(fs::is_regular_file(options.outputDirectory / sdk / "lib/arm64-v8a" /
                ("lib" + component + "_static_64.a")), "Prebuilt PhysX archive was lost");
        }
        const auto payload = options.outputDirectory / "entry/src/main/resources/rawfile/vulkcore";
        require(fs::is_regular_file(payload / "shaders/sun_caster_particle.frag.spv"),
            "Sun shadow coverage shader is missing from the export");
        require(read(payload / "shaders/shader_terrain.frag.spv") == mobileTerrain,
            "Export did not select the HarmonyOS terrain shader");
        const auto exported = read(payload / "scenes/tornado.scene");
        require(exported.ends_with(roads), "Road records were removed from a scene other than game.scene");
        require(exported.find(root.generic_string()) == std::string::npos, "Host absolute paths leaked into scene");
        require(exported.find("../assets/external/0/external model/mesh.gltf\" 42") != std::string::npos, "Model IDs/path were not preserved");
        require(exported.find("\"Named material\"") != std::string::npos, "Material name was changed");
        require(exported.find("camera_position 1 2 3") != std::string::npos, "Scene settings were changed");
        require(exported.find("material_textures 1 \"\" \"\"") != std::string::npos, "Missing optional texture did not retain editor fallback");
        require(fs::exists(payload / "assets/external/0/external model/buffer.bin"), "Model sidecar was lost");
        require(exported.find("realtime_effect 100 \"../assets/external/1/fire.vfx3d\" 1 0") != std::string::npos,
            "Realtime effect path or settings were lost");
        require(read(payload / "assets/external/1/fire.vfx3d") == "vfx3d package", "Realtime effect package was lost");
        require(exported.find("terrain_road_layout \"../assets/external/2/roads.yml\"") != std::string::npos &&
            read(payload / "assets/external/2/roads.yml") == "Rails: []\n", "Road layout file or reference was lost");
        require(exported.find("forest_species \"../assets/external/3/tree.sbfres\" \"Broadleaf\" 0.6 0.9 1") !=
            std::string::npos && read(payload / "assets/external/3/tree.sbfres") == "model archive" &&
            read(payload / "assets/external/3/tree.Tex.sbfres") == "texture archive" &&
            !fs::exists(payload / "assets/external/3/unused.sbfres"), "Forest files or model group were not preserved");
        require(exported.find("particle_system_object 99") != std::string::npos &&
            exported.find("particle_system_object 98") == std::string::npos &&
            !fs::exists(payload / "assets/external/2/atlas.png"), "Export retained a retired atlas effect");
        require(read(payload / "assets/fonts/SourceHanSansCN-Regular.otf") == "bundled font", "ImGui font was lost");
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
        require(failed && read(scene) == original + roads, "Existing export or source scene was overwritten");
        auto gameOptions = options;
        gameOptions.scene = scene.parent_path() / "game.scene";
        gameOptions.outputDirectory = root / "exported game";
        write(gameOptions.scene, original + roads);
        VulkCore::exportOhosProject(gameOptions);
        const auto gamePayload = gameOptions.outputDirectory / "entry/src/main/resources/rawfile/vulkcore";
        require(read(gamePayload / "scenes/game.scene") == exported.substr(0, exported.size() - roads.size()),
            "game.scene should omit road records and preserve other settings");
        require(read(gamePayload / "scenes/game.scene").find("terrain_road_path 4.8 3 20 25 25 29 31 32") !=
            std::string::npos, "Road mesh path was removed from HarmonyOS game.scene");
        require(read(gameOptions.scene) == original + roads, "Original game.scene was modified");
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
