#pragma once

#include <core/core.inl>

template <typename T>
struct TaskStatePushConstantSize {
    static inline constexpr size_t SIZE = sizeof(T);
};

template <>
struct TaskStatePushConstantSize<void> {
    static inline constexpr size_t SIZE = 0;
};

template <typename TaskImplT>
struct TaskStateTemplate {
    daxa::PipelineManager &pipeline_manager;
    std::shared_ptr<daxa::ComputePipeline> pipeline{};
    using Self = TaskImplT::Self;
    using PushConstant = TaskImplT::PushConstant;

    void compile_pipeline() {
        auto defines = TaskImplT::get_defines();
        auto compile_result = pipeline_manager.add_compute_pipeline({
            .shader_info = {
                .source = daxa::ShaderFile{TaskImplT::SHADER_FILE},
                .compile_options = {.defines = defines},
            },
            .push_constant_size = TaskStatePushConstantSize<PushConstant>::SIZE,
            .name = TaskImplT::name,
        });
        if (compile_result.is_err() || !compile_result.value()->is_valid()) {
            std::cerr << compile_result.message() << std::endl;
            return;
        }
        pipeline = compile_result.value();
    }

    TaskStateTemplate(daxa::PipelineManager &a_pipeline_manager)
        : pipeline_manager{a_pipeline_manager} {
        compile_pipeline();
    }

    void record_commands(daxa::TaskInterface const &ti, daxa::CommandRecorder &recorder, Self &self) {
        if (!pipeline || !*pipeline || !pipeline->is_valid()) {
            return;
        }
        recorder.set_pipeline(*pipeline);
        TaskImplT::dispatch(ti, recorder, self);
    }
};

template <typename TaskImplT>
struct TaskTemplate : TaskImplT::Uses {
    TaskStateTemplate<TaskImplT> *state;
    using Self = TaskImplT::Self;
    Self self;
    void callback(daxa::TaskInterface const &ti) {
        auto &recorder = ti.get_recorder();
        recorder.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        state->record_commands(ti, recorder, self);
    }
};
