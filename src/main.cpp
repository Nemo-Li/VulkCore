#include <cstdlib>
#include <exception>
#include <iostream>

#include "vulkcore/Application.hpp"

int main() {
    try {
        VulkCore::Application application;
        application.run();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
