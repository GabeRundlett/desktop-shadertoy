#include "buffer_panel.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementTabSet.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger.h>

#include <iostream>
#include <fstream>

#include <fmt/format.h>

#include <ui/app_ui.hpp>

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
} // namespace

void BufferPanel::load(Rml::Context *rml_context, Rml::ElementDocument *document) {
    base_element = document->GetElementById("buffer_panel");
    base_element->AddEventListener(Rml::EventId::Mousedown, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Mouseup, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Mousemove, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Keydown, &buffer_panel_event_listener);
    base_element->AddEventListener(Rml::EventId::Keyup, &buffer_panel_event_listener);

    bpiw_element = document->GetElementById("bpiw");
    bpiw_element->AddEventListener(Rml::EventId::Mousedown, &bpiw_event_listener);
    bpiw_element->AddEventListener(Rml::EventId::Mouseup, &bpiw_event_listener);
    bpiw_element->AddEventListener(Rml::EventId::Blur, &bpiw_event_listener);

    tabs_element = dynamic_cast<Rml::ElementTabSet *>(document->GetElementById("buffer_tabs"));
}

void replace_all(std::string &s, std::string const &toReplace, std::string const &replaceWith, bool wordBoundary = false);

void BufferPanel::process_event(Rml::Event &event, std::string const &value) {
    if (value == "buffer_panel_ichannel0_settings" ||
        value == "buffer_panel_ichannel1_settings" ||
        value == "buffer_panel_ichannel2_settings" ||
        value == "buffer_panel_ichannel3_settings") {
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

    auto find_pass = [this](std::string const &pass_name) -> nlohmann::json * {
        auto &renderpasses = json["renderpass"];
        for (auto &renderpass : renderpasses) {
            auto name = std::string{};
            if (renderpass["name"] == pass_name) {
                return &renderpass;
            }
        }
        return nullptr;
    };
    auto find_input = [](nlohmann::json &pass_json, int channel_index) -> nlohmann::json * {
        for (auto &input : pass_json["inputs"]) {
            if (input["channel"] == channel_index) {
                return &input;
            }
        }
        return nullptr;
    };

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

            ichannel_img->SetAttribute("src", img_path);
            ichannel_img->SetAttribute("style", "display: inline-block; image-color: #ffffff;");
            ichannel_sampler_menu->SetAttribute("style", "display: none;");
            ichannel_label_settings->SetAttribute("style", "display: block;");

            auto ichannel_name = ichannel->GetId();
            auto &pass = *find_pass(tab_div_content->GetText());
            auto channel_index = int(ichannel_name[8]) - int('0');

            auto *bpiw_tabs = dynamic_cast<Rml::ElementTabSet *>(bpiw_element->GetElementById("bpiw_tabs"));

            auto new_input = nlohmann::json{};
            new_input["id"] = std::to_string(std::hash<std::string>{}(img_path));
            replace_all(img_path, "../../media/images/", "/media/a/");
            auto default_sampler = nlohmann::json{};
            default_sampler["filter"] = "linear";
            default_sampler["wrap"] = "clamp";
            default_sampler["vflip"] = "true";
            default_sampler["srgb"] = "false";
            default_sampler["internal"] = "byte";

            switch (bpiw_tabs->GetActiveTab()) {
            case 0: {
                if (img_path == "../../media/icons/keyboard.png") {
                    new_input["filepath"] = "/presets/tex00.jpg";
                    new_input["type"] = "keyboard";
                    new_input["channel"] = channel_index;
                    new_input["sampler"] = default_sampler;
                    new_input["sampler"]["filter"] = "nearest";
                    new_input["published"] = 1;
                } else if (img_path == "../../media/icons/buffer00.png") {
                    auto *input_pass = find_pass("Buffer A");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer00.png";
                        new_input["type"] = "buffer";
                        new_input["channel"] = channel_index;
                        new_input["sampler"] = default_sampler;
                        new_input["published"] = 1;
                    }
                } else if (img_path == "../../media/icons/buffer01.png") {
                    auto *input_pass = find_pass("Buffer B");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer00.png";
                        new_input["type"] = "buffer";
                        new_input["channel"] = channel_index;
                        new_input["sampler"] = default_sampler;
                        new_input["published"] = 1;
                    }
                } else if (img_path == "../../media/icons/buffer02.png") {
                    auto *input_pass = find_pass("Buffer C");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer00.png";
                        new_input["type"] = "buffer";
                        new_input["channel"] = channel_index;
                        new_input["sampler"] = default_sampler;
                        new_input["published"] = 1;
                    }
                } else if (img_path == "../../media/icons/buffer03.png") {
                    auto *input_pass = find_pass("Buffer D");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/buffer00.png";
                        new_input["type"] = "buffer";
                        new_input["channel"] = channel_index;
                        new_input["sampler"] = default_sampler;
                        new_input["published"] = 1;
                    }
                } else if (img_path == "../../media/icons/cubemap00.png") {
                    auto *input_pass = find_pass("Cubemap A");
                    if (input_pass != nullptr) {
                        new_input["id"] = (*input_pass)["outputs"][0]["id"];
                        new_input["filepath"] = "/media/previz/cubemap00.png";
                        new_input["type"] = "cubemap";
                        new_input["channel"] = channel_index;
                        new_input["sampler"] = default_sampler;
                        new_input["published"] = 1;
                    }
                }
            } break;
            case 1: {
                new_input["filepath"] = img_path;
                new_input["type"] = "texture";
                new_input["channel"] = channel_index;
                new_input["sampler"] = default_sampler;
                new_input["sampler"]["filter"] = "mipmap";
                new_input["sampler"]["wrap"] = "repeat";
                new_input["published"] = 1;
            } break;
            case 2: {
                new_input["filepath"] = img_path;
                new_input["type"] = "cubemap";
                new_input["channel"] = channel_index;
                new_input["sampler"] = default_sampler;
                new_input["published"] = 1;
            } break;
            }

            auto *channel_input = find_input(pass, channel_index);
            if (channel_input != nullptr) {
                *channel_input = new_input;
            } else {
                pass["inputs"].push_back(new_input);
            }

            // auto &pass_inputs = pass["inputs"];
            // [channel_index]["sampler"]["wrap"] = wrap_select->GetValue();

            bpiw_element->SetAttribute("style", "display: none;");
            bpiw_element->Blur();
            dirty = true;
        }
    }
}

void BufferPanel::load_shadertoy_json(nlohmann::json const &temp_json) {
    during_shader_load = true;

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
                    ichannel_sampler_menu->SetAttribute("style", "display: none;");
                    ichannel_label_settings->SetAttribute("style", "display: block;");
                } else {
                    ichannel_img->SetAttribute("style", "display: inline-block; image-color: #000000;");
                    ichannel_img->SetAttribute("src", "");
                    ichannel_sampler_menu->SetAttribute("style", "display: none;");
                    ichannel_label_settings->SetAttribute("style", "display: none;");
                }
            }

            if (name == "Image") {
                tabs_element->SetActiveTab(tab_index);
            }
        }

        ++tab_index;
    }

    dirty = true;
    during_shader_load = false;
}
