#pragma once

#include <ui/components/buffer_panel.hpp>

#include <rml/system_glfw.hpp>
#include <rml/render_daxa.hpp>

#include <ui/app_window.hpp>
#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListenerInstancer.h>

class EventInstancer : public Rml::EventListenerInstancer {
  public:
    auto InstanceEventListener(const Rml::String &value, Rml::Element * /*element*/) -> Rml::EventListener * override;
};

struct AppSettings {
    bool export_downloads;
};

struct AppUi {
    std::atomic_bool should_close = false;
    AppWindow app_window;
    Rml::Element *viewport_element{};
    BufferPanel buffer_panel{};

    SystemInterface_GLFW system_interface{};
    RenderInterface_Daxa render_interface;
    Rml::Context *rml_context{};
    EventInstancer event_instancer{};

    static inline AppUi *s_instance = nullptr;
    bool paused{};
    bool is_fullscreen{};
    AppSettings settings{};

    Rml::String download_input{};

    std::function<void()> on_reset{};
    std::function<void(bool)> on_toggle_pause{};
    std::function<void(bool)> on_toggle_fullscreen{};
    std::function<void(Rml::String const &)> on_download{};

    explicit AppUi(daxa::Device device);
    ~AppUi();
    AppUi(const AppUi &) = delete;
    AppUi(AppUi &&) = delete;
    auto operator=(const AppUi &) -> AppUi & = delete;
    auto operator=(AppUi &&) -> AppUi & = delete;

    void update(float time, float fps);
    void render(daxa::CommandRecorder &recorder, daxa::ImageId target_image);

    void toggle_fullscreen();
};
