#include "RendererImplementation.hpp"
#include "../Platform/EditorTrash.hpp"
#include "vulkcore/Terrain/Terrain.hpp"
#include "vulkcore/Utils/SbfresLoader.hpp"

#include <utility>

namespace VulkCore {
namespace {

namespace fs = std::filesystem;
constexpr char kResourcePayload[] = "VULKCORE_RESOURCE";
constexpr char kResourceModelPayload[] = "VULKCORE_RESOURCE_MODEL";

std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool within(const fs::path& root, const fs::path& path) {
    const auto mismatch = std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    return mismatch.first == root.end();
}

bool managedDirectory(const fs::path& root, const fs::path& path) {
    std::error_code error;
    const auto canonical = fs::canonical(path, error);
    return !error && canonical == path && within(root, canonical) && fs::is_directory(canonical, error);
}

void requireUnused(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (status.type() == fs::file_type::not_found) return;
    if (error) throw fs::filesystem_error("Could not check destination", path, error);
    throw std::runtime_error("Already exists: " + utf8(path.filename()));
}

} // namespace

void Renderer::initializeResourceBrowser(const std::filesystem::path& root) {
    auto& browser = resourceBrowser_;
    browser.root = browser.directory = root;
    try {
        fs::create_directories(root);
        if (!fs::is_directory(fs::symlink_status(root))) {
            throw std::runtime_error("The resource path must be a directory.");
        }
        browser.root = browser.directory = fs::canonical(root);
    } catch (const std::exception& error) {
        browser.status = error.what();
    }
}

void Renderer::queueEditorDrop(std::vector<std::filesystem::path> paths, double x, double y) {
    auto& browser = resourceBrowser_;
    if (toolUiVisible_ && browser.open) {
        for (auto zone = browser.dropZones.rbegin(); zone != browser.dropZones.rend(); ++zone) {
            if (!zone->contains(x, y)) continue;
            for (auto& path : paths) browser.transfers.push_back({std::move(path), zone->directory, false});
            return;
        }
    }
    if (!paths.empty()) queueSceneOpen(paths.front());
}

void Renderer::processResourceTransfers() {
    auto& browser = resourceBrowser_;
    if (browser.transfers.empty()) return;
    const auto transfers = std::exchange(browser.transfers, {});
    browser.status.clear();
    std::size_t complete = 0;
    for (const auto& transfer : transfers) {
        try {
            if (!managedDirectory(browser.root, transfer.directory)) {
                throw std::runtime_error("The destination folder is no longer available.");
            }
            if (!fs::is_regular_file(fs::symlink_status(transfer.source))) {
                throw std::runtime_error("Drop regular files to import or move them.");
            }
            if (transfer.move && !managedDirectory(browser.root, transfer.source.parent_path())) {
                throw std::runtime_error("The source folder is no longer available.");
            }
            const auto destination = transfer.directory / transfer.source.filename();
            requireUnused(destination);
            // Copy without replacement, then remove the original only after a successful copy.
            fs::copy_file(transfer.source, destination);
            if (transfer.move) {
                std::error_code error;
                fs::remove(transfer.source, error);
                if (error) throw fs::filesystem_error("Copied file but could not remove original", transfer.source, error);
            }
            ++complete;
        } catch (const std::exception& error) {
            if (!browser.status.empty()) browser.status += '\n';
            browser.status += error.what();
        }
    }
    if (complete != 0) {
        const auto message = std::to_string(complete) + " file(s) transferred.";
        browser.status = browser.status.empty() ? message : message + "\n" + browser.status;
        if (!browser.selected.empty()) resourcePreview_.reload = true;
    }
    scanResourceDirectory();
}

void Renderer::processResourceTrash() {
    auto& browser = resourceBrowser_;
    const auto path = std::exchange(browser.pendingTrash, {});
    if (path.empty()) return;

    try {
        if (!managedDirectory(browser.root, path.parent_path()) ||
            !fs::is_regular_file(fs::symlink_status(path))) {
            throw std::runtime_error("The selected resource file is no longer available.");
        }
        trashEditorFile(path);
        if (browser.selected == path) browser.selected.clear();
        resourcePreview_.requestedModel.clear();
        resourcePreview_.reload = true;
        browser.status = "Moved " + utf8(path.filename()) + " to the Trash.";
    } catch (const std::exception& error) {
        browser.status = "Could not move " + utf8(path.filename()) + " to the Trash: " + error.what();
    }
    scanResourceDirectory();
}

void Renderer::scanResourceDirectory() {
    auto& browser = resourceBrowser_;
    browser.entries.clear();
    browser.nextScan = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    if (!managedDirectory(browser.root, browser.directory)) browser.directory = browser.root;
    try {
        if (!managedDirectory(browser.root, browser.directory)) {
            throw std::runtime_error("Cannot access the resource folder: " + utf8(browser.root));
        }
        for (const auto& entry : fs::directory_iterator(browser.directory)) {
            const auto type = entry.symlink_status();
            if (fs::is_directory(type) || fs::is_regular_file(type)) {
                browser.entries.push_back({entry.path(), fs::is_directory(type)});
            }
        }
    } catch (const std::exception& error) {
        browser.status = error.what();
    }
    std::sort(browser.entries.begin(), browser.entries.end(), [](const auto& a, const auto& b) {
        if (a.directory != b.directory) return a.directory;
        return a.path < b.path;
    });
    if (!browser.selected.empty() && std::none_of(browser.entries.begin(), browser.entries.end(),
            [&](const auto& entry) { return entry.path == browser.selected; })) {
        browser.selected.clear();
        resourcePreview_.reload = true;
    }
}

void Renderer::acceptSceneModelDrop() {
    if (ImGui::AcceptDragDropPayload(kResourceModelPayload) && !resourceBrowser_.dragged.empty()) {
        resourceBrowser_.pendingModelImport = resourceBrowser_.dragged;
    }
}

void Renderer::processSceneModelDrop() {
    const auto path = std::exchange(resourceBrowser_.pendingModelImport, {});
    if (path.empty()) return;

    try {
        if (isBfresPath(path) && inspectBfresContent(path) != BfresContent::Model) {
            throw std::runtime_error("The selected package does not contain a model.");
        }
    } catch (const std::exception& error) {
        sceneStatusFailed_ = true;
        sceneStatus_ = "Cannot add " + utf8(path.filename()) + ": " + error.what();
        return;
    }

    const glm::vec3 worldTarget = cameraTarget_ / modelScale_ + modelCenter_;
    const glm::vec2 viewDirection(cameraTarget_.x - cameraPosition_.x, cameraTarget_.z - cameraPosition_.z);
    if (!importModelResource(path)) {
        sceneStatusFailed_ = true;
        sceneStatus_ = resourceImportStatus_;
        return;
    }

    auto* node = findSceneNodeById(meshData_, sceneModelResources_.back().rootNodeId);
    if (node && node->hasBounds) {
        modelGravity_.try_emplace(node->nodeId);
        if (terrainScene_) {
            const glm::mat4 parentTransform = nodeManualTransform(meshData_.hierarchy);
            const glm::vec3 halfSize = (node->boundsMaximum - node->boundsMinimum) * 0.5f;
            const glm::vec3 halfExtents = glm::abs(glm::vec3(parentTransform[0])) * halfSize.x +
                glm::abs(glm::vec3(parentTransform[1])) * halfSize.y +
                glm::abs(glm::vec3(parentTransform[2])) * halfSize.z;
            glm::vec3 dropPoint = worldTarget;
            if (playerController_.active() && glm::length(viewDirection) > 0.00001f) {
                // Leave room for both the model and the player's capsule.
                const float clearance = 2.0f + glm::length(glm::vec2(halfExtents.x, halfExtents.z));
                const glm::vec2 ahead = glm::normalize(viewDirection) * clearance;
                dropPoint.x = playerController_.position().x + ahead.x;
                dropPoint.z = playerController_.position().z + ahead.y;
            }
            if (const auto height = terrainScene_->renderedHeightAtWorld({dropPoint.x, dropPoint.z})) {
                dropPoint.y = std::max(dropPoint.y, *height + 0.75f);
            } else {
                dropPoint = terrainScene_->centerPosition();
                dropPoint.y += 0.75f;
            }

            const glm::vec3 modelCenter(parentTransform *
                glm::vec4((node->boundsMinimum + node->boundsMaximum) * 0.5f, 1.0f));
            const glm::vec3 modelBase = modelCenter - glm::vec3(0.0f, halfExtents.y, 0.0f);
            translateSceneNode(*node, parentTransform, (dropPoint - modelBase) * modelScale_);
        }
    }

    selectedModelNode_ = node;
    editorSelection_ = EditorSelection::Node;
    editorPart_.reset();
    editorDirty_ = true;
    sceneStatusFailed_ = false;
    sceneStatus_ = "Added " + utf8(path.filename()) + " to the scene.";
}

void Renderer::drawResourceBrowser() {
    auto& browser = resourceBrowser_;
    if (ImGui::Button(browser.open ? "Resource Manager  [-]" : "Resource Manager  [+]")) {
        browser.open = !browser.open;
    }
    if (!browser.open) return;
    if (std::chrono::steady_clock::now() >= browser.nextScan) scanResourceDirectory();

    const auto setDirectory = [&](const fs::path& path) {
        browser.directory = path;
        browser.selected.clear();
        browser.status.clear();
        resourcePreview_.reload = true;
        resourcePreview_.requestedModel.clear();
        scanResourceDirectory();
    };
    const auto addDropZone = [&](const fs::path& directory, ImVec2 min, ImVec2 max) {
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        const auto* list = ImGui::GetWindowDrawList();
        const auto clipMin = list->GetClipRectMin(), clipMax = list->GetClipRectMax();
        const auto origin = ImGui::GetMainViewport()->Pos;
        browser.dropZones.push_back({directory, std::max(min.x, clipMin.x) - origin.x,
            std::max(min.y, clipMin.y) - origin.y, std::min(max.x, clipMax.x) - origin.x,
            std::min(max.y, clipMax.y) - origin.y});
    };
    const auto acceptMove = [&](const fs::path& directory) {
        if (ImGui::BeginDragDropTarget()) {
            if ((ImGui::AcceptDragDropPayload(kResourcePayload) ||
                 ImGui::AcceptDragDropPayload(kResourceModelPayload)) && !browser.dragged.empty()) {
                browser.transfers.push_back({browser.dragged, directory, true});
            }
            ImGui::EndDragDropTarget();
        }
    };

    ImGui::SameLine();
    if (ImGui::Button("New Folder")) {
        browser.folderName.fill('\0');
        ImGui::OpenPopup("New resource folder");
    }
    if (ImGui::BeginPopupModal("New resource folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##Folder name", "Folder name", browser.folderName.data(),
            browser.folderName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Create") || enter) {
            try {
                const std::string_view text(browser.folderName.data());
                const fs::path name(std::u8string(text.begin(), text.end()));
                if (name.empty() || name != name.filename() || name == "." || name == ".." ||
                    text.find_first_of("/\\:<>\"|?*") != std::string_view::npos ||
                    text.front() == ' ' || text.back() == ' ' || text.back() == '.') {
                    throw std::runtime_error("Enter a valid folder name.");
                }
                if (!managedDirectory(browser.root, browser.directory)) {
                    throw std::runtime_error("The current folder is unavailable.");
                }
                const auto folder = browser.directory / name;
                requireUnused(folder);
                if (!fs::create_directory(folder)) throw std::runtime_error("Could not create the folder.");
                browser.status.clear();
                scanResourceDirectory();
                ImGui::CloseCurrentPopup();
            } catch (const std::exception& error) {
                browser.status = error.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!browser.status.empty()) ImGui::TextWrapped("%s", browser.status.c_str());
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        browser.status.clear();
        scanResourceDirectory();
        resourcePreview_.reload = true;
    }
    if (browser.directory != browser.root) {
        const auto parent = browser.directory.parent_path();
        ImGui::SameLine();
        const bool up = ImGui::Button("Up");
        addDropZone(parent, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        acceptMove(parent);
        if (up) setDirectory(parent);
    }
    ImGui::SameLine();
    const auto relative = browser.directory.lexically_relative(browser.root);
    const auto display = relative == "." ? browser.root.filename() : browser.root.filename() / relative;
    ImGui::TextDisabled("%s/", utf8(display).c_str());
    ImGui::SetItemTooltip("%s", utf8(browser.directory).c_str());

    const float browserWidth = ImGui::GetContentRegionAvail().x * 0.37f;
    if (ImGui::BeginChild("Resource files", ImVec2(browserWidth, 0), ImGuiChildFlags_Borders)) {
        const auto min = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        addDropZone(browser.directory, min, ImVec2(min.x + size.x, min.y + size.y));
        ImGui::TextDisabled("Drop files here or onto a folder");
        if (!browser.status.empty()) ImGui::TextWrapped("%s", browser.status.c_str());
        fs::path openFolder;
        for (const auto& entry : browser.entries) {
            const auto name = utf8(entry.path.filename());
            ImGui::PushID(name.c_str());
            const bool picked = ImGui::Selectable("##Resource", browser.selected == entry.path,
                entry.directory ? ImGuiSelectableFlags_AllowDoubleClick : ImGuiSelectableFlags_None,
                ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
            const auto rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
            const auto label = entry.directory ? "[Folder]  " + name : name;
            ImGui::GetWindowDrawList()->AddText(ImVec2(rowMin.x + 3, rowMin.y + 2),
                ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            if (entry.directory) {
                addDropZone(entry.path, rowMin, rowMax);
                acceptMove(entry.path);
                if ((picked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
                    (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) openFolder = entry.path;
                ImGui::SetItemTooltip("Double-click to open");
            } else {
                if (ImGui::BeginDragDropSource()) {
                    browser.dragged = entry.path;
                    const bool model = isModelAssetPath(entry.path);
                    ImGui::SetDragDropPayload(model ? kResourceModelPayload : kResourcePayload, nullptr, 0);
                    ImGui::TextUnformatted(name.c_str());
                    if (model) ImGui::TextDisabled("Drop in the scene or Outliner to add");
                    ImGui::EndDragDropSource();
                }
                if (picked && browser.selected != entry.path) {
                    browser.selected = entry.path;
                    resourcePreview_.reload = true;
                    resourcePreview_.requestedModel.clear();
                }
                ImGui::SetItemTooltip("%s", name.c_str());
            }
            ImGui::PopID();
        }
        if (!openFolder.empty()) setDirectory(openFolder);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Resource preview", ImVec2(0, 0), ImGuiChildFlags_Borders)) drawResourcePreview();
    ImGui::EndChild();
    if (!browser.selected.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        editorDeletePressed()) {
        browser.pendingTrash = browser.selected;
    }
}

} // namespace VulkCore
