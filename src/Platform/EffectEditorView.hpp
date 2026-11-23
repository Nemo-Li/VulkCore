#pragma once

#include "vulkcore/Platform/Window.hpp"

#include <memory>

namespace VulkCore {

class EffectEditorView {
public:
    explicit EffectEditorView(Window* window);
    ~EffectEditorView();

    EffectEditorView(const EffectEditorView&) = delete;
    EffectEditorView& operator=(const EffectEditorView&) = delete;

    bool show(float x, float y, float width, float height);
    void hide();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace VulkCore
