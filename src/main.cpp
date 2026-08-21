#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "vulkcore/Application.hpp"

int main(int argc, char** argv) {
    try {
        std::filesystem::path startupScene;
        bool hideTools = false;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--scene") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("--scene requires a scene file path");
                }
                startupScene = argv[++index];
            } else if (argument == "--hide-ui") {
                hideTools = true;
            } else {
                throw std::invalid_argument("unknown command-line argument: " +
                    std::string(argument));
            }
        }
        VulkCore::Application application(std::move(startupScene), hideTools);
        application.run();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
