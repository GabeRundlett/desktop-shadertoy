#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace Rml {
    class Context;
    class ElementDocument;
    class Event;
    class Element;
    class ElementTabSet;
} // namespace Rml

struct BufferPanel {
    nlohmann::json json;
    bool dirty = false;
    bool during_shader_load = false;

    Rml::Element *buffer_menu_element{};
    Rml::ElementTabSet *buffer_tabs_element{};

    void load(Rml::Context *rml_context, Rml::ElementDocument *document);
    void process_event(Rml::Event &event, std::string const &value);

    void load_shadertoy_json(nlohmann::json const &temp_json);
    [[nodiscard]] auto get_shadertoy_json() const -> auto const & {
        return json;
    }
};
