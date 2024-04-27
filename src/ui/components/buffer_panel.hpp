#pragma once

#include <string>

namespace Rml {
    class Context;
    class ElementDocument;
    class Event;
} // namespace Rml

struct BufferPanel {
    void load(Rml::Context *rml_context, Rml::ElementDocument *document);
    void process_event(Rml::Event &event, std::string const &value);
};
