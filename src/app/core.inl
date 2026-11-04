#pragma once

#include <daxa/utils/task_graph.inl>

#if defined(__cplusplus)
#include <memory>
#include <daxa/utils/task_graph.hpp>
#include <daxa/utils/pipeline_manager.hpp>
namespace core {
    void log_error(std::string const &msg);
    // When set, log_error appends to this string instead of logging to the UI (used by the batch tester).
    extern std::string *error_sink;
}
#endif
