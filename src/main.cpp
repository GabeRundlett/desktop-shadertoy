#include <daxa/daxa.hpp>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/utils/task_graph.hpp>

#include <filesystem>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <ui/app_ui.hpp>
#include <core/ping_pong_resource.hpp>

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cstdlib>

#include <stb_image.h>

#include <main.inl>

#define MAX_MIP 9

enum struct ShaderPassInputType {
    BUFFER,
    CUBE,
    KEYBOARD,
    TEXTURE,
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
    // daxa::ImageId image_id{};
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

struct MipMapTask {
    struct Uses {
        daxa::ImageTransferRead<> lower_mip{};
        daxa::ImageTransferWrite<> higher_mip{};
    } uses = {};
    std::string name = "mip map";
    uint32_t mip = {};
    void callback(daxa::TaskInterface ti) {
        auto &recorder = ti.get_recorder();
        auto do_blit = [&](daxa::ImageTransferRead<> &lower_mip, daxa::ImageTransferWrite<> &higher_mip) {
            auto image_size = ti.get_device().info_image(lower_mip.image()).value().size;
            auto mip_size = std::array<int32_t, 3>{
                std::max<int32_t>(1, static_cast<int32_t>(image_size.x / (1u << mip))),
                std::max<int32_t>(1, static_cast<int32_t>(image_size.y / (1u << mip))),
                std::max<int32_t>(1, static_cast<int32_t>(image_size.z / (1u << mip))),
            };
            auto next_mip_size = std::array<int32_t, 3>{
                std::max<int32_t>(1, static_cast<int32_t>(image_size.x / (2u << mip))),
                std::max<int32_t>(1, static_cast<int32_t>(image_size.y / (2u << mip))),
                std::max<int32_t>(1, static_cast<int32_t>(image_size.z / (2u << mip))),
            };
            recorder.blit_image_to_image({
                .src_image = lower_mip.image(),
                .dst_image = higher_mip.image(),
                .src_slice = {
                    .mip_level = mip,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .src_offsets = {{{0, 0, 0}, {mip_size[0], mip_size[1], mip_size[2]}}},
                .dst_slice = {
                    .mip_level = mip + 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .dst_offsets = {{{0, 0, 0}, {next_mip_size[0], next_mip_size[1], next_mip_size[2]}}},
                .filter = daxa::Filter::LINEAR,
            });
        };
        do_blit(uses.lower_mip, uses.higher_mip);
    }
};

void GpuInputUploadTransferTask_record(daxa::Device &device, daxa::CommandRecorder &cmd_list, daxa::BufferId input_buffer, GpuInput &gpu_input) {
    auto staging_input_buffer = device.create_buffer({
        .size = sizeof(GpuInput),
        .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .name = "staging_input_buffer",
    });
    cmd_list.destroy_buffer_deferred(staging_input_buffer);
    auto *buffer_ptr = device.get_host_address_as<GpuInput>(staging_input_buffer).value();
    *buffer_ptr = gpu_input;
    cmd_list.copy_buffer_to_buffer({
        .src_buffer = staging_input_buffer,
        .dst_buffer = input_buffer,
        .size = sizeof(GpuInput),
    });
}

void ShaderToyTask_record(std::shared_ptr<daxa::RasterPipeline> const &pipeline, daxa::CommandRecorder &cmd_list, BDA input_buffer_ptr, InputImages const &images, daxa::ImageId render_image, daxa_u32vec2 size) {
    if (!pipeline) {
        return;
    }
    auto renderpass = std::move(cmd_list).begin_renderpass({
        .color_attachments = {{.image_view = render_image.default_view(), .load_op = daxa::AttachmentLoadOp::LOAD, .clear_value = std::array<daxa_f32, 4>{0.5f, 0.5f, 0.5f, 1.0f}}},
        .render_area = {.x = 0, .y = 0, .width = size.x, .height = size.y},
    });
    renderpass.set_pipeline(*pipeline);
    renderpass.push_constant(ShaderToyPush{
        .gpu_input = input_buffer_ptr,
        .input_images = images,
    });
    renderpass.draw({.vertex_count = 3});
    cmd_list = std::move(renderpass).end_renderpass();
}

void ShaderToyCubeTask_record(std::shared_ptr<daxa::RasterPipeline> const &pipeline, daxa::CommandRecorder &cmd_list, BDA input_buffer_ptr, InputImages const &images, daxa::ImageViewId cube_face, daxa_u32 i) {
    if (!pipeline) {
        return;
    }
    auto renderpass = std::move(cmd_list).begin_renderpass({
        .color_attachments = {{.image_view = cube_face, .load_op = daxa::AttachmentLoadOp::LOAD, .clear_value = std::array<daxa_f32, 4>{0.5f, 0.5f, 0.5f, 1.0f}}},
        .render_area = {.x = 0, .y = 0, .width = 1024, .height = 1024},
    });
    renderpass.set_pipeline(*pipeline);
    renderpass.push_constant(ShaderToyPush{
        .gpu_input = input_buffer_ptr,
        .input_images = images,
        .face_index = i,
    });
    renderpass.draw({.vertex_count = 3});
    cmd_list = std::move(renderpass).end_renderpass();
}

void replace_all(std::string &s, std::string const &toReplace, std::string const &replaceWith) {
    std::string buf;
    std::size_t pos = 0;
    std::size_t prevPos{};

    // Reserves rough estimate of final size of string.
    buf.reserve(s.size());

    while (true) {
        prevPos = pos;
        pos = s.find(toReplace, pos);
        if (pos == std::string::npos)
            break;
        buf.append(s, prevPos, pos - prevPos);
        buf += replaceWith;
        pos += toReplace.size();
    }

    buf.append(s, prevPos, s.size() - prevPos);
    s.swap(buf);
}

void preprocess_shadertoy_code(std::string &code) {
    replace_all(code, "\\n", "\n");
    replace_all(code, "sampler2D", "CombinedImageSampler2D");
    replace_all(code, "samplerCube", "CombinedImageSamplerCube");
    replace_all(code, "textureCube", "TextureCube");
}

enum struct ShaderToyFilter {
    NEAREST,
    LINEAR,
    MIPMAP,
};
enum struct ShaderToyWrap {
    CLAMP,
    REPEAT,
};

struct ShaderApp {
    daxa::Instance daxa_instance;
    daxa::Device daxa_device;
    daxa::PipelineManager pipeline_manager;
    AppUi ui;
    daxa::TaskGraph main_task_graph;
    daxa::TaskImage task_swapchain_image;

    std::vector<ShaderBufferPass> buffer_passes{};
    std::vector<ShaderCubePass> cube_passes{};
    ShaderBufferPass image_pass{};
    std::array<daxa::SamplerId, 6> samplers{};

    using Clock = std::chrono::high_resolution_clock;
    GpuInput gpu_input{};
    Clock::time_point start{};
    Clock::time_point prev_time{};
    bool mouse_enabled{};
    KeyboardInput keyboard_input{};
    daxa_f32vec2 mouse_pos{};
    std::unordered_map<std::string, std::pair<daxa::ImageId, size_t>> loaded_textures{};
    std::vector<daxa::TaskImage> task_textures{};

    ShaderApp();
    ~ShaderApp();

    ShaderApp(const ShaderApp &) = delete;
    ShaderApp(ShaderApp &&) = delete;
    auto operator=(const ShaderApp &) -> ShaderApp & = delete;
    auto operator=(ShaderApp &&) -> ShaderApp & = delete;

    void update();
    auto should_close() -> bool;
    void reset_input();
    void render();
    auto load_texture(std::string path) -> std::pair<daxa::ImageId, size_t>;
    void load_shadertoy_json(std::filesystem::path const &path);
    auto record_main_task_graph() -> daxa::TaskGraph;
};

auto main() -> int {
    auto app = ShaderApp();
    while (true) {
        app.update();
        if (app.should_close()) {
            break;
        }
        app.render();
    }
}

using namespace std::chrono_literals;

ShaderApp::ShaderApp()
    : daxa_instance{daxa::create_instance({})},
      daxa_device{daxa_instance.create_device({.name = "device"})},
      pipeline_manager{[this]() {
          auto result = daxa::PipelineManager({
              .device = daxa_device,
              .shader_compile_options = {
                  .root_paths = {DAXA_SHADER_INCLUDE_DIR, "src"},
                  .language = daxa::ShaderLanguage::GLSL,
                  .enable_debug_info = true,
              },
              .register_null_pipelines_when_first_compile_fails = true,
              .name = "pipeline_manager",
          });
          return result;
      }()},
      ui{daxa_device},
      task_swapchain_image{daxa::TaskImageInfo{.swapchain_image = true}} {

    ui.app_windows[0].on_resize = [&]() {
        main_task_graph = record_main_task_graph();
        reset_input();
        render();
    };
    ui.app_windows[0].on_drop = [&](std::span<char const *> paths) {
        load_shadertoy_json(paths[0]);
        main_task_graph = record_main_task_graph();
    };
    ui.app_windows[0].on_mouse_move = [&](float px, float py) {
        if (mouse_enabled) {
            gpu_input.Mouse.x = px;
            gpu_input.Mouse.y = py;
        }
    };
    ui.app_windows[0].on_mouse_button = [&](int32_t button_id, int32_t action) {
        if (button_id == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            gpu_input.Mouse.x = mouse_pos.x;
            gpu_input.Mouse.y = mouse_pos.y;
            gpu_input.Mouse.z = mouse_pos.x;
            gpu_input.Mouse.w = mouse_pos.y;
            mouse_enabled = true;
        } else if (action == GLFW_RELEASE) {
            mouse_enabled = false;
            gpu_input.Mouse.z = -1;
            gpu_input.Mouse.w = -1;
        }
    };
    ui.app_windows[0].on_key = [&](int32_t key_id, int32_t action) {
        int32_t transformed_key_id = key_id;

        switch (key_id) {
        case GLFW_KEY_LEFT:
            transformed_key_id = 37;
            break;
        case GLFW_KEY_UP:
            transformed_key_id = 38;
            break;
        case GLFW_KEY_RIGHT:
            transformed_key_id = 39;
            break;
        case GLFW_KEY_DOWN:
            transformed_key_id = 40;
            break;
            // case GLFW_KEY_F11:
            //     if (action == GLFW_PRESS) {
            //         toggle_fullscreen();
            //     }
            //     break;
        }

        if (transformed_key_id < 0 || transformed_key_id >= 256) {
            return;
        }

        keyboard_input.current_state[static_cast<size_t>(transformed_key_id)] = static_cast<int8_t>(action != GLFW_RELEASE);
        if (action == GLFW_PRESS) {
            keyboard_input.keypress[static_cast<size_t>(transformed_key_id)] = 1;
            keyboard_input.toggles[static_cast<size_t>(transformed_key_id)] = 1 - keyboard_input.toggles[static_cast<size_t>(transformed_key_id)];
        }
    };

    load_shadertoy_json("shader.json");
    main_task_graph = record_main_task_graph();

    reset_input();

    samplers[static_cast<size_t>(ShaderToyFilter::NEAREST) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3] = daxa_device.create_sampler({
        .magnification_filter = daxa::Filter::NEAREST,
        .minification_filter = daxa::Filter::NEAREST,
        .min_lod = 0,
        .max_lod = 0,
    });
    samplers[static_cast<size_t>(ShaderToyFilter::LINEAR) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3] = daxa_device.create_sampler({
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .min_lod = 0,
        .max_lod = 0,
    });
    samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3] = daxa_device.create_sampler({
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .min_lod = 0,
        .max_lod = MAX_MIP - 1,
    });
    samplers[static_cast<size_t>(ShaderToyFilter::NEAREST) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3] = daxa_device.create_sampler({
        .magnification_filter = daxa::Filter::NEAREST,
        .minification_filter = daxa::Filter::NEAREST,
        .address_mode_u = daxa::SamplerAddressMode::REPEAT,
        .address_mode_v = daxa::SamplerAddressMode::REPEAT,
        .address_mode_w = daxa::SamplerAddressMode::REPEAT,
        .min_lod = 0,
        .max_lod = 0,
    });
    samplers[static_cast<size_t>(ShaderToyFilter::LINEAR) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3] = daxa_device.create_sampler({
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .address_mode_u = daxa::SamplerAddressMode::REPEAT,
        .address_mode_v = daxa::SamplerAddressMode::REPEAT,
        .address_mode_w = daxa::SamplerAddressMode::REPEAT,
        .min_lod = 0,
        .max_lod = 0,
    });
    samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3] = daxa_device.create_sampler({
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .address_mode_u = daxa::SamplerAddressMode::REPEAT,
        .address_mode_v = daxa::SamplerAddressMode::REPEAT,
        .address_mode_w = daxa::SamplerAddressMode::REPEAT,
        .min_lod = 0,
        .max_lod = MAX_MIP - 1,
    });
}

ShaderApp::~ShaderApp() {
    ui.app_windows.clear();
    daxa_device.wait_idle();
    daxa_device.collect_garbage();
    for (auto &sampler : samplers) {
        daxa_device.destroy_sampler(sampler);
    }
    for (auto &[str, elem] : loaded_textures) {
        auto &[image_id, idx] = elem;
        daxa_device.destroy_image(image_id);
    }
}

void ShaderApp::update() {
    ui.update();

    auto now = Clock::now();
    gpu_input.Time = std::chrono::duration<daxa_f32>(now - start).count();
    gpu_input.TimeDelta = std::max(std::numeric_limits<float>::min(), std::chrono::duration<daxa_f32>(now - prev_time).count());
    gpu_input.FrameRate = 1.0f / gpu_input.TimeDelta;
    prev_time = now;
}

void ShaderApp::reset_input() {
    gpu_input.Frame = 0;
    auto now = Clock::now();
    start = now;
    gpu_input.Time = std::chrono::duration<daxa_f32>(now - start).count();
    gpu_input.TimeDelta = std::max(std::numeric_limits<float>::min(), std::chrono::duration<daxa_f32>(now - prev_time).count());
    gpu_input.FrameRate = 1.0f / gpu_input.TimeDelta;
    prev_time = now;
    gpu_input.Mouse = {0.0f, 0.0f, -1.0f, -1.0f};
}

void ShaderApp::render() {
    gpu_input.Resolution = daxa_f32vec3{
        static_cast<daxa_f32>(ui.app_windows[0].size.x),
        static_cast<daxa_f32>(ui.app_windows[0].size.y),
        1.0f,
    };

    gpu_input.ChannelResolution[0] = daxa_f32vec3{
        static_cast<daxa_f32>(ui.app_windows[0].size.x),
        static_cast<daxa_f32>(ui.app_windows[0].size.y),
        1.0f,
    };

    auto &app_window = ui.app_windows[0];
    auto const swapchain_image = app_window.swapchain.acquire_next_image();
    if (swapchain_image.is_empty()) {
        return;
    }
    task_swapchain_image.set_images({.images = {&swapchain_image, 1}});

    for (auto &pass : buffer_passes) {
        pass.buffer.swap();
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
    }
    for (auto &pass : cube_passes) {
        pass.buffer.swap();
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
    }

    main_task_graph.execute({});
    daxa_device.collect_garbage();
    ++gpu_input.Frame;
}

auto ShaderApp::should_close() -> bool {
    return ui.should_close.load();
}

auto ShaderApp::load_texture(std::string path) -> std::pair<daxa::ImageId, size_t> {
    auto task_image_index = task_textures.size();
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t channel_n = 0;
    auto task_image = daxa::TaskImage({.name = path});
    replace_all(path, "/media/a/", "media/");
    stbi_set_flip_vertically_on_load(1);
    auto *temp_data = stbi_load(path.c_str(), &size_x, &size_y, &channel_n, 4);
    auto image_id = daxa_device.create_image({
        .dimensions = 2,
        .format = daxa::Format::R8G8B8A8_UNORM,
        .size = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = "user_image",
    });
    task_image.set_images({.images = std::array{image_id}});

    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    temp_task_graph.use_persistent_image(task_image);
    temp_task_graph.add_task({
        .uses = {
            daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_WRITE>{task_image},
        },
        .task = [this, temp_data, size_x, size_y, image_id](daxa::TaskInterface task_runtime) {
            auto staging_buffer = daxa_device.create_buffer({
                .size = static_cast<uint32_t>(size_x * size_y * 4),
                .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                .name = "staging_buffer",
            });
            auto buffer_ptr = daxa_device.get_host_address_as<uint8_t>(staging_buffer).value();
            memcpy(buffer_ptr, temp_data, size_x * size_y * 4);
            auto &cmd_list = task_runtime.get_recorder();
            cmd_list.pipeline_barrier({
                .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            });
            cmd_list.destroy_buffer_deferred(staging_buffer);
            cmd_list.copy_buffer_to_image({
                .buffer = staging_buffer,
                .image = image_id,
                .image_extent = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
            });
        },
        .name = "upload_user_texture",
    });
    temp_task_graph.submit({});
    temp_task_graph.complete({});
    temp_task_graph.execute({});
    task_textures.push_back(task_image);
    stbi_image_free(temp_data);
    return std::pair<daxa::ImageId, size_t>{image_id, task_image_index};
}

void ShaderApp::load_shadertoy_json(std::filesystem::path const &path) {
    auto json = nlohmann::json::parse(std::ifstream(path));

    auto &renderpasses = json["renderpass"];

    auto id_map = std::unordered_map<std::string, ShaderPassInput>{};

    auto buffer_pass_n = size_t{};
    auto cube_pass_n = size_t{};
    auto common_code = std::string{"#pragma once\n"};
    for (auto &renderpass : renderpasses) {
        auto &type = renderpass["type"];
        auto &outputs = renderpass["outputs"];
        auto id = std::string{};
        for (auto &output : outputs) {
            id = output["id"];
        }
        if (type == "common") {
            common_code += renderpass["code"];
        } else if (type == "buffer") {
            id_map[id] = {.type = ShaderPassInputType::BUFFER, .index = buffer_pass_n};
            ++buffer_pass_n;
        } else if (type == "cubemap") {
            id_map[id] = {.type = ShaderPassInputType::CUBE, .index = cube_pass_n};
            ++cube_pass_n;
        }
    }
    buffer_passes.clear();
    cube_passes.clear();
    buffer_passes.reserve(buffer_pass_n);
    cube_passes.reserve(cube_pass_n);
    preprocess_shadertoy_code(common_code);
    pipeline_manager.add_virtual_file(daxa::VirtualFileInfo{
        .name = "common",
        .contents = common_code,
    });

    for (auto &renderpass : renderpasses) {
        auto &code = renderpass["code"];
        auto &description = renderpass["description"];
        auto &inputs = renderpass["inputs"];
        auto &name = renderpass["name"];
        auto &outputs = renderpass["outputs"];
        auto &type = renderpass["type"];

        if (type != "image" && type != "buffer" && type != "cubemap") {
            continue;
        }

        auto temp_inputs = std::vector<ShaderPassInput>{};

        pipeline_manager.add_virtual_file({
            .name = "iChannel0_decl",
            .contents = "#define iChannel0 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[0], daxa_push_constant.input_images.Channel_sampler[0])",
        });
        pipeline_manager.add_virtual_file({
            .name = "iChannel1_decl",
            .contents = "#define iChannel1 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[1], daxa_push_constant.input_images.Channel_sampler[1])",
        });
        pipeline_manager.add_virtual_file({
            .name = "iChannel2_decl",
            .contents = "#define iChannel2 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[2], daxa_push_constant.input_images.Channel_sampler[2])",
        });
        pipeline_manager.add_virtual_file({
            .name = "iChannel3_decl",
            .contents = "#define iChannel3 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[3], daxa_push_constant.input_images.Channel_sampler[3])",
        });

        auto type_json_to_glsl_image_type = [](nlohmann::json &json) -> std::string {
            if (json == "buffer" || json == "keyboard" || json == "texture") {
                return "CombinedImageSampler2D";
            } else if (json == "cubemap") {
                return "CombinedImageSamplerCube";
            }
            return "void"; // L
        };

        auto get_sampler = [](auto const &samplers, nlohmann::json &input) {
            auto filter_str = std::string{input["sampler"]["filter"]};
            auto wrap_str = std::string{input["sampler"]["wrap"]};

            auto filter = ShaderToyFilter{};
            if (filter_str == "nearest") {
                filter = ShaderToyFilter::NEAREST;
            } else if (filter_str == "linear") {
                filter = ShaderToyFilter::LINEAR;
            } else if (filter_str == "mipmap") {
                filter = ShaderToyFilter::MIPMAP;
            }

            auto wrap = ShaderToyWrap{};
            if (wrap_str == "clamp") {
                wrap = ShaderToyWrap::CLAMP;
            } else if (wrap_str == "repeat") {
                wrap = ShaderToyWrap::REPEAT;
            }

            return samplers[static_cast<size_t>(filter) + static_cast<size_t>(wrap) * 3];
        };

        for (auto &input : inputs) {
            auto &type = input["type"];

            auto channel_str = std::to_string(uint32_t{input["channel"]});
            auto image_type = type_json_to_glsl_image_type(type);

            if (type == "buffer" || type == "cubemap") {
                auto input_copy = id_map[input["id"]];
                input_copy.channel = input["channel"];
                input_copy.sampler = get_sampler(samplers, input);
                temp_inputs.push_back(input_copy);
            } else if (type == "keyboard") {
                temp_inputs.push_back({
                    .type = ShaderPassInputType::KEYBOARD,
                    .channel = input["channel"],
                    .sampler = get_sampler(samplers, input),
                });
            } else if (type == "texture") {
                auto input_copy = ShaderPassInput{
                    .type = ShaderPassInputType::TEXTURE,
                    .channel = input["channel"],
                    .sampler = get_sampler(samplers, input),
                };
                auto path = std::string{input["filepath"]};
                if (loaded_textures.contains(path)) {
                    auto &[loaded_texture, task_image_index] = loaded_textures.at(path);
                    // input_copy.image_id = loaded_texture;
                    input_copy.index = task_image_index;
                } else {
                    auto loaded_result = load_texture(path);
                    loaded_textures[path] = loaded_result;
                    auto &[loaded_texture, task_image_index] = loaded_result;
                    // input_copy.image_id = loaded_texture;
                    input_copy.index = task_image_index;
                }
                temp_inputs.push_back(input_copy);
            }
            pipeline_manager.add_virtual_file({
                .name = std::string{"iChannel"} + channel_str + "_decl",
                .contents = std::string{"#define iChannel"} + channel_str + " " + image_type + "(daxa_push_constant.input_images.Channel[" + channel_str + "], daxa_push_constant.input_images.Channel_sampler[" + channel_str + "])",
            });
        }

        auto user_code = daxa::VirtualFileInfo{
            .name = "user_code",
            .contents = code,
        };
        preprocess_shadertoy_code(user_code.contents);
        pipeline_manager.add_virtual_file(user_code);

        auto extra_defines = std::vector<daxa::ShaderDefine>{};
        auto pass_format = daxa::Format::R32G32B32A32_SFLOAT;
        if (type == "image") {
            pass_format = daxa::Format::R16G16B16A16_SFLOAT;
            // pass_format = ui.app_windows[0].swapchain.get_format();
            extra_defines.push_back({.name = "MAIN_IMAGE", .value = "1"});
        } else if (type == "cubemap") {
            pass_format = daxa::Format::R16G16B16A16_SFLOAT;
            extra_defines.push_back({.name = "CUBEMAP", .value = "1"});
        }

        auto compile_result = pipeline_manager.add_raster_pipeline({
            .vertex_shader_info = daxa::ShaderCompileInfo{.source = daxa::ShaderFile{"main.glsl"}, .compile_options{.defines = extra_defines}},
            .fragment_shader_info = daxa::ShaderCompileInfo{.source = daxa::ShaderFile{"main.glsl"}, .compile_options{.defines = extra_defines}},
            .color_attachments = {{
                .format = pass_format,
            }},
            .push_constant_size = sizeof(ShaderToyPush),
            .name = std::string{type} + " pass " + std::string{name},
        });
        if (compile_result.is_err() || !compile_result.value()->is_valid()) {
            std::cerr << user_code.contents << std::endl;
            std::cerr << compile_result.message() << std::endl;
            return;
        }

        if (type == "image") {
            image_pass = {name, temp_inputs, compile_result.value()};
        } else if (type == "buffer") {
            buffer_passes.emplace_back(name, temp_inputs, compile_result.value());
        } else if (type == "cubemap") {
            cube_passes.emplace_back(name, temp_inputs, compile_result.value());
        }
    }

    reset_input();
}

auto ShaderApp::record_main_task_graph() -> daxa::TaskGraph {
    auto &app_window = ui.app_windows[0];
    auto task_graph = daxa::TaskGraph(daxa::TaskGraphInfo{
        .device = daxa_device,
        .swapchain = app_window.swapchain,
        .name = "main_tg",
    });
    task_graph.use_persistent_image(task_swapchain_image);
    for (auto const &task_texture : task_textures) {
        task_graph.use_persistent_image(task_texture);
    }
    task_graph.add_task({
        .uses = {
            daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D>{task_swapchain_image},
        },
        .task = [this](daxa::TaskInterface const &ti) {
            auto &recorder = ti.get_recorder();
            auto swapchain_image = ti.uses[task_swapchain_image].image();
            auto swapchain_image_full_slice = daxa_device.info_image_view(swapchain_image.default_view()).value().slice;
            recorder.clear_image({
                .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .clear_value = std::array<daxa::f32, 4>{0.2f, 0.1f, 0.4f, 1.0f},
                .dst_image = swapchain_image,
                .dst_slice = swapchain_image_full_slice,
            });
        },
        .name = "clear screen",
    });
    auto viewport_render_image = task_graph.create_transient_image({
        .format = daxa::Format::R16G16B16A16_SFLOAT,
        .size = {static_cast<uint32_t>(app_window.size.x), static_cast<uint32_t>(app_window.size.y), 1},
        .name = "viewport_render_image",
    });
    auto task_input_buffer = task_graph.create_transient_buffer({
        .size = static_cast<uint32_t>(sizeof(GpuInput)),
        .name = "input_buffer",
    });
    auto task_keyboard_image = task_graph.create_transient_image({
        .format = daxa::Format::R8_SINT,
        .size = {256, 3, 1},
        .name = "keyboard_image",
    });

    // GpuInputUploadTransferTask
    task_graph.add_task({
        .uses = {
            daxa::TaskBufferUse<daxa::TaskBufferAccess::TRANSFER_WRITE>{task_input_buffer},
        },
        .task = [this, task_input_buffer](daxa::TaskInterface const &ti) {
            auto &cmd_list = ti.get_recorder();
            GpuInputUploadTransferTask_record(daxa_device, cmd_list, ti.uses[task_input_buffer].buffer(), gpu_input);
        },
        .name = "GpuInputUploadTransferTask",
    });

    // KeyboardInputUploadTask
    task_graph.add_task({
        .uses = {
            daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D>{task_keyboard_image},
        },
        .task = [this, task_keyboard_image](daxa::TaskInterface const &ti) {
            auto &cmd_list = ti.get_recorder();
            auto staging_buffer = daxa_device.create_buffer({
                .size = sizeof(KeyboardInput),
                .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                .name = "staging_buffer",
            });
            cmd_list.destroy_buffer_deferred(staging_buffer);
            auto *buffer_ptr = daxa_device.get_host_address_as<KeyboardInput>(staging_buffer).value();
            *buffer_ptr = keyboard_input;
            cmd_list.copy_buffer_to_image({
                .buffer = staging_buffer,
                .image = ti.uses[task_keyboard_image].image(),
                .image_extent = {256, 3, 1},
            });
        },
        .name = "KeyboardInputUploadTask",
    });

    auto get_resource_view = [this, task_keyboard_image](ShaderPassInput const &input) -> daxa::TaskImageView {
        switch (input.type) {
        case ShaderPassInputType::BUFFER: return buffer_passes[input.index].recording_buffer_view;
        case ShaderPassInputType::CUBE: return cube_passes[input.index].recording_buffer_view;
        case ShaderPassInputType::KEYBOARD: return task_keyboard_image;
        case ShaderPassInputType::TEXTURE: return task_textures[input.index];
        }
    };

    auto get_resource_view_slice = [get_resource_view](ShaderPassInput const &input) -> daxa::TaskImageView {
        auto view = get_resource_view(input);
        switch (input.type) {
        case ShaderPassInputType::BUFFER: return view.view({.level_count = MAX_MIP});
        case ShaderPassInputType::CUBE: return view.view({.level_count = 1, .layer_count = 6});
        case ShaderPassInputType::KEYBOARD: return view;
        case ShaderPassInputType::TEXTURE: return view;
        }
    };

    for (auto &pass : buffer_passes) {
        pass.buffer = {};
        pass.buffer.get(
            daxa_device,
            daxa::ImageInfo{
                .format = daxa::Format::R32G32B32A32_SFLOAT,
                .size = {static_cast<uint32_t>(ui.app_windows[0].size.x), static_cast<uint32_t>(ui.app_windows[0].size.y), 1},
                .mip_level_count = MAX_MIP,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
                .name = std::string{"buffer "} + std::string{pass.name},
            });
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
        task_graph.use_persistent_image(pass.buffer.task_resources.output_resource);
        task_graph.use_persistent_image(pass.buffer.task_resources.history_resource);
    }

    for (auto &pass : cube_passes) {
        pass.buffer = {};
        pass.buffer.get(
            daxa_device,
            daxa::ImageInfo{
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .size = {1024, 1024, 1},
                .mip_level_count = 1,
                .array_layer_count = 6,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
                .name = std::string{"cube buffer "} + std::string{pass.name},
            });
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;

        task_graph.use_persistent_image(pass.buffer.task_resources.output_resource);
        task_graph.use_persistent_image(pass.buffer.task_resources.history_resource);
    }

    for (auto &pass : buffer_passes) {
        auto uses = std::vector<daxa::GenericTaskResourceUse>{};
        uses.push_back(daxa::TaskBufferUse<daxa::TaskBufferAccess::FRAGMENT_SHADER_READ>{task_input_buffer});
        for (auto const &input : pass.inputs) {
            if (input.type == ShaderPassInputType::CUBE) {
                uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::CUBE>{get_resource_view_slice(input)});
            } else {
                uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_2D>{get_resource_view_slice(input)});
            }
        }
        auto pipeline = pass.pipeline;
        auto inputs = pass.inputs;
        auto output_view = pass.buffer.task_resources.output_resource.view();
        uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D>{output_view});
        task_graph.add_task({
            .uses = uses,
            .task = [this, &pass, task_input_buffer, get_resource_view_slice, pipeline, inputs, output_view](daxa::TaskInterface const &ti) {
                auto &cmd_list = ti.get_recorder();
                auto input_images = InputImages{};
                auto size = ti.get_device().info_image(ti.uses[output_view].image()).value().size;
                for (auto const &input : pass.inputs) {
                    input_images.Channel[input.channel] = ti.uses[get_resource_view_slice(input)].view();
                    input_images.Channel_sampler[input.channel] = input.sampler;
                }
                ShaderToyTask_record(
                    pipeline,
                    cmd_list,
                    daxa_device.get_device_address(ti.uses[task_input_buffer].buffer()).value(),
                    input_images,
                    ti.uses[output_view].image(),
                    daxa_u32vec2{size.x, size.y});
                pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
            },
            .name = std::string("buffer task ") + pass.name,
        });
        pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
        if (pass.needs_mipmap) {
            for (uint32_t i = 0; i < MAX_MIP - 1; ++i) {
                task_graph.add_task(MipMapTask{
                    .uses = {
                        .lower_mip = output_view.view({.base_mip_level = i}),
                        .higher_mip = output_view.view({.base_mip_level = i + 1}),
                    },
                    .name = std::string("mip map ") + std::to_string(i),
                    .mip = i,
                });
            }
        }
    }

    for (auto &pass : cube_passes) {
        auto uses = std::vector<daxa::GenericTaskResourceUse>{};
        uses.push_back(daxa::TaskBufferUse<daxa::TaskBufferAccess::FRAGMENT_SHADER_READ>{task_input_buffer});
        for (auto const &input : pass.inputs) {
            if (input.type == ShaderPassInputType::CUBE) {
                uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::CUBE>{get_resource_view_slice(input)});
            } else {
                uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_2D>{get_resource_view_slice(input)});
            }
        }
        auto face_views = std::array<daxa::TaskImageView, 6>{};
        for (uint32_t i = 0; i < 6; ++i) {
            face_views[i] = pass.buffer.task_resources.output_resource.view().view({.base_array_layer = i});
            uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D>{face_views[i]});
        }
        auto pipeline = pass.pipeline;
        auto inputs = pass.inputs;
        auto output_view = pass.buffer.task_resources.output_resource.view();
        task_graph.add_task({
            .uses = uses,
            .task = [this, &pass, task_input_buffer, get_resource_view_slice, pipeline, inputs, output_view, face_views](daxa::TaskInterface const &ti) {
                auto &cmd_list = ti.get_recorder();
                auto input_images = InputImages{};
                auto size = ti.get_device().info_image(ti.uses[output_view].image()).value().size;
                for (auto const &input : pass.inputs) {
                    input_images.Channel[input.channel] = ti.uses[get_resource_view_slice(input)].view();
                    input_images.Channel_sampler[input.channel] = input.sampler;
                }
                for (uint32_t i = 0; i < 6; ++i) {
                    ShaderToyCubeTask_record(
                        pipeline,
                        cmd_list,
                        daxa_device.get_device_address(ti.uses[task_input_buffer].buffer()).value(),
                        input_images,
                        ti.uses[face_views[i]].view(),
                        i);
                }
                pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
            },
            .name = std::string("cube task ") + pass.name,
        });
        pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
    }

    {
        auto &pass = image_pass;
        auto uses = std::vector<daxa::GenericTaskResourceUse>{};
        uses.push_back(daxa::TaskBufferUse<daxa::TaskBufferAccess::FRAGMENT_SHADER_READ>{task_input_buffer});
        for (auto const &input : pass.inputs) {
            if (input.type == ShaderPassInputType::CUBE) {
                uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::CUBE>{get_resource_view_slice(input)});
            } else {
                uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_2D>{get_resource_view_slice(input)});
            }
        }
        auto pipeline = pass.pipeline;
        auto inputs = pass.inputs;
        auto output_view = viewport_render_image;
        uses.push_back(daxa::TaskImageUse<daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D>{output_view});
        task_graph.add_task({
            .uses = uses,
            .task = [this, &pass, task_input_buffer, get_resource_view_slice, pipeline, inputs, output_view](daxa::TaskInterface const &ti) {
                auto &cmd_list = ti.get_recorder();
                auto input_images = InputImages{};
                auto size = ti.get_device().info_image(ti.uses[output_view].image()).value().size;
                for (auto const &input : pass.inputs) {
                    input_images.Channel[input.channel] = ti.uses[get_resource_view_slice(input)].view();
                    input_images.Channel_sampler[input.channel] = input.sampler;
                }
                ShaderToyTask_record(
                    pipeline,
                    cmd_list,
                    daxa_device.get_device_address(ti.uses[task_input_buffer].buffer()).value(),
                    input_images,
                    ti.uses[output_view].image(),
                    daxa_u32vec2{size.x, size.y});
            },
            .name = "image task",
        });
        if (pass.needs_mipmap) {
            for (uint32_t i = 0; i < MAX_MIP - 1; ++i) {
                task_graph.add_task(MipMapTask{
                    .uses = {
                        .lower_mip = output_view.view({.base_mip_level = i}),
                        .higher_mip = output_view.view({.base_mip_level = i + 1}),
                    },
                    .name = std::string("mip map ") + std::to_string(i),
                    .mip = i,
                });
            }
        }
    }

    task_graph.add_task({
        .uses = {
            daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_READ>{viewport_render_image},
            daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_WRITE>{task_swapchain_image},
        },
        .task = [viewport_render_image, this](daxa::TaskInterface const &ti) {
            auto &recorder = ti.get_recorder();
            auto image_size = ti.get_device().info_image(ti.uses[viewport_render_image].image()).value().size;
            recorder.blit_image_to_image({
                .src_image = ti.uses[viewport_render_image].image(),
                .src_image_layout = ti.uses[viewport_render_image].layout(),
                .dst_image = ti.uses[task_swapchain_image].image(),
                .dst_image_layout = ti.uses[task_swapchain_image].layout(),
                .src_offsets = {{{0, 0, 0}, {static_cast<int32_t>(image_size.x), static_cast<int32_t>(image_size.y), 1}}},
                .dst_offsets = {{{0, 0, 0}, {static_cast<int32_t>(image_size.x), static_cast<int32_t>(image_size.y), 1}}},
                .filter = daxa::Filter::LINEAR,
            });
        },
        .name = "blit_image_to_image",
    });

    // task_graph.add_task({
    //     .uses = {
    //         daxa::TaskImageUse<daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D>{task_swapchain_image},
    //     },
    //     .task = [this](daxa::TaskInterface ti) {
    //         auto &recorder = ti.get_recorder();
    //         ui.render(recorder, ti.uses[task_swapchain_image].image());
    //     },
    //     .name = "ui draw",
    // });
    task_graph.submit({});
    task_graph.present({});
    task_graph.complete({});
    return task_graph;
}
