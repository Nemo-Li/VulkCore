#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "vulkcore/Application.hpp"
#include "vulkcore/Export/OhosExporter.hpp"

int main(int argc, char** argv) {
    try {
        std::filesystem::path startupScene;
        bool hideTools = false;
        bool terrainFrustumCullingEnabled = true;
        std::filesystem::path frameOutput;
        float frameProgress = 0.0f;
        float frameTime = 0.0f;
        int frameWidth = 0, frameHeight = 0;
        VulkCore::OhosExportOptions exportOptions;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--scene") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("--scene requires a scene file path");
                }
                startupScene = argv[++index];
            } else if (argument == "--capture-frame") {
                if (index + 5 >= argc) {
                    throw std::invalid_argument("--capture-frame requires progress, time, width, height and output");
                }
                frameProgress = std::stof(argv[++index]);
                frameTime = std::stof(argv[++index]);
                frameWidth = std::stoi(argv[++index]);
                frameHeight = std::stoi(argv[++index]);
                frameOutput = argv[++index];
                if (!std::isfinite(frameProgress) || frameProgress < 0 || frameProgress > 1 ||
                    !std::isfinite(frameTime) || frameTime < 0 ||
                    frameWidth < 1 || frameHeight < 1 || frameWidth > 8192 || frameHeight > 8192 ||
                    frameOutput.empty()) {
                    throw std::invalid_argument("invalid --capture-frame arguments");
                }
            } else if (argument == "--export-ohos" || argument == "--app-name" || argument == "--bundle-name") {
                if (index + 1 >= argc) throw std::invalid_argument(std::string(argument) + " requires a value");
                const std::string value(argv[++index]);
                if (argument == "--export-ohos") exportOptions.outputDirectory = value;
                else if (argument == "--app-name") exportOptions.appName = value;
                else exportOptions.bundleName = value;
            } else if (argument == "--help") {
                std::cout << "Usage: vulkcore [--scene path.scene] [--hide-ui]\n"
                    "       vulkcore --scene path.scene --capture-frame progress time width height output.png\n"
                    "       vulkcore --scene path.scene --export-ohos output-directory\n"
                    "                [--app-name name] [--bundle-name com.example.game]\n"
                    "       Add --no-terrain-culling to compare viewport output.\n";
                return EXIT_SUCCESS;
            } else if (argument == "--no-terrain-culling") {
                terrainFrustumCullingEnabled = false;
            } else if (argument == "--hide-ui") {
                hideTools = true;
            } else {
                throw std::invalid_argument("unknown command-line argument: " +
                    std::string(argument));
            }
        }
        if (!frameOutput.empty() && (startupScene.empty() || !exportOptions.outputDirectory.empty())) {
            throw std::invalid_argument("--capture-frame requires --scene and is incompatible with export");
        }
        if (!exportOptions.outputDirectory.empty()) {
            exportOptions.scene = startupScene;
            const auto output = VulkCore::exportOhosProject(exportOptions);
            std::cout << "Exported HarmonyOS project: " << output.string() << '\n';
            return EXIT_SUCCESS;
        }
        VulkCore::Application application(std::move(startupScene), hideTools, terrainFrustumCullingEnabled);
        if (frameOutput.empty()) application.run();
        else application.captureFrame(frameProgress, frameTime, frameWidth, frameHeight, frameOutput);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
