#pragma once

#include "rml/system_glfw.hpp"
#include "rml/render_daxa.hpp"

#include "app_window.hpp"

struct AppUi {
    std::atomic_bool should_close = false;
    std::vector<AppWindow> app_windows{};

    SystemInterface_GLFW system_interface{};
    RenderInterface_Daxa render_interface;
    Rml::Context *rml_context{};

    // App state
    bool show_text = true;
    Rml::String animal = "dog";

    explicit AppUi(daxa::Device device);
    ~AppUi();
    AppUi(const AppUi &) = delete;
    AppUi(AppUi &&) = delete;
    auto operator=(const AppUi &) -> AppUi & = delete;
    auto operator=(AppUi &&) -> AppUi & = delete;

    void update();
    void render(daxa::CommandRecorder &recorder, daxa::ImageId target_image);
};
