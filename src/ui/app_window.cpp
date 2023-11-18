#include "app_window.hpp"

#include <GLFW/glfw3.h>
#include <daxa/c/core.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

#include <stb_image.h>

auto get_native_handle(GLFWwindow *glfw_window_ptr) -> daxa::NativeWindowHandle {
#if defined(_WIN32)
    return glfwGetWin32Window(glfw_window_ptr);
#elif defined(__linux__)
    return reinterpret_cast<daxa::NativeWindowHandle>(glfwGetX11Window(glfw_window_ptr));
#endif
}

auto get_native_platform(GLFWwindow * /*unused*/) -> daxa::NativeWindowPlatform {
#if defined(_WIN32)
    return daxa::NativeWindowPlatform::WIN32_API;
#elif defined(__linux__)
    return daxa::NativeWindowPlatform::XLIB_API;
#endif
}

namespace {
    auto create(daxa_i32vec2 size) -> GLFWwindow * {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        return glfwCreateWindow(
            static_cast<int32_t>(size.x),
            static_cast<int32_t>(size.y),
            "Desktop Shadertoy", nullptr, nullptr);
    }
} // namespace

AppWindow::AppWindow(daxa::Device device, daxa_i32vec2 size)
    : glfw_window{create(size), &glfwDestroyWindow}, size{size} {
    glfwSetWindowUserPointer(this->glfw_window.get(), this);
    glfwSetWindowSizeCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, int width, int height) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            self.size = {width, height};
            self.swapchain.resize();
            if (self.on_resize) {
                self.on_resize();
            }
        });

    glfwSetWindowCloseCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            if (self.on_close) {
                self.on_close();
            }
        });

    glfwSetKeyCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, int glfw_key, int /*scancode*/, int glfw_action, int glfw_mods) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            if (context == nullptr) {
                return;
            }

            // Store the active modifiers for later because GLFW doesn't provide them in the callbacks to the mouse input events.
            self.glfw_active_modifiers = glfw_mods;

            // Override the default key event callback to add global shortcuts for the samples.
            RmlKeyDownCallback &key_down_callback = self.key_down_callback;
            bool still_valid = true;

            switch (glfw_action) {
            case GLFW_PRESS:
            case GLFW_REPEAT: {
                const Rml::Input::KeyIdentifier key = RmlGLFW::ConvertKey(glfw_key);
                const int key_modifier = RmlGLFW::ConvertKeyModifiers(glfw_mods);
                float dp_ratio = 1.f;
                glfwGetWindowContentScale(glfw_window, &dp_ratio, nullptr);

                // See if we have any global shortcuts that take priority over the context.
                if (key_down_callback && !key_down_callback(context, key, key_modifier, dp_ratio, true)) {
                    break;
                }
                // Otherwise, hand the event over to the context by calling the input handler as normal.
                still_valid = RmlGLFW::ProcessKeyCallback(context, glfw_key, glfw_action, glfw_mods);
                if (!still_valid) {
                    break;
                }
                // The key was not consumed by the context either, try keyboard shortcuts of lower priority.
                if (key_down_callback && !key_down_callback(context, key, key_modifier, dp_ratio, false)) {
                    break;
                }
            } break;
            case GLFW_RELEASE:
                RmlGLFW::ProcessKeyCallback(context, glfw_key, glfw_action, glfw_mods);
                break;
            }

            if (still_valid && self.on_key) {
                self.on_key(glfw_key, glfw_action);
            }
        });

    glfwSetCharCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, unsigned int codepoint) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            RmlGLFW::ProcessCharCallback(context, codepoint);
        });

    glfwSetCursorEnterCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, int entered) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            RmlGLFW::ProcessCursorEnterCallback(context, entered);
        });

    // Mouse input
    glfwSetCursorPosCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, double xpos, double ypos) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            bool still_valid = RmlGLFW::ProcessCursorPosCallback(context, glfw_window, xpos, ypos, self.glfw_active_modifiers);
            if (still_valid && self.on_mouse_move) {
                self.on_mouse_move(static_cast<float>(xpos), static_cast<float>(ypos));
            }
        });

    glfwSetMouseButtonCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, int button, int action, int mods) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            self.glfw_active_modifiers = mods;
            bool still_valid = RmlGLFW::ProcessMouseButtonCallback(context, button, action, mods);
            if (still_valid && self.on_mouse_button) {
                self.on_mouse_button(button, action);
            }
        });

    glfwSetScrollCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, double xoffset, double yoffset) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            bool still_valid = RmlGLFW::ProcessScrollCallback(context, yoffset, self.glfw_active_modifiers);
            if (still_valid && self.on_mouse_scroll) {
                self.on_mouse_scroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
            }
        });

    glfwSetFramebufferSizeCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, int width, int height) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            RmlGLFW::ProcessFramebufferSizeCallback(context, width, height);
        });

    glfwSetWindowContentScaleCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, float xscale, float /*yscale*/) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            auto *context = self.rml_context;
            RmlGLFW::ProcessContentScaleCallback(context, xscale);
        });

    glfwSetDropCallback(
        this->glfw_window.get(),
        [](GLFWwindow *glfw_window, int path_count, char const *paths[]) {
            auto &self = *reinterpret_cast<AppWindow *>(glfwGetWindowUserPointer(glfw_window));
            if (self.on_drop) {
                self.on_drop(std::span<char const *>{paths, static_cast<size_t>(path_count)});
            }
        });

    glfwSetWindowSizeLimits(this->glfw_window.get(), 650, 24, GLFW_DONT_CARE, GLFW_DONT_CARE);

    auto icon_image = GLFWimage{};
    icon_image.pixels = stbi_load("appicon.png", &icon_image.width, &icon_image.height, nullptr, 4);
    glfwSetWindowIcon(this->glfw_window.get(), 1, &icon_image);
    stbi_image_free(icon_image.pixels);

    this->swapchain = device.create_swapchain({
        .native_window = get_native_handle(this->glfw_window.get()),
        .native_window_platform = get_native_platform(this->glfw_window.get()),
        .surface_format_selector = [](daxa::Format format) -> int32_t {
            switch (format) {
            case daxa::Format::R8G8B8A8_UNORM: return 90;
            case daxa::Format::B8G8R8A8_UNORM: return 80;
            default: return 0;
            }
        },
        .present_mode = daxa::PresentMode::FIFO,
        .image_usage = daxa::ImageUsageFlagBits::TRANSFER_DST,
        .max_allowed_frames_in_flight = 1,
        .name = "AppWindowSwapchain",
    });
}

void AppWindow::update() {
    glfwSetWindowUserPointer(this->glfw_window.get(), this);
    glfwPollEvents();
}

void AppWindow::set_fullscreen(bool is_fullscreen) {
    auto *monitor = glfwGetPrimaryMonitor();
    if (is_fullscreen) {
        GLFWvidmode const *mode = glfwGetVideoMode(monitor);
        glfwGetWindowPos(glfw_window.get(), &fullscreen_cache.pos.x, &fullscreen_cache.pos.y);
        fullscreen_cache.size = size;
        glfwSetWindowMonitor(glfw_window.get(), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(glfw_window.get(), nullptr, fullscreen_cache.pos.x, fullscreen_cache.pos.y, fullscreen_cache.size.x, fullscreen_cache.size.y, GLFW_DONT_CARE);
    }
}
