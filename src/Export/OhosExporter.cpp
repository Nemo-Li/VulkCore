#include "vulkcore/Export/OhosExporter.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace VulkCore {
namespace {
namespace fs = std::filesystem;

bool within(const fs::path& path, const fs::path& directory) {
    const auto relative = path.lexically_relative(directory);
    return !relative.empty() && *relative.begin() != "..";
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if (!stream) throw std::runtime_error("Cannot write " + path.string());
}

void copyTree(const fs::path& source, const fs::path& destination) {
    if (fs::is_symlink(source)) {
        throw std::runtime_error("Replace symbolic link with a local resource before exporting: " + source.string());
    }
    if (fs::is_directory(source)) {
        fs::create_directories(destination);
        for (const auto& item : fs::directory_iterator(source)) {
            if (item.path().filename() == ".git" || item.path().filename() == ".DS_Store") continue;
            copyTree(item.path(), destination / item.path().filename());
        }
    } else if (fs::is_regular_file(source)) {
        fs::create_directories(destination.parent_path());
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
    } else {
        throw std::runtime_error("Export resource is missing or not a regular file: " + source.string());
    }
}

std::string readText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string jsonString(const std::string& value) {
    std::ostringstream stream;
    stream << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') stream << '\\' << c;
        else if (c < 0x20) stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        else stream << c;
    }
    stream << '"';
    return stream.str();
}

void replaceAll(std::string& text, const std::string& token, const std::string& value) {
    std::size_t offset = 0;
    while ((offset = text.find(token, offset)) != std::string::npos) {
        text.replace(offset, token.size(), value);
        offset += value.size();
    }
}

struct QuotedField {
    std::size_t start;
    std::size_t length;
    std::string value;
};

std::vector<QuotedField> quotedFields(const std::string& line) {
    std::vector<QuotedField> fields;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] != '"') continue;
        const auto start = index++;
        std::string value;
        bool closed = false;
        for (; index < line.size(); ++index) {
            if (line[index] == '"') { closed = true; break; }
            if (line[index] == '\\') {
                if (++index == line.size()) break;
            }
            value += line[index];
        }
        if (!closed) throw std::runtime_error("Unterminated quoted scene field: " + line);
        fields.push_back({start, index - start + 1, std::move(value)});
    }
    return fields;
}

class ResourceCollector {
public:
    ResourceCollector(fs::path source, fs::path scene, fs::path payload, fs::path output)
        : source_(std::move(source)), scene_(std::move(scene)), payload_(std::move(payload)), output_(std::move(output)) {}

    std::string path(const std::string& value, bool sceneRelative, bool modelDirectory) {
        if (value.empty()) return value;
        fs::path resolved(value);
        if (resolved.is_relative()) {
            const fs::path candidates[] = {
                (sceneRelative ? scene_.parent_path() : source_) / resolved,
                (sceneRelative ? source_ : scene_.parent_path()) / resolved,
                fs::current_path() / resolved
            };
            for (const auto& candidate : candidates) {
                if (fs::exists(candidate)) { resolved = candidate; break; }
            }
        }
        if (!fs::exists(resolved)) {
            // Textures already fall back to base color, flat normals or a soft
            // particle in the editor. Preserve that behavior without host paths.
            if (!sceneRelative) return {};
            throw std::runtime_error("Scene resource was not found: " + value);
        }
        resolved = fs::canonical(resolved);
        const auto assets = fs::weakly_canonical(source_ / "assets");
        fs::path target;
        if (within(resolved, assets)) {
            target = fs::path("assets") / resolved.lexically_relative(assets);
        } else {
            for (const auto& [origin, exported] : external_) {
                if (resolved == origin || (fs::is_directory(origin) && within(resolved, origin))) {
                    target = exported;
                    if (resolved != origin) target /= resolved.lexically_relative(origin);
                    break;
                }
            }
            if (target.empty()) {
                const fs::path origin = modelDirectory ? resolved.parent_path() : resolved;
                // Model directories include glTF buffers, OBJ/MTL and texture sidecars.
                // Do not accidentally copy a home/root directory or recurse into output.
                if (origin == origin.root_path() || within(output_, origin) || within(source_, origin)) {
                    throw std::runtime_error("Move external model/resources to a dedicated asset folder: " + origin.string());
                }
                const fs::path exported = fs::path("assets/external") /
                    std::to_string(external_.size()) / origin.filename();
                copyTree(origin, payload_ / exported);
                external_.emplace_back(origin, exported);
                target = modelDirectory ? exported / resolved.filename() : exported;
            }
        }
        return (sceneRelative ? fs::path("..") / target : target).generic_string();
    }
private:
    fs::path source_, scene_, payload_, output_;
    std::vector<std::pair<fs::path, fs::path>> external_;
};

std::string rewriteScene(const std::string& input, ResourceCollector& collector) {
    std::istringstream stream(input);
    std::ostringstream result;
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream tokens(line);
        std::string key;
        tokens >> key;
        const bool model = key == "model_resource_path" || key == "player_model_resource";
        const bool reference = model || key == "terrain_resource" || key == "water_resource";
        const bool material = key == "material";
        const bool textures = key == "material_textures";
        const bool particle = key == "particle_system_object" || key == "particle_system";
        if (reference || material || textures || particle) {
            auto fields = quotedFields(line);
            const std::size_t first = material ? 1 : 0;
            const std::size_t count = textures ? 2 : 1;
            if (fields.size() != first + count) {
                throw std::runtime_error("Invalid path fields in scene record: " + key);
            }
            for (std::size_t index = first + count; index-- > first;) {
                auto& field = fields[index];
                std::ostringstream encoded;
                encoded << std::quoted(collector.path(field.value, reference, model));
                line.replace(field.start, field.length, encoded.str());
            }
        }
        result << line << '\n';
    }
    return result.str();
}
} // namespace

std::filesystem::path exportOhosProject(const OhosExportOptions& options) {
    if (options.outputDirectory.empty() || options.scene.empty()) {
        throw std::invalid_argument("HarmonyOS export requires a scene and output directory.");
    }
    if (options.appName.empty() || !std::regex_match(options.bundleName,
            std::regex("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+"))) {
        throw std::invalid_argument("Use a nonempty app name and a bundle name such as com.example.game.");
    }
    const fs::path source = fs::canonical(options.sourceDirectory.empty()
        ? fs::path(VULKCORE_SOURCE_DIR) : options.sourceDirectory);
    const fs::path scene = fs::canonical(options.scene);
    const fs::path shaders = options.shaderDirectory.empty()
        ? fs::path(VULKCORE_EXPORT_SHADER_DIR) : options.shaderDirectory;
    const fs::path output = fs::weakly_canonical(fs::absolute(options.outputDirectory));
    if (!fs::is_regular_file(scene) || scene.extension() != ".scene") {
        throw std::runtime_error("Select a saved .scene file before exporting.");
    }
    if (fs::exists(output)) throw std::runtime_error("Export directory already exists; choose a new directory: " + output.string());
    for (const auto* directory : {"assets", "src", "include", "shaders", "platform", "third_party"}) {
        if (within(output, source / directory)) throw std::runtime_error("Export outside engine source/resource directories.");
    }
    fs::create_directories(output.parent_path());
    const fs::path staging = output.string() + ".tmp-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (!fs::create_directory(staging)) throw std::runtime_error("Cannot create export staging directory.");
    try {
        copyTree(source / "platform/ohos/template", staging);
        for (const auto& item : fs::recursive_directory_iterator(staging)) {
            if (!item.is_regular_file()) continue;
            auto text = readText(item.path());
            replaceAll(text, "@APP_NAME_JSON@", jsonString(options.appName));
            replaceAll(text, "@BUNDLE_NAME_JSON@", jsonString(options.bundleName));
            writeText(item.path(), text);
        }
        const auto engine = staging / "engine";
        for (const auto* directory : {"src", "include", "cmake", "shaders/src", "third_party/stb"}) {
            copyTree(source / directory, engine / directory);
        }
        copyTree(source / "CMakeLists.txt", engine / "CMakeLists.txt");
        const auto payload = staging / "entry/src/main/resources/rawfile/vulkcore";
        copyTree(source / "assets", payload / "assets");
        std::size_t shaderCount = 0;
        for (const auto& item : fs::directory_iterator(source / "shaders/src")) {
            const auto ext = item.path().extension();
            if (ext != ".vert" && ext != ".frag" && ext != ".tesc" && ext != ".tese" && ext != ".geom" && ext != ".comp") continue;
            const auto binary = shaders / (item.path().filename().string() + ".spv");
            std::ifstream file(binary, std::ios::binary);
            unsigned char magic[4]{};
            file.read(reinterpret_cast<char*>(magic), sizeof(magic));
            if (!file || magic[0] != 3 || magic[1] != 2 || magic[2] != 35 || magic[3] != 7) {
                throw std::runtime_error("Compile shaders before exporting; missing/invalid SPIR-V: " + binary.string());
            }
            copyTree(binary, payload / "shaders" / binary.filename());
            ++shaderCount;
        }
        if (shaderCount == 0) throw std::runtime_error("No runtime shaders were found.");
        ResourceCollector collector(source, scene, payload, staging);
        writeText(payload / "scenes" / scene.filename(), rewriteScene(readText(scene), collector));
        writeText(payload / "startup.txt", (fs::path("scenes") / scene.filename()).generic_string() + "\n");
        std::vector<std::string> files;
        for (const auto& item : fs::recursive_directory_iterator(payload)) {
            if (!item.is_regular_file()) continue;
            const auto name = item.path().lexically_relative(payload).generic_string();
            if (name.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("Resource filename contains a line break.");
            files.push_back(name);
        }
        std::sort(files.begin(), files.end());
        std::ostringstream manifest;
        for (const auto& name : files) manifest << name << '\n';
        writeText(payload / "manifest.txt", manifest.str());
        fs::rename(staging, output);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
    return output;
}

} // namespace VulkCore
