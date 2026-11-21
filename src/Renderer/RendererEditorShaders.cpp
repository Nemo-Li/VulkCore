#include "RendererImplementation.hpp"
#include "HdrComposition.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace VulkCore {
namespace {

const std::filesystem::path kSourceDirectory = VULKCORE_SHADER_SOURCE_DIR;
const std::filesystem::path kCompiler = VULKCORE_GLSLC;

std::string shaderSource(const std::string& name) {
    const auto data = readBinaryFile(kSourceDirectory / name);
    return std::string(data.begin(), data.end());
}

bool shaderInFamily(const std::string& name, std::string_view family) {
    if (family == "mesh") {
        static constexpr std::array meshShaders{
            "shader_base", "shader_diffuse", "shader_unlit", "shader_cel", "shader_rim",
            "shader_fresnel", "shader_vertex_color", "shader_normal_debug", "shader_position_debug",
            "shader_specular_debug", "shader_uv_ribbon", "shader_grid"
        };
        const auto stem = std::filesystem::path(name).stem().string();
        return std::any_of(meshShaders.begin(), meshShaders.end(), [&](std::string_view prefix) {
            return stem == prefix || (stem.starts_with(prefix) && stem[prefix.size()] == '_');
        });
    }
    const std::string prefix = "shader_" + std::string(family);
    return name.starts_with(prefix + ".") || name.starts_with(prefix + "_");
}

struct ShaderScratchDirectory {
    std::filesystem::path path;

    ShaderScratchDirectory() {
        const auto prefix = "vulkcore-shader-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (int attempt = 0; attempt < 10; ++attempt) {
            path = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("Could not create a temporary shader directory.");
    }

    ~ShaderScratchDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void writeText(const std::filesystem::path& path, const std::string& source) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << source;
    file.close();
    if (!file) throw std::runtime_error("Could not write " + path.string());
}

void compileShader(const std::filesystem::path& source, const std::filesystem::path& binary,
                   const std::filesystem::path& logPath) {
    bool success = false;
#ifdef _WIN32
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const HANDLE output = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open the shader compiler log.");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION child{};
    std::wstring command = L"\"" + kCompiler.wstring() + L"\" -O \"-I" +
        kSourceDirectory.wstring() + L"\" \"" + source.wstring() + L"\" -o \"" + binary.wstring() + L"\"";
    const bool started = CreateProcessW(kCompiler.c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != 0;
    CloseHandle(output);
    if (!started) throw std::runtime_error("Could not start glslc.");
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 1;
    success = GetExitCodeProcess(child.hProcess, &code) && code == 0;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
#else
    std::array<std::string, 6> args{kCompiler.string(), "-O", "-I" + kSourceDirectory.string(),
        source.string(), "-o", binary.string()};
    std::array<char*, 7> argv{};
    for (std::size_t i = 0; i < args.size(); ++i) argv[i] = args[i].data();
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        throw std::runtime_error("Could not initialize the shader compiler process.");
    }
    int error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t child = 0;
    if (!error) error = posix_spawn(&child, kCompiler.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error) throw std::runtime_error("Could not start glslc: " + std::system_category().message(error));
    int status = 0;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    success = result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    if (!success) {
        std::ifstream log(logPath);
        std::ostringstream message;
        message << log.rdbuf();
        throw std::runtime_error(message.str().empty() ? "Shader compilation failed." : message.str());
    }
}

} // namespace

void Renderer::drawEditorShader(ShaderStage stage, const std::string& name, std::string_view family,
                                std::optional<std::uint32_t> material) {
    if (name.empty()) return;
    ImGui::PushID(static_cast<int>(stage));
    ImGui::TextDisabled("%s", shaderStageLabel(stage));
    if (ImGui::Selectable(name.c_str())) {
        editorShader_ = {};
        editorShader_.stage = stage;
        editorShader_.family = family;
        editorShader_.material = material;
        editorShader_.chosen = name;
        editorShader_.candidates = {name};
        if (material) {
            std::error_code error;
            const auto extension = std::filesystem::path(name).extension();
            for (std::filesystem::directory_iterator it(kSourceDirectory, error), end;
                 !error && it != end; it.increment(error)) {
                const auto candidate = it->path().filename().string();
                if (candidate != name && it->path().extension() == extension &&
                    shaderInFamily(candidate, family)) {
                    editorShader_.candidates.push_back(candidate);
                }
            }
            std::sort(editorShader_.candidates.begin(), editorShader_.candidates.end());
        }
        try {
            editorShader_.source = editorShader_.original = shaderSource(name);
        } catch (const std::exception& error) {
            editorShader_.status = error.what();
        }
        editorShaderRequested_ = true;
    }
    ImGui::PopID();
}

void Renderer::drawEditorShaderDialog() {
    if (editorShaderRequested_) {
        editorShaderRequested_ = false;
        ImGui::OpenPopup("Shader Editor");
    }
    if (!ImGui::IsPopupOpen("Shader Editor")) return;

    const auto format = swapChain_->imageFormat();
    const bool srgb = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
    // ImGui vertex colors are written as linear values to an sRGB attachment.
    const auto channel = [srgb](int value) {
        const float c = static_cast<float>(value) / 255.0f;
        if (!srgb) return c;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    const auto color = [&](int red, int green, int blue, float alpha = 1.0f) {
        return ImVec4(channel(red), channel(green), channel(blue), alpha);
    };
    const auto extent = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(900.0f, extent.x * 0.9f),
        std::min(680.0f, extent.y * 0.9f)), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color(29, 34, 41));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, color(0, 0, 0, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, color(49, 60, 74));
    const bool visible = ImGui::BeginPopupModal("Shader Editor");
    ImGui::PopStyleColor(3);
    if (!visible) return;
    auto& dialog = editorShader_;
    if (dialog.candidates.size() > 1 && ImGui::BeginCombo("Shader", dialog.chosen.c_str())) {
        for (const auto& candidate : dialog.candidates) {
            if (ImGui::Selectable(candidate.c_str(), dialog.chosen == candidate) && dialog.chosen != candidate) {
                try {
                    auto source = shaderSource(candidate);
                    dialog.chosen = candidate;
                    dialog.source = dialog.original = std::move(source);
                    dialog.status.clear();
                } catch (const std::exception& error) {
                    dialog.status = error.what();
                }
            }
        }
        ImGui::EndCombo();
    } else if (dialog.candidates.size() <= 1) {
        ImGui::TextUnformatted(dialog.chosen.c_str());
    }
    ImGui::TextWrapped("Source changes apply to all materials using this file. Keep the shader's pipeline interface compatible.");
    const float footer = ImGui::GetFrameHeightWithSpacing() * (dialog.status.empty() ? 2.0f : 5.0f);
    ImGui::PushFont(editorShaderFont_, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, color(37, 43, 52));
    ImGui::PushStyleColor(ImGuiCol_Text, color(221, 228, 236));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, color(59, 82, 105));
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, color(255, 209, 138));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, color(107, 132, 156));
    auto& io = ImGui::GetIO();
    auto& style = ImGui::GetStyle();
    const bool cursorBlink = io.ConfigInputTextCursorBlink;
    const float cursorSize = style.InputTextCursorSize;
    io.ConfigInputTextCursorBlink = false;
    style.InputTextCursorSize = std::max(2.0f, cursorSize);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextMultiline("##GLSL", &dialog.source, ImVec2(-1.0f, -footer), ImGuiInputTextFlags_AllowTabInput);
    style.InputTextCursorSize = cursorSize;
    io.ConfigInputTextCursorBlink = cursorBlink;
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::BeginDisabled(dialog.source.empty() || editorShaderApplyRequested_);
    if (ImGui::Button("Compile and apply")) editorShaderApplyRequested_ = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    if (!dialog.status.empty()) {
        ImGui::BeginChild("Compiler output", ImVec2(0, 0), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(dialog.status.c_str());
        ImGui::EndChild();
    }
    ImGui::EndPopup();
}

void Renderer::applyEditorShader() {
    auto& dialog = editorShader_;
    const auto device = context_->device();
    std::vector<std::pair<VkPipeline*, VkPipeline>> fixedPipelines;
    std::unordered_map<std::string, VkPipeline> materialPipelines;
    auto& stageBinaries = availableShaders_[static_cast<std::size_t>(dialog.stage)];
    const auto& name = dialog.chosen;
    VkShaderModule oldModule = VK_NULL_HANDLE;
    VkShaderModule candidateModule = VK_NULL_HANDLE;
    bool registered = false;
    ShaderConfiguration selectedConfiguration;
    try {
        ShaderScratchDirectory temporary;
        const auto source = temporary.path / name;
        const auto binary = temporary.path / (name + ".spv");
        writeText(source, dialog.source);
        compileShader(source, binary, temporary.path / "glslc.log");
        candidateModule = createShaderModule(readBinaryFile(binary));
        oldModule = shaderModules_[name];
        shaderModules_[name] = candidateModule;
        if (dialog.material && std::find(stageBinaries.begin(), stageBinaries.end(), name) == stageBinaries.end()) {
            stageBinaries.push_back(name);
            registered = true;
        }

        fixedPipelines.reserve(5);
        if (dialog.family == "skybox") {
            fixedPipelines.emplace_back(&skyboxPipeline_, createSkyboxPipeline(sceneRenderPass_));
            if (hdrComposition_) fixedPipelines.emplace_back(&hdrSkyboxPipeline_,
                createSkyboxPipeline(hdrComposition_->geometryPass()));
        } else if (dialog.family == "particle") {
            fixedPipelines.emplace_back(&particleAlphaPipeline_, createParticlePipeline(false, renderPass_));
            fixedPipelines.emplace_back(&particleAdditivePipeline_, createParticlePipeline(true, renderPass_));
            if (hdrComposition_) {
                fixedPipelines.emplace_back(&hdrParticleAlphaPipeline_,
                    createParticlePipeline(false, hdrComposition_->geometryPass()));
                fixedPipelines.emplace_back(&hdrParticleAdditivePipeline_,
                    createParticlePipeline(true, hdrComposition_->geometryPass()));
            }
        } else if (dialog.material) {
            selectedConfiguration = meshData_.materials.at(*dialog.material).shaderConfiguration;
            selectedConfiguration.shader(dialog.stage) = name;
            if (name == defaultShaderForStage(dialog.stage)) {
                fixedPipelines.emplace_back(&graphicsPipeline_,
                    createMaterialGraphicsPipeline({}, {}, renderPass_, VK_CULL_MODE_BACK_BIT));
            }
            for (std::size_t i = 0; i < meshData_.materials.size(); ++i) {
                const auto& material = meshData_.materials[i];
                const auto& configuration = i == *dialog.material ? selectedConfiguration : material.shaderConfiguration;
                const auto& configuredName = configuration.shader(dialog.stage);
                if ((configuredName.empty() ? defaultShaderForStage(dialog.stage) : configuredName) != name) continue;
                const auto buildPipeline = [&](bool hdr, VkCullModeFlags cullMode) {
                    auto key = graphicsPipelineKey(configuration, material, hdr, cullMode);
                    if (materialPipelines.contains(key)) return;
                    const VkRenderPass pass = hdr ? hdrComposition_->geometryPass() : renderPass_;
                    const auto pipeline = createMaterialGraphicsPipeline(configuration, material, pass, cullMode);
                    materialPipelines.emplace(std::move(key), pipeline);
                };
                const auto buildCullModes = [&](bool hdr) {
                    if (material.doubleSided && material.drawBackFacesFirst && isTransparentMaterial(material)) {
                        buildPipeline(hdr, VK_CULL_MODE_FRONT_BIT);
                        buildPipeline(hdr, VK_CULL_MODE_BACK_BIT);
                    } else {
                        buildPipeline(hdr, material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
                    }
                };
                buildCullModes(false);
                if (hdrComposition_) buildCullModes(true);
            }
        }
        std::filesystem::copy_file(binary, std::filesystem::path(kShaderBinaryDirectory) / (name + ".spv"),
            std::filesystem::copy_options::overwrite_existing);
        if (dialog.source != dialog.original) writeText(kSourceDirectory / name, dialog.source);
    } catch (const std::exception& error) {
        for (const auto& [target, pipeline] : fixedPipelines) vkDestroyPipeline(device, pipeline, nullptr);
        for (const auto& [key, pipeline] : materialPipelines) vkDestroyPipeline(device, pipeline, nullptr);
        if (candidateModule) {
            if (oldModule) shaderModules_[name] = oldModule;
            else shaderModules_.erase(name);
            vkDestroyShaderModule(device, candidateModule, nullptr);
        }
        if (registered) std::erase(stageBinaries, name);
        dialog.status = error.what();
        return;
    }

    context_->waitIdle();
    for (auto& [target, pipeline] : fixedPipelines) {
        if (*target) vkDestroyPipeline(device, *target, nullptr);
        *target = pipeline;
    }
    for (auto& [key, pipeline] : materialPipelines) {
        if (const auto old = nodeGraphicsPipelines_.find(key); old != nodeGraphicsPipelines_.end()) {
            vkDestroyPipeline(device, old->second, nullptr);
            old->second = pipeline;
        } else {
            nodeGraphicsPipelines_.emplace(key, pipeline);
        }
        failedNodeGraphicsPipelineKeys_.erase(key);
    }
    if (dialog.material) {
        auto& material = meshData_.materials[*dialog.material];
        if (material.shaderConfiguration.shaderNames != selectedConfiguration.shaderNames) {
            material.shaderConfiguration = std::move(selectedConfiguration);
            material.userModified = true;
            editorDirty_ = true;
        }
    }
    if (oldModule) vkDestroyShaderModule(device, oldModule, nullptr);
    dialog.original = dialog.source;
    dialog.status = "Compiled and applied.";
}

} // namespace VulkCore
