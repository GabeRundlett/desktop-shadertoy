#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace Rml {
    class Context;
    class ElementDocument;
    class Event;
    class Element;
    class ElementTabSet;
    class ElementText;
} // namespace Rml

struct BufferFileEditState;

struct BufferPanel {
    nlohmann::json json;
    bool dirty = false;
    bool during_shader_load = false;

    Rml::Element *base_element{};
    Rml::ElementTabSet *tabs_element{};
    Rml::Element *bpiw_element{};
    Rml::Element *open_ichannel_img_element{};

    BufferFileEditState *common_file_edit_state{};
    BufferFileEditState *buffer00_file_edit_state{};
    BufferFileEditState *buffer01_file_edit_state{};
    BufferFileEditState *buffer02_file_edit_state{};
    BufferFileEditState *buffer03_file_edit_state{};
    BufferFileEditState *cubemap00_file_edit_state{};
    BufferFileEditState *image_file_edit_state{};

    ~BufferPanel();

    void load(Rml::Context *rml_context, Rml::ElementDocument *document);
    void process_event(Rml::Event &event, std::string const &value);
    void cleanup();
    void update();

    void load_shadertoy_json(nlohmann::json const &temp_json);
    void reload_json();
    [[nodiscard]] auto get_shadertoy_json() const -> auto const & {
        return json;
    }

  private:
    void buffer_panel_ichannel_settings(Rml::Event &event);
    void buffer_panel_bpiw_close();
    void buffer_panel_change_filter(Rml::Event &event, Rml::ElementText *tab_div_content);
    void buffer_panel_change_wrap(Rml::Event &event, Rml::ElementText *tab_div_content);
    void buffer_panel_bpiw_select(Rml::Event &event, Rml::ElementText *tab_div_content);
    void buffer_panel_ichannel_close(Rml::Event &event, Rml::ElementText *tab_div_content);
    void buffer_panel_add(Rml::Event &event);
    void buffer_panel_add_option(Rml::Event &event);
    void buffer_panel_tab_close(Rml::Event &event);
    void buffer_panel_tab_edit(Rml::Event &event);
};
