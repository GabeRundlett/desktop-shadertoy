#include "app/core.inl"
#include <GLFW/glfw3.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger.h>
#include <fmt/format.h>

#include <ui/app_ui.hpp>

#include <daxa/command_recorder.hpp>
#include <cassert>
#include <iostream>

namespace {
    Rml::Element *time_element{};
    Rml::Element *fps_element{};
    Rml::Element *resolution_element{};
    Rml::Element *pause_element{};
    Rml::Element *download_bar_element{};
    Rml::Element *download_input_element{};
    Rml::Element *download_input_placeholder_element{};

    class DownloadBarEventListener : public Rml::EventListener {
      public:
        void ProcessEvent(Rml::Event &event) override {
            if (event.GetId() == Rml::EventId::Blur) {
                download_bar_element->SetProperty("display", "none");
            }
        }
    };
    DownloadBarEventListener download_bar_event_listener;

    class ViewportEventListener : public Rml::EventListener {
      public:
        void ProcessEvent(Rml::Event &event) override {
            switch (event.GetId()) {
            case Rml::EventId::Mousemove: {
                auto mouse_x = event.GetParameter("mouse_x", 0.0f);
                auto mouse_y = event.GetParameter("mouse_y", 0.0f);
                AppUi::s_instance->app_window.on_mouse_move(mouse_x, mouse_y);
            } break;
            case Rml::EventId::Keydown: {
                auto key = Rml::Input::KeyIdentifier(event.GetParameter("key_identifier", 0));
                AppUi::s_instance->app_window.on_key(RmlGLFW::ConvertKey(key), GLFW_PRESS);
            } break;
            case Rml::EventId::Keyup: {
                auto key = Rml::Input::KeyIdentifier(event.GetParameter("key_identifier", 0));
                AppUi::s_instance->app_window.on_key(RmlGLFW::ConvertKey(key), GLFW_RELEASE);
            } break;
            case Rml::EventId::Mousedown: {
                auto button = event.GetParameter("button", 0);
                AppUi::s_instance->app_window.on_mouse_button(button, GLFW_PRESS);
            } break;
            case Rml::EventId::Mouseup: {
                auto button = event.GetParameter("button", 0);
                AppUi::s_instance->app_window.on_mouse_button(button, GLFW_RELEASE);
            } break;
            default: break;
            }
        }
    };
    ViewportEventListener viewport_event_listener;

    void load_page(Rml::Context *context, Rml::String const &src_url) {
        auto *document = context->LoadDocument(src_url);
        document->Show();

        if (src_url == "src/ui/main.rml") {
            time_element = document->GetElementById("time");
            fps_element = document->GetElementById("fps");
            resolution_element = document->GetElementById("resolution");
            pause_element = document->GetElementById("pause");

            download_bar_element = document->GetElementById("download_bar");
            download_input_element = document->GetElementById("download_input");
            download_input_placeholder_element = document->GetElementById("download_input_placeholder");
            download_bar_element->AddEventListener(Rml::EventId::Blur, &download_bar_event_listener);

            AppUi::s_instance->viewport_element = document->GetElementById("viewport");
            AppUi::s_instance->viewport_element->AddEventListener(Rml::EventId::Mousedown, &viewport_event_listener);
            AppUi::s_instance->viewport_element->AddEventListener(Rml::EventId::Mouseup, &viewport_event_listener);
            AppUi::s_instance->viewport_element->AddEventListener(Rml::EventId::Mousemove, &viewport_event_listener);
            AppUi::s_instance->viewport_element->AddEventListener(Rml::EventId::Keydown, &viewport_event_listener);
            AppUi::s_instance->viewport_element->AddEventListener(Rml::EventId::Keyup, &viewport_event_listener);
        }
    }

    void load_fonts() {
        const Rml::String directory = "media/fonts/";

        struct FontFace {
            const char *filename;
            bool fallback_face;
        };
        auto font_faces = std::array{
            FontFace{"LatoLatin-Regular.ttf", false},
            FontFace{"LatoLatin-Italic.ttf", false},
            FontFace{"LatoLatin-Bold.ttf", false},
            FontFace{"LatoLatin-BoldItalic.ttf", false},
            FontFace{"NotoEmoji-Regular.ttf", true},
        };

        for (const FontFace &face : font_faces) {
            Rml::LoadFontFace(directory + face.filename, face.fallback_face);
        }
    }

    auto key_down_callback(Rml::Context *context, Rml::Input::KeyIdentifier key, int key_modifier, float native_dp_ratio, bool priority) -> bool {
        if (context == nullptr) {
            return true;
        }

        bool result = false;

        if (priority) {
            switch (key) {
            case Rml::Input::KI_R:
                if ((key_modifier & Rml::Input::KM_CTRL) != 0) {
                    auto docs_to_reload = std::vector<std::pair<Rml::String, Rml::ElementDocument *>>{};
                    for (int i = 0; i < context->GetNumDocuments(); i++) {
                        Rml::ElementDocument *document = context->GetDocument(i);
                        Rml::String const &src = document->GetSourceURL();
                        if (src.size() > 4 && src.substr(src.size() - 4) == ".rml") {
                            docs_to_reload.emplace_back(src, document);
                            document->ReloadStyleSheet();
                        }
                    }
                    for (auto const &[src_url, document] : docs_to_reload) {
                        document->Close();
                        load_page(context, src_url);
                    }
                }
                break;
            case Rml::Input::KI_F11:
                AppUi::s_instance->toggle_fullscreen();
                break;
            case Rml::Input::KI_ESCAPE:
                if (AppUi::s_instance->is_fullscreen) {
                    AppUi::s_instance->toggle_fullscreen();
                }
                break;
            case Rml::Input::KI_F8:
                Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
                break;
            case Rml::Input::KI_0:
                if ((key_modifier & Rml::Input::KM_CTRL) != 0) {
                    context->SetDensityIndependentPixelRatio(native_dp_ratio);
                }
                break;
            case Rml::Input::KI_1:
                if ((key_modifier & Rml::Input::KM_CTRL) != 0) {
                    context->SetDensityIndependentPixelRatio(1.f);
                }
                break;
            case Rml::Input::KI_OEM_MINUS: [[fallthrough]];
            case Rml::Input::KI_SUBTRACT:
                if ((key_modifier & Rml::Input::KM_CTRL) != 0) {
                    const float new_dp_ratio = Rml::Math::Max(context->GetDensityIndependentPixelRatio() / 1.2f, 0.5f);
                    context->SetDensityIndependentPixelRatio(new_dp_ratio);
                }
                break;
            case Rml::Input::KI_OEM_PLUS: [[fallthrough]];
            case Rml::Input::KI_ADD:
                if ((key_modifier & Rml::Input::KM_CTRL) != 0) {
                    const float new_dp_ratio = Rml::Math::Min(context->GetDensityIndependentPixelRatio() * 1.2f, 2.5f);
                    context->SetDensityIndependentPixelRatio(new_dp_ratio);
                }
                break;
            default:
                result = true;
                break;
            }
        }

        return result;
    }

    class Event : public Rml::EventListener {
      public:
        explicit Event(Rml::String value) : value(std::move(value)) {}

        void ProcessEvent(Rml::Event &event) override {
            if (value == "reset") {
                AppUi::s_instance->on_reset();
            } else if (value == "pause") {
                AppUi::s_instance->paused = !AppUi::s_instance->paused;
                if (AppUi::s_instance->paused) {
                    pause_element->SetAttribute("src", "../../media/icons/play.png");
                } else {
                    pause_element->SetAttribute("src", "../../media/icons/pause.png");
                }
                AppUi::s_instance->on_toggle_pause(AppUi::s_instance->paused);
            } else if (value == "fullscreen") {
                AppUi::s_instance->toggle_fullscreen();
            } else if (value == "download_button") {
                auto const display_prop = download_bar_element->GetProperty("display")->ToString();
                if (display_prop == "block") {
                    download_bar_element->SetProperty("display", "none");
                    download_input_element->Blur();
                } else {
                    download_bar_element->SetProperty("display", "block");
                    download_input_element->Focus();
                }
            } else if (value == "download_input_key") {
                auto key = event.GetParameter("key_identifier", 0);
                if (key == Rml::Input::KeyIdentifier::KI_RETURN ||
                    key == Rml::Input::KeyIdentifier::KI_NUMPADENTER) {
                    download_bar_element->SetProperty("display", "none");
                    download_input_element->Blur();
                    AppUi::s_instance->on_download(AppUi::s_instance->download_input);
                }
            }
        }

        void OnDetach(Rml::Element * /*element*/) override { delete this; }

      private:
        Rml::String value;
    };
} // namespace

auto EventInstancer::InstanceEventListener(const Rml::String &value, Rml::Element * /*element*/) -> Rml::EventListener * { return new Event(value); }

AppUi::AppUi(daxa::Device device)
    : app_window(device, daxa_i32vec2{1280, 720 + 24}),
      render_interface(device, app_window.swapchain.get_format()) {

    assert(s_instance == nullptr);
    s_instance = this;

    app_window.on_close = [&]() { should_close.store(true); };

    system_interface.SetWindow(app_window.glfw_window.get());

    Rml::SetSystemInterface(&system_interface);
    Rml::SetRenderInterface(&render_interface);

    Rml::Initialise();

    load_fonts();

    rml_context = Rml::CreateContext("main", Rml::Vector2i(app_window.size.x, app_window.size.y));
    app_window.rml_context = rml_context;

    Rml::Debugger::Initialise(rml_context);
    Rml::Factory::RegisterEventListenerInstancer(&event_instancer);

    if (Rml::DataModelConstructor constructor = rml_context->CreateDataModel("ui_data")) {
        constructor.Bind("download_input", &download_input);
    }

    load_page(rml_context, "src/ui/main.rml");
}

AppUi::~AppUi() {
    Rml::Shutdown();
}

void AppUi::update(float time, float fps) {
    app_window.key_down_callback = key_down_callback;
    app_window.update();

    auto time_str = fmt::format("{:.2f}", time);
    time_element->SetInnerRML(time_str);

    auto fps_str = fmt::format("{:.1f} fps", fps);
    fps_element->SetInnerRML(fps_str);

    if (AppUi::s_instance->download_input.empty()) {
        download_input_placeholder_element->SetProperty("display", "block");
    } else {
        download_input_placeholder_element->SetProperty("display", "none");
    }
}

void AppUi::render(daxa::CommandRecorder &recorder, daxa::ImageId target_image) {
    auto resolution_str = fmt::format("{} x {}", viewport_element->GetClientWidth(), viewport_element->GetClientHeight());
    resolution_element->SetInnerRML(resolution_str);

    rml_context->Update();
    render_interface.begin_frame(target_image, recorder);
    rml_context->Render();
    render_interface.end_frame(target_image, recorder);
}

void AppUi::toggle_fullscreen() {
    is_fullscreen = !is_fullscreen;
    on_toggle_fullscreen(is_fullscreen);
}
