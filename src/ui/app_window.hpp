#pragma once

#include <memory>
#include <utility>

#include <daxa/daxa.hpp>
#include <GLFW/glfw3.h>

#include <rml/system_glfw.hpp>

struct AppWindow {
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> glfw_window{nullptr, &glfwDestroyWindow};
    daxa::Swapchain swapchain{};
    daxa_i32vec2 size{};

    struct FullscreenCache {
        daxa_i32vec2 pos{};
        daxa_i32vec2 size{};
    };

    FullscreenCache fullscreen_cache{};

    int glfw_active_modifiers{};
    Rml::Context *rml_context{};

    std::function<void()> on_resize{};
    std::function<void()> on_close{};
    std::function<void(float, float)> on_mouse_move{};
    std::function<void(float, float)> on_mouse_scroll{};
    std::function<void(int32_t, int32_t)> on_mouse_button{};
    std::function<void(int32_t, int32_t)> on_key{};
    std::function<void(std::span<char const *>)> on_drop{};

    using RmlKeyDownCallback = std::function<bool(Rml::Context *context, Rml::Input::KeyIdentifier key, int key_modifier, int glfw_action, float native_dp_ratio, bool priority)>;
    RmlKeyDownCallback key_down_callback{};

    AppWindow() = default;
    explicit AppWindow(daxa::Device device, daxa_i32vec2 size);

    void update();
    void set_fullscreen(bool is_fullscreen);
    void set_vsync(bool enabled);
};
