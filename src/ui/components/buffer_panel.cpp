#include "buffer_panel.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementTabSet.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger.h>

#include <iostream>
#include <fstream>

#include <ui/app_ui.hpp>

#include <fmt/format.h>
#include <thomasmonkman-filewatch/FileWatch.hpp>

void replace_all(std::string &s, std::string const &toReplace, std::string const &replaceWith, bool wordBoundary = false);

struct BufferFileEditState {
    std::string name;
    std::filesystem::path path;
    filewatch::FileWatch<std::string> file_watch;
    std::atomic_bool modified = false;

    ~BufferFileEditState() {
        // delete the file?
        auto ec = std::error_code{};
        std::filesystem::remove(path, ec);
        // ignore error code.
    }
};

namespace {
    class BufferPanelEventListener : public Rml::EventListener {
      public:
        void ProcessEvent(Rml::Event &event) override {
            switch (event.GetId()) {
            case Rml::EventId::Mouseup: {
                auto *element = event.GetTargetElement();
                auto const &element_id = element->GetId();
                if (element_id.starts_with("ichannel")) {
                    if (element_id == "ichannel0_img" ||
                        element_id == "ichannel1_img" ||
                        element_id == "ichannel2_img" ||
                        element_id == "ichannel3_img") {
                        // Show input selection window
                        AppUi::s_instance->buffer_panel.open_ichannel_img_element = element;
                        auto *bpiw_element = AppUi::s_instance->buffer_panel.bpiw_element;
                        bpiw_element->SetAttribute("style", "display: block;");
                        bpiw_element->Focus();
                    }
                }
            } break;
            default: break;
            }
        }
    };
    BufferPanelEventListener buffer_panel_event_listener;

    class BpiwEventListener : public Rml::EventListener {
      public:
        void ProcessEvent(Rml::Event &event) override {
            switch (event.GetId()) {
            case Rml::EventId::Blur: {
                auto *bpiw_element = AppUi::s_instance->buffer_panel.bpiw_element;
                bpiw_element->SetAttribute("style", "display: none;");
                AppUi::s_instance->buffer_panel.open_ichannel_img_element = nullptr;
            } break;
            default: break;
            }
        }
    };
    BpiwEventListener bpiw_event_listener;

    class BufferPanelAddOptionsEventListener : public Rml::EventListener {
      public:
        void ProcessEvent(Rml::Event &event) override {
            switch (event.GetId()) {
            case Rml::EventId::Blur: {
                event.GetCurrentElement()->SetAttribute("style", "display: none;");
            } break;
            default: break;
            }
        }
    };
    BufferPanelAddOptionsEventListener buffer_panel_add_options_event_listener;

    auto random_string(size_t length) -> std::string {
        auto randchar = []() -> char {
            const char charset[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 1);
            return charset[rand() % max_index];
        };
        std::string str(length, 0);
        std::generate_n(str.begin(), length, randchar);
        return str;
    }

    auto find_pass(std::string const &pass_name) -> nlohmann::json * {
        auto &renderpasses = AppUi::s_instance->buffer_panel.json["renderpass"];
        for (auto &renderpass : renderpasses) {
            auto name = std::string{};
            if (renderpass["name"] == pass_name) {
                return &renderpass;
            }
        }
        return nullptr;
    }

    auto find_input(nlohmann::json &pass_json, int channel_index) -> nlohmann::json * {
        for (auto &input : pass_json["inputs"]) {
            if (input["channel"] == channel_index) {
                return &input;
            }
        }
        return nullptr;
    }

    void on_file_update(std::string const &path, filewatch::Event const change_type) {
        if (change_type != filewatch::Event::modified) {
            // we don't care about anything except modifications
            return;
        }

        auto name = path.substr(0, path.find('_'));

        auto *edit_state_ptr = (BufferFileEditState *)nullptr;
        if (name == "Common") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.common_file_edit_state;
        } else if (name == "Buffer A") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.buffer00_file_edit_state;
        } else if (name == "Buffer B") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.buffer01_file_edit_state;
        } else if (name == "Buffer C") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.buffer02_file_edit_state;
        } else if (name == "Buffer D") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.buffer03_file_edit_state;
        } else if (name == "Cube A") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.cubemap00_file_edit_state;
        } else if (name == "Image") {
            edit_state_ptr = AppUi::s_instance->buffer_panel.image_file_edit_state;
        } else {
            return;
        }

        edit_state_ptr->modified = true;
    }
} // namespace

BufferPanel::~BufferPanel() {
    cleanup();
}

void BufferPanel::load(Rml::Context *rml_context, Rml::ElementDocument *document) {
    base_element = document->GetElementById("buffer_panel");
    base_element->AddEventListener(Rml::EventId::Mousedown, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Mouseup, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Mousemove, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Keydown, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Keyup, &buffer_panel_event_listener);

    bpiw_element = document->GetElementById("bpiw");
    bpiw_element->AddEventListener(Rml::EventId::Blur, &bpiw_event_listener);

    auto *buffer_panel_add_panel = document->GetElementById("buffer_panel_add_panel");
    buffer_panel_add_panel->AddEventListener(Rml::EventId::Blur, &buffer_panel_add_options_event_listener);

    tabs_element = dynamic_cast<Rml::ElementTabSet *>(document->GetElementById("buffer_tabs"));
}

void BufferPanel::process_event(Rml::Event &event, std::string const &value) {
    if (value == "buffer_panel_ichannel_settings") {
        auto *ichannel = event.GetCurrentElement()->GetParentNode()->GetParentNode()->GetChild(0);
        auto *ichannel_img = ichannel->GetChild(0);
        auto *ichannel_sampler_menu = ichannel->GetChild(1);

        auto is_sampler_menu_closed = ichannel_sampler_menu->GetAttribute("style")->Get(Rml::String("display: none;")) == "display: none;";
        if (is_sampler_menu_closed) {
            ichannel_img->SetAttribute("style", "display: none; image-color: #000000;");
            ichannel_sampler_menu->SetAttribute("style", "display: inline-block;");
        } else {
            ichannel_img->SetAttribute("style", "display: inline-block; image-color: #ffffff;");
            ichannel_sampler_menu->SetAttribute("style", "display: none;");
        }
    }

    if (value == "buffer_panel_bpiw_close") {
        bpiw_element->SetAttribute("style", "display: none;");
        bpiw_element->Blur();
    }

    if (during_shader_load) {
        return;
    }

    auto *tabs = tabs_element->GetChild(0);
    auto *tab = tabs->GetChild(tabs_element->GetActiveTab());
    auto *tab_div = tab->GetChild(1);
    auto *tab_div_content = dynamic_cast<Rml::ElementText *>(tab_div->GetChild(0));

    if (value == "buffer_panel_change_filter") {
        auto *filter_select = dynamic_cast<Rml::ElementFormControlSelect *>(event.GetCurrentElement());
        auto *ichannel = filter_select->GetParentNode()->GetParentNode();
        auto ichannel_name = ichannel->GetId();
        auto &pass = *find_pass(tab_div_content->GetText());
        auto channel_index = int(ichannel_name[8]) - int('0');
        auto *channel_input = find_input(pass, channel_index);
        if (channel_input != nullptr) {
            auto &input = *channel_input;
            input["sampler"]["filter"] = filter_select->GetValue();
            dirty = true;
        }
    }
    if (value == "buffer_panel_change_wrap") {
        auto *wrap_select = dynamic_cast<Rml::ElementFormControlSelect *>(event.GetCurrentElement());
        auto *ichannel = wrap_select->GetParentNode()->GetParentNode();
        auto ichannel_name = ichannel->GetId();
        auto &pass = *find_pass(tab_div_content->GetText());
        auto channel_index = int(ichannel_name[8]) - int('0');
        auto *channel_input = find_input(pass, channel_index);
        if (channel_input != nullptr) {
            auto &input = *channel_input;
            input["sampler"]["wrap"] = wrap_select->GetValue();
            dirty = true;
        }
    }
    if (value == "buffer_panel_bpiw_select") {
        auto *select_div = event.GetCurrentElement();
        auto *select_img = select_div->GetChild(0);
        if (select_img != nullptr && open_ichannel_img_element != nullptr) {
            auto img_path = select_img->GetAttribute("src")->Get(Rml::String(""));

            auto *datagrid_column = open_ichannel_img_element->GetParentNode()->GetParentNode();
            auto *ichannel = datagrid_column->GetChild(0);
            auto *ichannel_label = datagrid_column->GetChild(1);
            auto *ichannel_label_settings = ichannel_label->GetChild(1);

            auto *ichannel_img = ichannel->GetChild(0);
            auto *ichannel_sampler_menu = ichannel->GetChild(1);
            auto *ichannel_close = ichannel->GetChild(2);

            ichannel_img->SetAttribute("src", img_path);
            ichannel_img->SetAttribute("style", "display: inline-block; image-color: #ffffff;");
            ichannel_close->SetAttribute("style", "display: inline-block;");
            ichannel_sampler_menu->SetAttribute("style", "display: none;");
            ichannel_label_settings->SetAttribute("style", "display: block;");

            auto ichannel_name = ichannel->GetId();
            auto &pass = *find_pass(tab_div_content->GetText());
            auto channel_index = int(ichannel_name[8]) - int('0');

            auto *bpiw_tabs = dynamic_cast<Rml::ElementTabSet *>(bpiw_element->GetElementById("bpiw_tabs"));

            replace_all(img_path, "../../media/images/", "/media/a/");
            auto default_sampler = nlohmann::json{};
            default_sampler["filter"] = "linear";
            default_sampler["wrap"] = "clamp";
            default_sampler["vflip"] = "true";
            default_sampler["srgb"] = "false";
            default_sampler["internal"] = "byte";

            auto new_input = nlohmann::json{};
            new_input["id"] = std::to_string(std::hash<std::string>{}(img_path));
            new_input["channel"] = channel_index;
            new_input["sampler"] = default_sampler;
            new_input["published"] = 1;

            switch (bpiw_tabs->GetActiveTab()) {
            case 0: {
                if (img_path == "../../media/icons/keyboard.png") {
                    new_input["filepath"] = "/presets/tex00.jpg";
                    new_input["type"] = "keyboard";
                    new_input["sampler"]["filter"] = "nearest";
                } else if (img_path == "../../media/icons/buffer00.png") {
                    auto *input_pass = find_pass("Buffer A");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer00.png";
                        new_input["type"] = "buffer";
                    }
                } else if (img_path == "../../media/icons/buffer01.png") {
                    auto *input_pass = find_pass("Buffer B");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer01.png";
                        new_input["type"] = "buffer";
                    }
                } else if (img_path == "../../media/icons/buffer02.png") {
                    auto *input_pass = find_pass("Buffer C");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer02.png";
                        new_input["type"] = "buffer";
                    }
                } else if (img_path == "../../media/icons/buffer03.png") {
                    auto *input_pass = find_pass("Buffer D");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer03.png";
                        new_input["type"] = "buffer";
                    }
                } else if (img_path == "../../media/icons/cubemap00.png") {
                    auto *input_pass = find_pass("Cube A");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/cubemap00.png";
                        new_input["type"] = "cubemap";
                    }
                }
            } break;
            case 1: {
                new_input["filepath"] = img_path;
                new_input["type"] = "texture";
                new_input["sampler"]["filter"] = "mipmap";
                new_input["sampler"]["wrap"] = "repeat";
            } break;
            case 2: {
                new_input["filepath"] = img_path;
                new_input["type"] = "cubemap";
            } break;
            }

            auto *channel_input = find_input(pass, channel_index);
            if (channel_input != nullptr) {
                *channel_input = new_input;
            } else {
                pass["inputs"].push_back(new_input);
            }

            bpiw_element->SetAttribute("style", "display: none;");
            bpiw_element->Blur();
            dirty = true;
        }
    }
    if (value == "buffer_panel_ichannel_close") {
        auto *datagrid_column = event.GetCurrentElement()->GetParentNode()->GetParentNode();
        auto *ichannel = datagrid_column->GetChild(0);

        auto ichannel_name = ichannel->GetId();
        auto &pass = *find_pass(tab_div_content->GetText());
        auto channel_index = int(ichannel_name[8]) - int('0');

        int current_index = 0;
        for (auto &input : pass["inputs"]) {
            if (input["channel"] == channel_index) {
                auto *ichannel_label = datagrid_column->GetChild(1);
                auto *ichannel_label_settings = ichannel_label->GetChild(1);

                auto *ichannel_img = ichannel->GetChild(0);
                auto *ichannel_sampler_menu = ichannel->GetChild(1);
                auto *ichannel_close = ichannel->GetChild(2);

                ichannel_img->SetAttribute("style", "display: inline-block; image-color: #000000;");
                ichannel_img->SetAttribute("src", "");
                ichannel_close->SetAttribute("style", "display: none;");
                ichannel_sampler_menu->SetAttribute("style", "display: none;");
                ichannel_label_settings->SetAttribute("style", "display: none;");

                pass["inputs"].erase(current_index);
                dirty = true;
                break;
            }
            ++current_index;
        }
    }
    if (value == "buffer_panel_add") {
        auto *buffer_panel_add_panel = event.GetCurrentElement()->GetElementById("buffer_panel_add_panel");
        buffer_panel_add_panel->SetAttribute("style", "display: inline-block;");
        buffer_panel_add_panel->Focus();

        auto *buffer_panel_add_common = buffer_panel_add_panel->GetElementById("buffer_panel_add_common");
        auto *buffer_panel_add_buffer00 = buffer_panel_add_panel->GetElementById("buffer_panel_add_buffer00");
        auto *buffer_panel_add_buffer01 = buffer_panel_add_panel->GetElementById("buffer_panel_add_buffer01");
        auto *buffer_panel_add_buffer02 = buffer_panel_add_panel->GetElementById("buffer_panel_add_buffer02");
        auto *buffer_panel_add_buffer03 = buffer_panel_add_panel->GetElementById("buffer_panel_add_buffer03");
        auto *buffer_panel_add_cubemap00 = buffer_panel_add_panel->GetElementById("buffer_panel_add_cubemap00");

        auto add_option_to_list = [&](auto &&name, auto *option_element) {
            auto *channel_input = find_pass(name);
            if (channel_input != nullptr) {
                // pass already exists, hide it
                option_element->SetAttribute("style", "display: none;");
            } else {
                // show it in the list
                option_element->SetAttribute("style", "display: block;");
            }
        };

        add_option_to_list("Common", buffer_panel_add_common);
        add_option_to_list("Buffer A", buffer_panel_add_buffer00);
        add_option_to_list("Buffer B", buffer_panel_add_buffer01);
        add_option_to_list("Buffer C", buffer_panel_add_buffer02);
        add_option_to_list("Buffer D", buffer_panel_add_buffer03);
        add_option_to_list("Cube A", buffer_panel_add_cubemap00);
    }
    if (value == "buffer_panel_add_option") {
        auto *buffer_panel_add_panel = event.GetCurrentElement()->GetElementById("buffer_panel_add_panel");
        buffer_panel_add_panel->SetAttribute("style", "display: none;");
        buffer_panel_add_panel->Blur();

        // Add the buffer to the passes
        // NOTE(grundlett): I'll just modify the json and reload it...

        auto &renderpasses = json["renderpass"];

        auto new_pass = nlohmann::json{};
        auto element_id = event.GetCurrentElement()->GetId();

        auto iter = renderpasses.begin();

        if (element_id != "buffer_panel_add_common") {
            // find image pass
            while (iter != renderpasses.end()) {
                // Buffer passes should always be before the cube/image passes.
                if ((*iter)["name"] == "Image" || (*iter)["name"] == "Cube A") {
                    break;
                }
                ++iter;
            }
        }

        if (iter == renderpasses.end()) {
            // should never happen
            return;
        }

        if (element_id == "buffer_panel_add_common") {
            new_pass = nlohmann::json::parse(R"({
                "outputs": [],
                "inputs": [],
                "code": "vec4 someFunction( vec4 a, float b )\n{\n    return a+b;\n}",
                "name": "Common",
                "description": "",
                "type": "common"
            })");
        } else if (element_id == "buffer_panel_add_buffer00") {
            new_pass = nlohmann::json::parse(R"({
                "outputs": [
                    {
                        "channel": 0,
                        "id": "4dXGR8"
                    }
                ],
                "inputs": [],
                "code": "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n    fragColor = vec4(0.0,0.0,1.0,1.0);\n}",
                "name": "Buffer A",
                "description": "",
                "type": "buffer"
            })");
        } else if (element_id == "buffer_panel_add_buffer01") {
            new_pass = nlohmann::json::parse(R"({
                "outputs": [
                    {
                        "channel": 0,
                        "id": "XsXGR8"
                    }
                ],
                "inputs": [],
                "code": "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n    fragColor = vec4(0.0,0.0,1.0,1.0);\n}",
                "name": "Buffer B",
                "description": "",
                "type": "buffer"
            })");
        } else if (element_id == "buffer_panel_add_buffer02") {
            new_pass = nlohmann::json::parse(R"({
                "outputs": [
                    {
                        "channel": 0,
                        "id": "4sXGR8"
                    }
                ],
                "inputs": [],
                "code": "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n    fragColor = vec4(0.0,0.0,1.0,1.0);\n}",
                "name": "Buffer C",
                "description": "",
                "type": "buffer"
            })");
        } else if (element_id == "buffer_panel_add_buffer03") {
            new_pass = nlohmann::json::parse(R"({
                "outputs": [
                    {
                        "channel": 0,
                        "id": "XdfGR8"
                    }
                ],
                "inputs": [],
                "code": "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n    fragColor = vec4(0.0,0.0,1.0,1.0);\n}",
                "name": "Buffer D",
                "description": "",
                "type": "buffer"
            })");
        } else if (element_id == "buffer_panel_add_cubemap00") {
            new_pass = nlohmann::json::parse(R"({
                "outputs": [
                    {
                        "channel": 0,
                        "id": "4dX3Rr"
                    }
                ],
                "inputs": [],
                "code": "void mainCubemap( out vec4 fragColor, in vec2 fragCoord, in vec3 rayOri, in vec3 rayDir )\n{\n    // Ray direction as color\n    vec3 col = 0.5 + 0.5*rayDir;\n\n    // Output to cubemap\n    fragColor = vec4(col,1.0);\n}",
                "name": "Cube A",
                "description": "",
                "type": "cubemap"
            })");
        }

        auto tab_index = static_cast<int32_t>(iter - renderpasses.begin());

        renderpasses.insert(iter, new_pass);
        reload_json();

        tabs_element->SetActiveTab(tab_index);
    }
    if (value == "buffer_panel_tab_close") {
        auto &renderpasses = json["renderpass"];

        auto new_pass = nlohmann::json{};
        auto *element = event.GetCurrentElement();
        auto *parent = element->GetParentNode();
        auto *content_div = parent->GetChild(1);
        auto *tab_content = dynamic_cast<Rml::ElementText *>(content_div->GetChild(0));

        auto name = tab_content->GetText();

        auto iter = renderpasses.begin();
        while (iter != renderpasses.end()) {
            if ((*iter)["name"] == name) {
                break;
            }
            ++iter;
        }

        if (iter == renderpasses.end()) {
            // Should never happen
            return;
        }

        renderpasses.erase(iter);
        // Cope because we are about to rebuild the UI while it's propagating the events
        event.StopImmediatePropagation();
        reload_json();
    }
    if (value == "buffer_panel_tab_edit") {
        auto &renderpasses = json["renderpass"];

        auto new_pass = nlohmann::json{};
        auto *element = event.GetCurrentElement();
        auto *parent = element->GetParentNode();
        auto *content_div = parent->GetChild(1);
        auto *tab_content = dynamic_cast<Rml::ElementText *>(content_div->GetChild(0));

        auto name = tab_content->GetText();

        auto *edit_state_ptr = (BufferFileEditState **)nullptr;
        if (name == "Common") {
            edit_state_ptr = &common_file_edit_state;
        } else if (name == "Buffer A") {
            edit_state_ptr = &buffer00_file_edit_state;
        } else if (name == "Buffer B") {
            edit_state_ptr = &buffer01_file_edit_state;
        } else if (name == "Buffer C") {
            edit_state_ptr = &buffer02_file_edit_state;
        } else if (name == "Buffer D") {
            edit_state_ptr = &buffer03_file_edit_state;
        } else if (name == "Cube A") {
            edit_state_ptr = &cubemap00_file_edit_state;
        } else if (name == "Image") {
            edit_state_ptr = &image_file_edit_state;
        } else {
            return;
        }

        if (*edit_state_ptr != nullptr) {
            // ensure file still exists
            auto &edit_state = **edit_state_ptr;
            if (!std::filesystem::exists(edit_state.path)) {
                // if it doesn't exist anymore, then we need to destroy our state and recreate it
                delete *edit_state_ptr;
                *edit_state_ptr = nullptr;
            }
        }

        if (*edit_state_ptr == nullptr) {
            // create new file and edit state

            auto new_temp_filepath = [&name]() {
                return std::filesystem::temp_directory_path() / (name + "_" + random_string(6) + ".glsl");
            };

            auto path = std::filesystem::path{};

            while (true) {
                path = new_temp_filepath();
                if (!std::filesystem::exists(path)) {
                    break;
                }
            }

            auto *pass_ptr = find_pass(name);
            if (pass_ptr == nullptr) {
                // This again should never happen
                return;
            }

            auto &pass = *pass_ptr;
            auto content = std::string(pass["code"]);
            replace_all(content, "\\n", "\n");

            auto file = std::ofstream{path};
            file << content;
            file.close();

            *edit_state_ptr = new BufferFileEditState{
                .name = name,
                .path = path,
                .file_watch = filewatch::FileWatch<std::string>(path.string(), on_file_update),
            };
            auto &edit_state = **edit_state_ptr;
        }
        auto &edit_state = **edit_state_ptr;

#if _WIN32
        std::system(fmt::format("explorer.exe {}", edit_state.path.string()).c_str());
#else
        std::system(fmt::format("open {}", edit_state.path.string()).c_str());
#endif
    }
}

void BufferPanel::cleanup() {
    delete common_file_edit_state;
    delete buffer00_file_edit_state;
    delete buffer01_file_edit_state;
    delete buffer02_file_edit_state;
    delete buffer03_file_edit_state;
    delete cubemap00_file_edit_state;
    delete image_file_edit_state;

    common_file_edit_state = nullptr;
    buffer00_file_edit_state = nullptr;
    buffer01_file_edit_state = nullptr;
    buffer02_file_edit_state = nullptr;
    buffer03_file_edit_state = nullptr;
    cubemap00_file_edit_state = nullptr;
    image_file_edit_state = nullptr;
}

void BufferPanel::update() {
    auto code_changed = false;

    auto update_edit_state = [&](BufferFileEditState *edit_state_ptr) {
        if (edit_state_ptr == nullptr) {
            return;
        }
        if (!edit_state_ptr->modified.load()) {
            return;
        }

        // if it was modified, we want to update the shader.

        auto new_content = [&]() {
            auto file = std::ifstream(edit_state_ptr->path);
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }();

        if (new_content.empty()) {
            return;
        }

        auto *pass_ptr = find_pass(edit_state_ptr->name);
        if (pass_ptr == nullptr) {
            // Shouldn't ever happen
            return;
        }
        auto &pass = *pass_ptr;
        auto old_content = std::string(pass["code"]);
        if (new_content != old_content) {
            pass["code"] = new_content;
            code_changed = true;
        }
    };

    update_edit_state(common_file_edit_state);
    update_edit_state(buffer00_file_edit_state);
    update_edit_state(buffer01_file_edit_state);
    update_edit_state(buffer02_file_edit_state);
    update_edit_state(buffer03_file_edit_state);
    update_edit_state(cubemap00_file_edit_state);
    update_edit_state(image_file_edit_state);

    if (code_changed) {
        AppUi::s_instance->buffer_panel.reload_json();
    }
}

void BufferPanel::load_shadertoy_json(nlohmann::json const &temp_json) {
    if (temp_json.contains("numShaders")) {
        // Is a "export all shaders" json file. Let's split it up for the user.
        for (auto &shader : temp_json["shaders"]) {
            auto filepath = std::string{"shader_"} + std::string{shader["info"]["id"]} + std::string{".json"};
            auto f = std::ofstream(filepath);
            f << std::setw(4) << shader;
        }

        json = temp_json["shaders"][0];
    } else {
        json = temp_json;
    }
    cleanup();
    reload_json();
}
void BufferPanel::reload_json() {
    during_shader_load = true;

    auto &renderpasses = json["renderpass"];

    auto tab_index = 0;

    auto tab_count = tabs_element->GetNumTabs();
    for (auto tab_i = 0; tab_i < tab_count; ++tab_i) {
        tabs_element->RemoveTab(tab_count - tab_i - 1);
    }

    for (auto &renderpass : renderpasses) {
        auto &type = renderpass["type"];
        auto &outputs = renderpass["outputs"];

        auto name = std::string{renderpass["name"]};

        tabs_element->SetTab(tab_index, fmt::format("<template src=\"buffer_tab\">{}</template>", name));
        tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");

        auto *tabs = tabs_element->GetChild(0);
        auto *tab = tabs->GetChild(tab_index);

        auto *panels = tabs_element->GetChild(1);
        auto *panel = panels->GetChild(tab_index);
        auto *datagrid = panel->GetChild(0);
        auto *datagrid_header = datagrid->GetChild(0);

        auto &inputs = renderpass["inputs"];

        if (renderpass["type"] == "common") {
            datagrid->SetAttribute("style", "display: none;");
        } else {
            for (auto channel_i = 0; channel_i < 4; ++channel_i) {
                auto *datagrid_column = datagrid_header->GetChild(channel_i);
                auto *ichannel = datagrid_column->GetChild(0);
                auto *ichannel_label = datagrid_column->GetChild(1);
                auto *ichannel_label_settings = ichannel_label->GetChild(1);

                auto *ichannel_img = ichannel->GetChild(0);
                auto *ichannel_sampler_menu = ichannel->GetChild(1);
                auto *ichannel_close = ichannel->GetChild(2);

                bool has_input = false;

                for (auto &input : inputs) {
                    if (input["channel"] == channel_i) {
                        auto path = std::string{};
                        if (input.contains("filepath")) {
                            path = input["filepath"];
                        } else if (input.contains("src")) {
                            path = input["src"];
                        } else {
                            // ?
                            continue;
                        }
                        replace_all(path, "/media/a/", "media/images/");

                        auto type = std::string{};
                        if (input.contains("type")) {
                            type = input["type"];
                        } else if (input.contains("ctype")) {
                            type = input["ctype"];
                        } else {
                            // ?
                            continue;
                        }

                        if (std::filesystem::exists(path)) {
                            std::cout << path << std::endl;
                            ichannel_img->SetAttribute("src", fmt::format("../../{}", path));
                            has_input = true;
                        } else if (type == "keyboard") {
                            ichannel_img->SetAttribute("src", "../../media/icons/keyboard.png");
                            has_input = true;
                        } else if (type == "buffer") {
                            auto buffer_index = 0;
                            for (auto &input_renderpass : renderpasses) {
                                if (input_renderpass["type"] == "buffer") {
                                    if (input_renderpass["outputs"][0]["id"] == input["id"]) {
                                        break;
                                    }
                                    ++buffer_index;
                                }
                            }

                            ichannel_img->SetAttribute("src", fmt::format("../../media/icons/buffer0{}.png", buffer_index));
                            has_input = true;
                        } else if (type == "cubemap") {
                            ichannel_img->SetAttribute("src", "../../media/icons/cubemap00.png");
                            has_input = true;
                        }

                        if (input.contains("sampler")) {
                            auto &sampler = input["sampler"];
                            auto *filter_select = dynamic_cast<Rml::ElementFormControlSelect *>(ichannel_sampler_menu->GetChild(1));
                            auto *wrap_select = dynamic_cast<Rml::ElementFormControlSelect *>(ichannel_sampler_menu->GetChild(4));
                            auto filter = std::string{"linear"};
                            auto wrap = std::string{"repeat"};
                            if (sampler.contains("filter")) {
                                filter = std::string(sampler["filter"]);
                            }
                            if (sampler.contains("wrap")) {
                                wrap = std::string(sampler["wrap"]);
                            }
                            filter_select->SetValue(filter);
                            wrap_select->SetValue(wrap);
                        }
                    }
                }

                if (has_input) {
                    ichannel_img->SetAttribute("style", "display: inline-block; image-color: #ffffff;");
                    ichannel_close->SetAttribute("style", "display: inline-block;");
                    ichannel_sampler_menu->SetAttribute("style", "display: none;");
                    ichannel_label_settings->SetAttribute("style", "display: block;");
                } else {
                    ichannel_img->SetAttribute("style", "display: inline-block; image-color: #000000;");
                    ichannel_img->SetAttribute("src", "");
                    ichannel_close->SetAttribute("style", "display: none;");
                    ichannel_sampler_menu->SetAttribute("style", "display: none;");
                    ichannel_label_settings->SetAttribute("style", "display: none;");
                }
            }

            if (name == "Image") {
                auto *tab_close_element = tab->GetChild(2);
                tab_close_element->SetAttribute("style", "display: none;");
                tabs_element->SetActiveTab(tab_index);
            }
        }

        ++tab_index;
    }

    dirty = true;
    during_shader_load = false;
}
