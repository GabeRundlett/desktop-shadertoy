#pragma once

#include <app/viewport.inl>
#include <app/ping_pong_resource.hpp>

#include <daxa/daxa.hpp>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/utils/task_graph.hpp>
#include <nlohmann/json.hpp>

enum struct ShaderPassInputType {
    NONE,
    BUFFER,
    CUBE,
    KEYBOARD,
    TEXTURE,
    CUBE_TEXTURE,
    VOLUME_TEXTURE,
};

struct KeyboardInput {
    std::array<int8_t, 256> current_state{};
    std::array<int8_t, 256> keypress{};
    std::array<int8_t, 256> toggles{};
};

struct ShaderPassInput {
    ShaderPassInputType type{};
    size_t index{};
    uint32_t channel{};
    daxa::SamplerId sampler{};
};

struct ShaderBufferPass {
    std::string name;
    std::vector<ShaderPassInput> inputs;
    std::shared_ptr<daxa::RasterPipeline> pipeline;
    PingPongImage buffer;
    daxa::TaskImageView recording_buffer_view;
    bool needs_mipmap{};
};

struct ShaderCubePass {
    std::string name;
    std::vector<ShaderPassInput> inputs;
    std::shared_ptr<daxa::RasterPipeline> pipeline;
    PingPongImage buffer;
    daxa::TaskImageView recording_buffer_view;
    bool needs_mipmap{};
};

struct Viewport {
    daxa::Device daxa_device;
    daxa::PipelineManager pipeline_manager;

    std::vector<ShaderBufferPass> buffer_passes{};
    std::vector<ShaderCubePass> cube_passes{};
    ShaderBufferPass image_pass{};
    std::array<daxa::SamplerId, 6> samplers{};
    std::unordered_map<std::string, std::pair<daxa::ImageId, size_t>> loaded_textures{};
    std::vector<daxa::TaskImage> task_textures{};
    GpuInput gpu_input{};

    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point start{};
    Clock::time_point prev_time{};
    Clock::time_point pause_time{};
    Clock::time_point prev_fps_time{};

    size_t fps_count{};
    float last_known_fps{};
    bool mouse_enabled{};
    bool is_reset{};
    KeyboardInput keyboard_input{};
    daxa_f32vec2 mouse_pos{};

    bool first_record_after_load{};
    bool load_failed{};

    explicit Viewport(daxa::Device a_daxa_device);
    ~Viewport();

    Viewport(const Viewport &) = delete;
    Viewport(Viewport &&) = delete;
    auto operator=(const Viewport &) -> Viewport & = delete;
    auto operator=(Viewport &&) -> Viewport & = delete;

    void update();
    void render();
    auto record(daxa::TaskGraph &task_graph) -> daxa::TaskImageView;
    void reset();

    void on_mouse_move(float px, float py);
    void on_mouse_button(int32_t button_id, int32_t action);
    void on_key(int32_t key_id, int32_t action);
    void on_toggle_pause(bool is_paused);

    auto load_texture(std::string path) -> std::pair<daxa::ImageId, size_t>;
    auto load_cube_texture(std::string path) -> std::pair<daxa::ImageId, size_t>;
    auto load_volume_texture(std::string id) -> std::pair<daxa::ImageId, size_t>;
    void load_shadertoy_json(nlohmann::json json);
};
