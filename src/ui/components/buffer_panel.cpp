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

namespace {
    Rml::Element *buffer_menu_element{};
    Rml::ElementTabSet *buffer_tabs_element{};

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

    // TODO: make this automatic
    auto tab_index = 0;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Common</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Buffer A</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Buffer B</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Buffer C</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Buffer D</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Cube A</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetTab(tab_index, "<template src=\"buffer_tab\">Image</template>");
    buffer_tabs_element->SetPanel(tab_index, "<template src=\"buffer_panel\"> </template>");
    ++tab_index;

    buffer_tabs_element->SetActiveTab(1);
}

void BufferPanel::process_event(Rml::Event &event, std::string const &value) {
    if (value == "buffer_panel_ichannel0_settings") {
        // TODO: Fix me
        auto *ichannel0 = event.GetCurrentElement()->GetParentNode()->GetElementById("ichannel0");
        auto *ichannel_img = ichannel0->GetElementById("ichannel0_img");
        auto *ichannel_sampler_menu = ichannel0->GetElementById("ichannel_sampler_menu");
        ichannel_img->SetAttribute("style", "visibility: hidden; height: 0px;");
        ichannel_sampler_menu->SetAttribute("style", "visibility: visible;");
    }
}
