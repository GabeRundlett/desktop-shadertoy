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

namespace {
    class BufferMenuEventListener : public Rml::EventListener {
      public:
        void ProcessEvent(Rml::Event &event) override {
            switch (event.GetId()) {
            case Rml::EventId::Mousedown: {
                auto *element = event.GetTargetElement();
                auto const &element_id = element->GetId();
                if (element_id.starts_with("ichannel")) {
                    if (element_id == "ichannel0_img") {
                        std::cout << "test0" << std::endl;
                    } else if (element_id == "ichannel1_img") {
                        std::cout << "test1" << std::endl;
                    } else if (element_id == "ichannel2_img") {
                        std::cout << "test2" << std::endl;
                    } else if (element_id == "ichannel3_img") {
                        std::cout << "test3" << std::endl;
                    }
                }
            } break;
            default: break;
            }
        }
    };
    BufferMenuEventListener buffer_menu_event_listener;
} // namespace

void BufferPanel::load(Rml::Context *rml_context, Rml::ElementDocument *document) {
    buffer_menu_element = document->GetElementById("buffer_menu");
    buffer_menu_element->AddEventListener(Rml::EventId::Mousedown, &buffer_menu_event_listener);
    buffer_menu_element->AddEventListener(Rml::EventId::Mouseup, &buffer_menu_event_listener);
    buffer_menu_element->AddEventListener(Rml::EventId::Mousemove, &buffer_menu_event_listener);
    buffer_menu_element->AddEventListener(Rml::EventId::Keydown, &buffer_menu_event_listener);
    buffer_menu_element->AddEventListener(Rml::EventId::Keyup, &buffer_menu_event_listener);
    buffer_tabs_element = dynamic_cast<Rml::ElementTabSet *>(document->GetElementById("buffer_tabs"));
}

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

    if (during_shader_load) {
        return;
    }

    auto *tabs = buffer_tabs_element->GetChild(0);
    auto *tab = tabs->GetChild(buffer_tabs_element->GetActiveTab());
    auto *tab_div = tab->GetChild(1);
    auto *tab_div_content = dynamic_cast<Rml::ElementText *>(tab_div->GetChild(0));

    auto find_pass = [this](std::string const &pass_name) -> nlohmann::json & {
        auto &renderpasses = json["renderpass"];
        for (auto &renderpass : renderpasses) {
            auto name = std::string{};
            if (renderpass["name"] == pass_name) {
                return renderpass;
            }
        }
        // should never happen
        assert(0);
        return json;
    };

    if (value == "buffer_panel_change_filter") {
        auto *filter_select = dynamic_cast<Rml::ElementFormControlSelect *>(event.GetCurrentElement());
        auto *ichannel = filter_select->GetParentNode()->GetParentNode();
        auto ichannel_name = ichannel->GetId();
        auto &pass = find_pass(tab_div_content->GetText());
        auto channel_index = int(ichannel_name[8]) - int('0');
        pass["inputs"][channel_index]["sampler"]["filter"] = filter_select->GetValue();
        dirty = true;
    }
    if (value == "buffer_panel_change_wrap") {
        auto *wrap_select = dynamic_cast<Rml::ElementFormControlSelect *>(event.GetCurrentElement());
        auto *ichannel = wrap_select->GetParentNode()->GetParentNode();
        auto ichannel_name = ichannel->GetId();
        auto &pass = find_pass(tab_div_content->GetText());
        auto channel_index = int(ichannel_name[8]) - int('0');
        pass["inputs"][channel_index]["sampler"]["wrap"] = wrap_select->GetValue();
        dirty = true;
    }
}

void replace_all(std::string &s, std::string const &toReplace, std::string const &replaceWith, bool wordBoundary = false);

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

    auto tab_count = buffer_tabs_element->GetNumTabs();
    for (auto tab_i = 0; tab_i < tab_count; ++tab_i) {
        buffer_tabs_element->RemoveTab(tab_count - tab_i - 1);
    }

    for (auto &renderpass : renderpasses) {
        auto &type = renderpass["type"];
        auto &outputs = renderpass["outputs"];

        auto name = std::string{renderpass["name"]};

        buffer_tabs_element->SetTab(tab_index, fmt::format("<template src=\"buffer_tab\">{}</template>", name));
        buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");

        auto *panels = buffer_tabs_element->GetChild(1);
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
                        auto path = std::string(input["filepath"]);
                        replace_all(path, "/media/a/", "media/images/");

                        if (std::filesystem::exists(path)) {
                            std::cout << path << std::endl;
                            ichannel_img->SetAttribute("src", fmt::format("../../{}", path));
                            has_input = true;
                        } else if (input["type"] == "keyboard") {
                            ichannel_img->SetAttribute("src", "../../media/icons/keyboard.png");
                            has_input = true;
                        } else if (input["type"] == "buffer") {
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
                        } else if (input["type"] == "cubemap") {
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
                buffer_tabs_element->SetActiveTab(tab_index);
            }
        }

        ++tab_index;
    }

    dirty = true;
    during_shader_load = false;
}
