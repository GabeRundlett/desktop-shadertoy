#define DAXA_REMOVE_DEPRECATED 0

#include <app/viewport.hpp>
#include <app/resources.hpp>
#include <app/glsl_transform.hpp>

#include <GLFW/glfw3.h>
#include <daxa/c/core.h>
#include <daxa/utils/task_graph_types.hpp>
#include <stb_image.h>

#include <unordered_map>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <regex>

#include <fstream>

#include <daxa/utils/task_graph.hpp>

enum struct ShaderToyFilter {
    NEAREST,
    LINEAR,
    MIPMAP,
};
enum struct ShaderToyWrap {
    CLAMP,
    REPEAT,
};

#define MAX_MIP 9

auto do_blit(daxa::TaskInterface ti, daxa::ImageId lower_mip, daxa::ImageId higher_mip, uint32_t mip) {
    auto image_size = ti.device.image_info(lower_mip).value().size;
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
    ti.recorder.blit_image_to_image({
        .src_image = lower_mip,
        .dst_image = higher_mip,
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
}

void GpuInputUploadTransferTask_record(daxa::Device &device, daxa::CommandRecorder &cmd_list, daxa::BufferId input_buffer, GpuInput &gpu_input) {
    auto staging_input_buffer = device.create_buffer({
        .size = sizeof(GpuInput),
        .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .name = "staging_input_buffer",
    });
    cmd_list.destroy_buffer_deferred(staging_input_buffer);
    auto *buffer_ptr = device.buffer_host_address_as<GpuInput>(staging_input_buffer).value();
    *buffer_ptr = gpu_input;
    cmd_list.copy_buffer_to_buffer({
        .src_buffer = staging_input_buffer,
        .dst_buffer = input_buffer,
        .size = sizeof(GpuInput),
    });
}

void ShaderToyTask_record(std::shared_ptr<daxa::RasterPipeline> const &pipeline, daxa::CommandRecorder &cmd_list, BDA input_buffer_ptr, InputImages const &images, daxa::ImageViewId render_target, daxa_u32vec2 size) {
    if (!pipeline || !pipeline->is_valid()) {
        return;
    }
    auto renderpass = std::move(cmd_list).begin_renderpass({
        .color_attachments = {{.image_view = render_target, .load_op = daxa::AttachmentLoadOp::LOAD, .clear_value = std::array<daxa_f32, 4>{0.5f, 0.5f, 0.5f, 1.0f}}},
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

namespace {
    // Maps a shadertoy channel (iChannel0-3) to the task attachment that holds its image.
    struct ChannelBinding {
        uint32_t attachment_index = ~0u;
        daxa::SamplerId sampler = {};
    };
    using ChannelBindings = std::array<ChannelBinding, 4>;

    // Adds a sampled fragment shader attachment for each of a pass's inputs. The first one gets the
    // attachment index `first_attachment_index`. The task graph requires all attachments of a task to
    // refer to different resources, so channels that read the same image share one attachment.
    template <typename GetViewFn>
    auto add_input_attachments(daxa::Task &task, std::vector<ShaderPassInput> const &inputs, uint32_t first_attachment_index, GetViewFn const &get_view) -> ChannelBindings {
        auto bindings = ChannelBindings{};
        auto attached_views = std::vector<daxa::TaskImageView>{};
        auto input_view_type = [](ShaderPassInputType type) -> daxa::ImageViewType {
            switch (type) {
            case ShaderPassInputType::CUBE:
            case ShaderPassInputType::CUBE_TEXTURE: return daxa::ImageViewType::CUBE;
            case ShaderPassInputType::VOLUME_TEXTURE: return daxa::ImageViewType::REGULAR_3D;
            default: return daxa::ImageViewType::REGULAR_2D;
            }
        };
        for (auto const &input : inputs) {
            if (input.type == ShaderPassInputType::NONE || input.channel >= bindings.size()) {
                continue;
            }
            auto const view = get_view(input);
            auto iter = std::find(attached_views.begin(), attached_views.end(), view);
            if (iter == attached_views.end()) {
                task.fragment_shader.samples(input_view_type(input.type), view);
                iter = attached_views.insert(attached_views.end(), view);
            }
            bindings[input.channel] = {
                .attachment_index = first_attachment_index + static_cast<uint32_t>(iter - attached_views.begin()),
                .sampler = input.sampler,
            };
        }
        return bindings;
    }

    auto get_input_images(daxa::TaskInterface const &ti, ChannelBindings const &bindings) -> InputImages {
        auto input_images = InputImages{};
        for (uint32_t channel = 0; channel < bindings.size(); ++channel) {
            if (bindings[channel].attachment_index == ~0u) {
                continue;
            }
            input_images.Channel[channel] = ti.get(daxa::TaskImageAttachmentIndex{bindings[channel].attachment_index}).view_ids[0];
            input_images.Channel_sampler[channel] = bindings[channel].sampler;
        }
        return input_images;
    }

    // Generates mips 1..MAX_MIP-1 from mip 0. The whole mip chain is a single attachment (a task may not
    // attach the same image twice), so the barriers between the mip levels are recorded manually.
    void add_mip_generation_task(daxa::TaskGraph &task_graph, daxa::TaskImageView image_view, std::string const &name) {
        auto const mip_chain_view = image_view.mips(0, MAX_MIP);
        task_graph.add_task(
            daxa::Task::Transfer(name)
                .reads_writes(mip_chain_view)
                .executes([mip_chain_view](daxa::TaskInterface ti) {
                    auto const image = ti.get(mip_chain_view).id;
                    for (uint32_t mip = 0; mip < MAX_MIP - 1; ++mip) {
                        if (mip > 0) {
                            ti.recorder.pipeline_barrier({
                                .src_access = daxa::AccessConsts::TRANSFER_WRITE,
                                .dst_access = daxa::AccessConsts::TRANSFER_READ,
                            });
                        }
                        do_blit(ti, image, image, mip);
                    }
                }));
    }
} // namespace

void replace_all(std::string &s, std::string const &toReplace, std::string const &replaceWith, bool wordBoundary = false) {
    std::string buf;
    std::size_t pos = 0;
    std::size_t prevPos{};

    // Reserves rough estimate of final size of string.
    buf.reserve(s.size());

    auto is_word_char = [](char c) -> bool {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_';
    };

    while (true) {
        prevPos = pos;
        pos = s.find(toReplace, pos);
        if (pos == std::string::npos) {
            break;
        }

        bool is_word_bound_begin = pos == 0 || !is_word_char(s[pos - 1]);
        bool is_word_bound_end = pos == (s.size() - toReplace.size()) || !is_word_char(s[pos + toReplace.size()]);
        if ((!is_word_bound_begin || !is_word_bound_end) && wordBoundary) {
            buf.append(s, prevPos, pos - prevPos);
            buf += toReplace;
            pos += toReplace.size();
        } else {
            buf.append(s, prevPos, pos - prevPos);
            buf += replaceWith;
            pos += toReplace.size();
        }
    }

    buf.append(s, prevPos, s.size() - prevPos);
    s.swap(buf);
}

// Code that was stored with escaped newlines ("\\n") has no real newlines. Only such code is unescaped, as
// otherwise a "\\n" in a comment (e.g. a commented out printf) would break the code.
void unescape_newlines_if_needed(std::string &code) {
    if (code.find('\n') == std::string::npos) {
        replace_all(code, "\\n", "\n");
    }
}

// Whether the code contains "#define <name>" without a parameter list.
auto defines_object_like_macro(std::string const &code, std::string const &name) -> bool {
    static auto const define_regex = std::regex{R"(#[ \t]*define[ \t]+(\w+)(\(?))"};
    for (auto it = std::sregex_iterator(code.begin(), code.end(), define_regex); it != std::sregex_iterator{}; ++it) {
        if ((*it)[1] == name && (*it)[2].length() == 0) {
            return true;
        }
    }
    return false;
}

void shader_preprocess(std::string &contents, std::filesystem::path const & /*path*/) {
    // Shadertoy's sampler types are emulated with structs (see viewport.inl), so that they can be passed to
    // functions. The rest of the user code conversion happens in transform_shadertoy_code (glsl_transform.cpp).
    std::string line = {};
    std::stringstream file_ss{contents};
    std::stringstream result_ss = {};
    while (std::getline(file_ss, line)) {
        replace_all(line, "sampler2D", "CombinedImageSampler2D", true);
        replace_all(line, "sampler3D", "CombinedImageSampler3D", true);
        replace_all(line, "samplerCube", "CombinedImageSamplerCube", true);
        result_ss << line << '\n';
    }
    contents = result_ss.str();
}

Viewport::Viewport(daxa::Device a_daxa_device)
    : daxa_device{std::move(a_daxa_device)},
      pipeline_manager{[this]() {
          // Debugging aid: DESKTOP_SHADERTOY_DUMP_SHADERS=<dir> writes the preprocessed source and the
          // SPIR-V of every compiled shader to <dir>.
          auto dump_dir = std::optional<std::filesystem::path>{};
          if (char const *dir = std::getenv("DESKTOP_SHADERTOY_DUMP_SHADERS"); dir != nullptr && *dir != '\0') {
              dump_dir = dir;
              std::filesystem::create_directories(*dump_dir);
          }
          auto result = daxa::PipelineManager(
              daxa::PipelineManagerInfo2{
                  .device = daxa_device,
                  .root_paths = {DAXA_SHADER_INCLUDE_DIR, resource_dir / "src", "src"},
                  .write_out_preprocessed_code = dump_dir,
                  .write_out_spirv = dump_dir,
                  .register_null_pipelines_when_first_compile_fails = true,
                  .custom_preprocessor = shader_preprocess,
                  .default_language = daxa::ShaderLanguage::GLSL,
                  .default_enable_debug_info = false,
                  .name = "pipeline_manager",
              });
          return result;
      }()} {
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

Viewport::~Viewport() {
    for (auto &sampler : samplers) {
        daxa_device.destroy_sampler(sampler);
    }
    for (auto &[str, elem] : loaded_textures) {
        auto &[image_id, idx] = elem;
        daxa_device.destroy_image(image_id);
    }
}

void Viewport::update() {
    using namespace std::chrono_literals;
    auto now = Clock::now();
    gpu_input.Time = std::chrono::duration<daxa_f32>(now - start).count();
    gpu_input.TimeDelta = std::max(std::numeric_limits<float>::min(), std::chrono::duration<daxa_f32>(now - prev_time).count());
    gpu_input.FrameRate = 1.0f / gpu_input.TimeDelta;
    prev_time = now;
    if (now - prev_fps_time > 0.5s) {
        prev_fps_time = now;
        last_known_fps = static_cast<float>(fps_count) / 0.5f;
        fps_count = 0;
    }
    ++fps_count;
}

void Viewport::render() {
    for (auto &pass : buffer_passes) {
        pass.buffer.swap();
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
    }
    for (auto &pass : cube_passes) {
        pass.buffer.swap();
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
    }
}

auto Viewport::record(daxa::TaskGraph &task_graph) -> daxa::TaskImageView {
    auto viewport_render_image = task_graph.create_task_image({
        .format = daxa::Format::R16G16B16A16_SFLOAT,
        .size = {static_cast<uint32_t>(gpu_input.Resolution.x), static_cast<uint32_t>(gpu_input.Resolution.y), 1},
        .name = "viewport_render_image",
    });

    for (auto const &task_texture : task_textures) {
        task_graph.register_image(task_texture);
    }

    auto task_input_buffer = task_graph.create_task_buffer({
        .size = static_cast<uint32_t>(sizeof(GpuInput)),
        .name = "input_buffer",
    });
    task_graph.add_task(
        daxa::Task::Transfer("GpuInputUploadTransferTask")
            .writes(task_input_buffer)
            .executes([this, task_input_buffer](daxa::TaskInterface ti) {
                GpuInputUploadTransferTask_record(daxa_device, ti.recorder, ti.get(task_input_buffer).id, gpu_input);
            }));

    // The task graph derives a transient image's usage flags from its attachments, so only create the
    // keyboard image when it is sampled. Otherwise it would only have TRANSFER_DST usage, which is invalid
    // for the image's default view.
    auto const pass_uses_keyboard = [](auto const &pass) {
        return std::any_of(pass.inputs.begin(), pass.inputs.end(), [](ShaderPassInput const &input) { return input.type == ShaderPassInputType::KEYBOARD; });
    };
    auto const uses_keyboard =
        pass_uses_keyboard(image_pass) ||
        std::any_of(buffer_passes.begin(), buffer_passes.end(), pass_uses_keyboard) ||
        std::any_of(cube_passes.begin(), cube_passes.end(), pass_uses_keyboard);

    auto task_keyboard_image = daxa::TaskImageView{};
    if (uses_keyboard) {
        task_keyboard_image = task_graph.create_task_image({
            .format = daxa::Format::R8_UINT,
            .size = {256, 3, 1},
            .name = "keyboard_image",
        });
        task_graph.add_task(
            daxa::Task::Transfer("KeyboardInputUploadTask")
                .writes(task_keyboard_image)
                .executes([this, task_keyboard_image](daxa::TaskInterface ti) {
                    auto &cmd_list = ti.recorder;
                    auto staging_buffer = daxa_device.create_buffer({
                        .size = sizeof(KeyboardInput),
                        .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                        .name = "staging_buffer",
                    });
                    cmd_list.destroy_buffer_deferred(staging_buffer);
                    auto *buffer_ptr = daxa_device.buffer_host_address_as<KeyboardInput>(staging_buffer).value();
                    *buffer_ptr = keyboard_input;
                    cmd_list.copy_buffer_to_image({
                        .src_buffer = staging_buffer,
                        .dst_image = ti.get(task_keyboard_image).id,
                        .image_extent = {256, 3, 1},
                    });
                }));
    }

    auto get_resource_view = [this, task_keyboard_image](ShaderPassInput const &input) -> daxa::TaskImageView {
        switch (input.type) {
        case ShaderPassInputType::BUFFER: return buffer_passes[input.index].recording_buffer_view;
        case ShaderPassInputType::CUBE: return cube_passes[input.index].recording_buffer_view;
        case ShaderPassInputType::KEYBOARD: return task_keyboard_image;
        case ShaderPassInputType::TEXTURE:
        case ShaderPassInputType::CUBE_TEXTURE:
        case ShaderPassInputType::VOLUME_TEXTURE:
            return task_textures[input.index];
        case ShaderPassInputType::NONE: break;
        }
        return {};
    };

    auto get_resource_view_slice = [get_resource_view](ShaderPassInput const &input) -> daxa::TaskImageView {
        auto view = get_resource_view(input);
        switch (input.type) {
        case ShaderPassInputType::BUFFER: return view.mips(0, MAX_MIP);
        case ShaderPassInputType::CUBE: return view.layers(0, 6);
        case ShaderPassInputType::KEYBOARD:
        case ShaderPassInputType::TEXTURE:
        case ShaderPassInputType::VOLUME_TEXTURE: return view;
        case ShaderPassInputType::CUBE_TEXTURE: return view.layers(0, 6);
        case ShaderPassInputType::NONE: break;
        }
        return {};
    };

    for (auto &pass : buffer_passes) {
        auto const image_info = daxa::ImageInfo{
            .format = daxa::Format::R32G32B32A32_SFLOAT,
            .size = {static_cast<uint32_t>(gpu_input.Resolution.x), static_cast<uint32_t>(gpu_input.Resolution.y), 1},
            .mip_level_count = MAX_MIP,
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
            .name = std::string{"buffer "} + std::string{pass.name},
        };
        if (first_record_after_load) {
            pass.buffer = {};
            pass.buffer.get(daxa_device, image_info);
        } else {
            // resize the image while keeping the contents
            auto temp_task_graph = daxa::TaskGraph(daxa::TaskGraphInfo{
                .device = daxa_device,
                .name = "temp_tg",
            });
            auto new_buffer = PingPongImage{};
            new_buffer.get(daxa_device, image_info);
            auto const src_view = temp_task_graph.register_image(pass.buffer.task_resources.history_resource).mips(0, MAX_MIP);
            auto const dst_view = temp_task_graph.register_image(new_buffer.task_resources.output_resource).mips(0, MAX_MIP);
            temp_task_graph.add_task(
                daxa::Task::Transfer("resize_blit")
                    .reads(src_view)
                    .writes(dst_view)
                    .executes([src_view, dst_view](daxa::TaskInterface ti) {
                        auto const src_image = ti.get(src_view).id;
                        auto const dst_image = ti.get(dst_view).id;
                        auto size_a = ti.device.image_info(src_image).value().size;
                        auto size_b = ti.device.image_info(dst_image).value().size;
                        auto size = daxa_i32vec2{
                            std::min(static_cast<int32_t>(size_a.x), static_cast<int32_t>(size_b.x)),
                            std::min(static_cast<int32_t>(size_a.y), static_cast<int32_t>(size_b.y)),
                        };
                        ti.recorder.blit_image_to_image({
                            .src_image = src_image,
                            .dst_image = dst_image,
                            .src_offsets = {{{0, 0, 0}, {size.x, size.y, 1}}},
                            .dst_offsets = {{{0, 0, 0}, {size.x, size.y, 1}}},
                            .filter = daxa::Filter::NEAREST,
                        });
                    }));
            temp_task_graph.submit({});
            temp_task_graph.complete({});
            temp_task_graph.execute({});
            pass.buffer = std::move(new_buffer);
        }
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
        task_graph.register_image(pass.buffer.task_resources.output_resource);
        task_graph.register_image(pass.buffer.task_resources.history_resource);
    }

    for (auto &pass : cube_passes) {
        if (first_record_after_load) {
            pass.buffer = {};
        }
        pass.buffer.get(
            daxa_device,
            daxa::ImageInfo{
                .flags = daxa::ImageCreateFlagBits::COMPATIBLE_CUBE,
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .size = {1024, 1024, 1},
                .mip_level_count = 1,
                .array_layer_count = 6,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
                .name = std::string{"cube buffer "} + std::string{pass.name},
            });
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;

        task_graph.register_image(pass.buffer.task_resources.output_resource);
        task_graph.register_image(pass.buffer.task_resources.history_resource);
    }

    // Attachment layout of every shader pass task:
    //   0:       the gpu input buffer
    //   1..N:    the sampled input images (see add_input_attachments)
    //   N+1:     the color attachment
    // The task callbacks must stay trivially destructible, so they capture a pointer to the pass's
    // pipeline (same lifetime as the old `&pass` capture: the graph is re-recorded whenever passes change).
    auto add_shader_pass_task = [&](ShaderBufferPass &pass, daxa::TaskImageView output_view, std::string const &task_name) {
        auto task = daxa::Task::Raster(task_name);
        task.fragment_shader.reads(task_input_buffer);
        auto const channels = add_input_attachments(task, pass.inputs, 1, get_resource_view_slice);
        task.color_attachment.reads_writes(daxa::ImageViewType::REGULAR_2D, output_view);
        task.executes([pipeline = &pass.pipeline, task_input_buffer, output_view, channels](daxa::TaskInterface ti) {
            auto size = ti.device.image_info(ti.get(output_view).id).value().size;
            ShaderToyTask_record(
                *pipeline,
                ti.recorder,
                ti.device_address(task_input_buffer).value(),
                get_input_images(ti, channels),
                ti.get(output_view).view_ids[0],
                daxa_u32vec2{size.x, size.y});
        });
        task_graph.add_task(task);
    };

    for (auto &pass : buffer_passes) {
        auto const output_view = pass.buffer.task_resources.output_resource.view();
        add_shader_pass_task(pass, output_view, std::string("buffer task ") + pass.name);
        // Passes recorded after this one read this frame's output.
        pass.recording_buffer_view = output_view;
        if (pass.needs_mipmap) {
            add_mip_generation_task(task_graph, output_view, std::string("mip map ") + pass.name);
        }
    }

    for (auto &pass : cube_passes) {
        // All six faces are a single attachment (a task may not attach the same image more than once).
        // The per-face render target views are created while recording.
        auto const output_view = pass.buffer.task_resources.output_resource.view().layers(0, 6);
        auto const task_name = std::string("cube task ") + pass.name;
        auto task = daxa::Task::Raster(task_name);
        task.fragment_shader.reads(task_input_buffer);
        auto const channels = add_input_attachments(task, pass.inputs, 1, get_resource_view_slice);
        task.color_attachment.reads_writes(output_view);
        task.executes([pipeline = &pass.pipeline, task_input_buffer, output_view, channels](daxa::TaskInterface ti) {
            auto const image = ti.get(output_view).id;
            auto const format = ti.device.image_info(image).value().format;
            auto const input_images = get_input_images(ti, channels);
            auto const input_buffer_ptr = ti.device_address(task_input_buffer).value();
            for (uint32_t i = 0; i < 6; ++i) {
                auto const face_view = ti.device.create_image_view({
                    .type = daxa::ImageViewType::REGULAR_2D,
                    .format = format,
                    .image = image,
                    .slice = {.base_array_layer = i},
                    .name = "cube face",
                });
                ti.recorder.destroy_image_view_deferred(face_view);
                ShaderToyCubeTask_record(*pipeline, ti.recorder, input_buffer_ptr, input_images, face_view, i);
            }
        });
        task_graph.add_task(task);
        pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
    }

    // The image pass is never sampled by another pass, so it never needs mips.
    add_shader_pass_task(image_pass, viewport_render_image, "image task");

    first_record_after_load = false;

    return viewport_render_image;
}

void Viewport::reset() {
    gpu_input.Frame = 0;
    auto now = Clock::now();
    start = now;
    gpu_input.Time = std::chrono::duration<daxa_f32>(now - start).count();
    gpu_input.TimeDelta = std::max(std::numeric_limits<float>::min(), std::chrono::duration<daxa_f32>(now - prev_time).count());
    gpu_input.FrameRate = 1.0f / gpu_input.TimeDelta;
    prev_time = now;
    prev_fps_time = now;
    fps_count = 0;
    last_known_fps = 1.0f;
    gpu_input.Mouse = {0.0f, 0.0f, -1.0f, -1.0f};
    is_reset = true;
}

void Viewport::on_mouse_move(float px, float py) {
    mouse_pos.x = px;
    mouse_pos.y = gpu_input.Resolution.y - py - 1.0f;
    if (mouse_enabled) {
        gpu_input.Mouse.x = mouse_pos.x;
        gpu_input.Mouse.y = mouse_pos.y;
    }
}

void Viewport::on_mouse_button(int32_t button_id, int32_t action) {
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
}

void Viewport::on_key(int32_t key_id, int32_t action) {
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
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT:
        transformed_key_id = 16;
        break;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL:
        transformed_key_id = 17;
        break;
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT:
        transformed_key_id = 18;
        break;
    case GLFW_KEY_CAPS_LOCK:
        transformed_key_id = 20;
        break;
    case GLFW_KEY_TAB:
        transformed_key_id = 9;
        break;
    default:
        break;
    }
    if (transformed_key_id < 0 || transformed_key_id >= 256) {
        return;
    }

    keyboard_input.current_state[static_cast<size_t>(transformed_key_id)] = static_cast<int8_t>(action != GLFW_RELEASE);
    if (action == GLFW_PRESS) {
        keyboard_input.keypress[static_cast<size_t>(transformed_key_id)] = 1;
        keyboard_input.toggles[static_cast<size_t>(transformed_key_id)] = 1 - keyboard_input.toggles[static_cast<size_t>(transformed_key_id)];
    }
}

void Viewport::on_toggle_pause(bool is_paused) {
    if (is_paused) {
        pause_time = Clock::now();
        is_reset = false;
    } else if (is_reset) {
        auto now = Clock::now();
        start = now;
        prev_time = now;
    } else {
        auto now = Clock::now();
        start += (now - pause_time);
        prev_time += (now - pause_time);
    }
}

auto Viewport::load_texture(std::string path) -> std::pair<daxa::ImageId, size_t> {
    auto task_image_index = task_textures.size();
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t channel_n = 0;
    // External task image names must be unique within a task graph.
    auto const task_image_name = "texture " + path;
    replace_all(path, "/media/a/", "media/images/");
    stbi_set_flip_vertically_on_load(1);
    auto *stb_data = stbi_load(path.c_str(), &size_x, &size_y, &channel_n, 4);
    auto *temp_data = stb_data;
    auto *heap_data = (stbi_uc *)nullptr;
    auto temp_format = daxa::Format::R8G8B8A8_UNORM;
    auto pixel_size_bytes = 4;
    if (stb_data == nullptr) {
        // check if the file exists at all, allowing people to load a file to binary data
        auto file = std::ifstream{path, std::ios::binary};
        if (file.good()) {
            auto size = std::filesystem::file_size(path);
            pixel_size_bytes = 16;
            size = (size + pixel_size_bytes - 1) & ~(pixel_size_bytes - 1);
            size_x = std::min<int>(size / pixel_size_bytes, 1024);
            size_y = (size / pixel_size_bytes + 1023) / 1024;
            heap_data = new stbi_uc[size_x * size_y * pixel_size_bytes];
            file.read(reinterpret_cast<char *>(heap_data), static_cast<std::streamsize>(size));
            temp_data = heap_data;
            temp_format = daxa::Format::R32G32B32A32_UINT;
        } else {
            // Missing file: use a 1x1 black texture.
            core::log_error("missing texture: " + path);
            size_x = 1;
            size_y = 1;
            heap_data = new stbi_uc[4]{0, 0, 0, 255};
            temp_data = heap_data;
        }
    }
    auto image_id = daxa_device.create_image({
        .dimensions = 2,
        .format = temp_format,
        .size = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = "texture",
    });
    auto task_image = daxa::ExternalTaskImage({.image = image_id, .name = task_image_name});

    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    auto const task_image_view = temp_task_graph.register_image(task_image);
    temp_task_graph.add_task(
        daxa::Task::Transfer("upload_user_texture")
            .writes(task_image_view)
            .executes([this, temp_data, size_x, size_y, image_id, pixel_size_bytes](daxa::TaskInterface task_runtime) {
                auto staging_buffer = daxa_device.create_buffer({
                    .size = static_cast<uint32_t>(size_x * size_y * pixel_size_bytes),
                    .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                    .name = "staging_buffer",
                });
                auto *buffer_ptr = daxa_device.buffer_host_address_as<uint8_t>(staging_buffer).value();
                memcpy(buffer_ptr, temp_data, size_x * size_y * pixel_size_bytes);
                auto &cmd_list = task_runtime.recorder;
                cmd_list.pipeline_barrier({
                    .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
                });
                cmd_list.destroy_buffer_deferred(staging_buffer);
                cmd_list.copy_buffer_to_image({
                    .src_buffer = staging_buffer,
                    .dst_image = image_id,
                    .image_extent = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
                });
            }));
    temp_task_graph.submit({});
    temp_task_graph.complete({});
    temp_task_graph.execute({});
    task_textures.push_back(task_image);
    if (stb_data != nullptr) {
        stbi_image_free(temp_data);
    }
    if (heap_data != nullptr) {
        delete[] heap_data;
    }
    return std::pair<daxa::ImageId, size_t>{image_id, task_image_index};
}

auto Viewport::load_cube_texture(std::string path) -> std::pair<daxa::ImageId, size_t> {
    auto task_image_index = task_textures.size();
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t channel_n = 0;
    // External task image names must be unique within a task graph.
    auto const task_image_name = "cube texture " + path;
    replace_all(path, "/media/a/", "media/images/");
    stbi_set_flip_vertically_on_load(0);
    // Face i > 0 is stored in "<name>_<i>.<ext>". Missing faces (or faces with a different size than the
    // first loaded face) are replaced by black.
    auto faces = std::array<std::vector<uint8_t>, 6>{};
    for (uint32_t i = 0; i < 6; ++i) {
        auto face_path = std::filesystem::path(path);
        if (i > 0) {
            face_path = face_path.parent_path() / std::filesystem::path(face_path.stem().string() + "_" + std::to_string(i) + face_path.extension().string());
        }
        int32_t face_x = 0;
        int32_t face_y = 0;
        auto *face_data = stbi_load(face_path.string().c_str(), &face_x, &face_y, &channel_n, 4);
        if (face_data == nullptr) {
            core::log_error("missing cube texture face: " + face_path.string());
            continue;
        }
        if (size_x == 0) {
            size_x = face_x;
            size_y = face_y;
        }
        if (face_x == size_x && face_y == size_y) {
            faces[i].assign(face_data, face_data + static_cast<size_t>(face_x) * face_y * 4);
        }
        stbi_image_free(face_data);
    }
    if (size_x == 0) {
        size_x = 1;
        size_y = 1;
    }
    for (auto &face : faces) {
        if (face.empty()) {
            face.resize(static_cast<size_t>(size_x) * size_y * 4, 0);
        }
    }
    auto image_id = daxa_device.create_image({
        .flags = daxa::ImageCreateFlagBits::COMPATIBLE_CUBE,
        .dimensions = 2,
        .format = daxa::Format::R8G8B8A8_UNORM,
        .size = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
        .array_layer_count = 6,
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = "cube texture",
    });
    auto task_image = daxa::ExternalTaskImage({.image = image_id, .name = task_image_name});
    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    auto const task_image_view = temp_task_graph.register_image(task_image).layers(0, 6);
    // One task uploads all six faces (a task may not attach the same image more than once).
    temp_task_graph.add_task(
        daxa::Task::Transfer("upload_user_texture")
            .writes(task_image_view)
            .executes([this, &faces, size_x, size_y, image_id](daxa::TaskInterface task_runtime) {
                auto &cmd_list = task_runtime.recorder;
                cmd_list.pipeline_barrier({
                    .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
                });
                for (uint32_t i = 0; i < 6; ++i) {
                    auto staging_buffer = daxa_device.create_buffer({
                        .size = static_cast<uint32_t>(size_x * size_y * 4),
                        .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                        .name = "staging_buffer",
                    });
                    auto *buffer_ptr = daxa_device.buffer_host_address_as<uint8_t>(staging_buffer).value();
                    memcpy(buffer_ptr, faces[i].data(), faces[i].size());
                    cmd_list.destroy_buffer_deferred(staging_buffer);
                    cmd_list.copy_buffer_to_image({
                        .src_buffer = staging_buffer,
                        .dst_image = image_id,
                        .image_slice = {
                            .base_array_layer = i,
                        },
                        .image_extent = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
                    });
                }
            }));
    temp_task_graph.submit({});
    temp_task_graph.complete({});
    temp_task_graph.execute({});
    task_textures.push_back(task_image);
    return std::pair<daxa::ImageId, size_t>{image_id, task_image_index};
}

auto Viewport::load_volume_texture(std::string id) -> std::pair<daxa::ImageId, size_t> {
    auto task_image_index = task_textures.size();

    auto num_channels = uint32_t{4};
    if (id == "4sfGRr") {
        num_channels = 1;
    }

    auto random_numbers = std::vector<uint8_t>{};
    random_numbers.resize(32 * 32 * 32 * num_channels);

    auto rng = std::mt19937_64(std::hash<std::string>{}(id));
    auto dist = std::uniform_int_distribution<std::mt19937::result_type>(0, 255);
    for (auto &num : random_numbers) {
        num = dist(rng) & 0xff;
    }

    auto const *name = num_channels == 1 ? "gray_rnd_volume" : "rgba_rnd_volume";

    auto image_id = daxa_device.create_image({
        .dimensions = 3,
        .format = num_channels == 1 ? daxa::Format::R8_UNORM : daxa::Format::R8G8B8A8_UNORM,
        .size = {32, 32, 32},
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = name,
    });
    // External task image names must be unique within a task graph.
    auto task_image = daxa::ExternalTaskImage({.image = image_id, .name = std::string(name) + " " + id});

    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    auto const task_image_view = temp_task_graph.register_image(task_image);
    temp_task_graph.add_task(
        daxa::Task::Transfer("upload_user_texture")
            .writes(task_image_view)
            .executes([this, &random_numbers, image_id](daxa::TaskInterface task_runtime) {
                auto staging_buffer = daxa_device.create_buffer({
                    .size = static_cast<uint32_t>(random_numbers.size()),
                    .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                    .name = "staging_buffer",
                });
                auto *buffer_ptr = daxa_device.buffer_host_address_as<uint8_t>(staging_buffer).value();
                memcpy(buffer_ptr, random_numbers.data(), random_numbers.size());
                auto &cmd_list = task_runtime.recorder;
                cmd_list.pipeline_barrier({
                    .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
                });
                cmd_list.destroy_buffer_deferred(staging_buffer);
                cmd_list.copy_buffer_to_image({
                    .src_buffer = staging_buffer,
                    .dst_image = image_id,
                    .image_extent = {32, 32, 32},
                });
            }));
    temp_task_graph.submit({});
    temp_task_graph.complete({});
    temp_task_graph.execute({});
    task_textures.push_back(task_image);

    return std::pair<daxa::ImageId, size_t>{image_id, task_image_index};
}

void Viewport::load_shadertoy_json(nlohmann::json json) {
    this->load_failed = false;
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
            id = nlohmann::to_string(output["id"]);
        }
        replace_all(id, "\"", "");
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

    std::vector<ShaderBufferPass> new_buffer_passes{};
    std::vector<ShaderCubePass> new_cube_passes{};
    ShaderBufferPass new_image_pass{};

    new_buffer_passes.reserve(buffer_pass_n);
    new_cube_passes.reserve(cube_pass_n);
    unescape_newlines_if_needed(common_code);

    auto is_supported_pass_type = [](nlohmann::json const &pass_type) {
        return pass_type == "image" || pass_type == "buffer" || pass_type == "cubemap";
    };
    // The virtual file names of the passes are generated, as the pass names (which may be empty, or contain
    // any characters) can't be used in #include directives.
    auto pass_file_name = [](size_t pass_index) { return "_ds_pass" + std::to_string(pass_index); };

    // Convert the WebGL GLSL to Vulkan GLSL. All passes are transformed together with the common code, as
    // the transformation depends on what the common code defines.
    auto pass_codes = std::vector<std::string>(renderpasses.size() + 1);
    {
        auto code_ptrs = std::vector<std::string *>{};
        auto pass_index = size_t{0};
        for (auto &renderpass : renderpasses) {
            ++pass_index;
            if (!is_supported_pass_type(renderpass["type"])) {
                continue;
            }
            pass_codes[pass_index] = renderpass.value("code", std::string{});
            unescape_newlines_if_needed(pass_codes[pass_index]);
            code_ptrs.push_back(&pass_codes[pass_index]);
        }
        transform_shadertoy_code(common_code, code_ptrs);
    }

    pipeline_manager.add_virtual_file(daxa::VirtualFileInfo{
        .name = "common",
        .contents = common_code,
    });

    auto user_code = daxa::VirtualFileInfo{
        .name = "user_code",
        .contents = "#pragma once\n",
    };

    auto pass_i = size_t{0};
    for (auto &renderpass : renderpasses) {
        ++pass_i;
        // Skip unknown pass type
        if (!is_supported_pass_type(renderpass["type"])) {
            continue;
        }
        user_code.contents += "#if defined(_DESKTOP_SHADERTOY_USER_PASS" + std::to_string(pass_i) + ")\n";
        user_code.contents += "#include <" + pass_file_name(pass_i) + "_inputs>\n";
        user_code.contents += "#include <common>\n";
        user_code.contents += "#include <" + pass_file_name(pass_i) + ">\n";
        // An object-like "#define mainImage ..." (e.g. an antialiasing wrapper that expands into the definition of
        // the real mainImage) must not expand at the call in _ds_call_mainImage.
        if (defines_object_like_macro(common_code, "mainImage") || defines_object_like_macro(pass_codes[pass_i], "mainImage")) {
            user_code.contents += "#undef mainImage\n";
        }
        if (defines_object_like_macro(common_code, "mainCubemap") || defines_object_like_macro(pass_codes[pass_i], "mainCubemap")) {
            user_code.contents += "#undef mainCubemap\n";
        }
        user_code.contents += "#endif\n";
    }
    pipeline_manager.add_virtual_file(user_code);

    pass_i = size_t{0};
    for (auto &renderpass : renderpasses) {
        ++pass_i;
        auto &inputs = renderpass["inputs"];
        auto &name = renderpass["name"];
        auto &pass_type = renderpass["type"];
        // Skip unknown pass type
        if (!is_supported_pass_type(pass_type)) {
            continue;
        }
        auto pipeline_name = name.is_string() ? std::string{name} : std::string{};
        if (pipeline_name.empty()) {
            pipeline_name = std::string{pass_type} + " " + std::to_string(pass_i);
        }

        auto temp_inputs = std::vector<ShaderPassInput>{};

        auto pass_inputs_file = daxa::VirtualFileInfo{
            .name = pass_file_name(pass_i) + "_inputs",
            .contents = "#pragma once\n",
        };

        auto type_json_to_glsl_image_type = [](std::string const &json) -> std::string {
            if (json == "buffer" || json == "keyboard" || json == "texture") {
                return "CombinedImageSampler2D";
            } else if (json == "cubemap") {
                return "CombinedImageSamplerCube";
            } else if (json == "volume") {
                return "CombinedImageSampler3D";
            }
            return "void"; // L
        };

        auto type_json_to_glsl_image_type_extra = [](std::string const &json) -> std::string {
            if (json == "buffer" || json == "texture") {
                return ", 0";
            } else if (json == "keyboard") {
                return ", 1";
            }
            return "";
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

        // iChannel0-3 are global variables (like the uniforms in WebGL), declared before the common code.
        // Unbound channels are 2D.
        auto channel_types = std::array<std::pair<std::string, std::string>, 4>{};
        channel_types.fill({"CombinedImageSampler2D", ", 0"});

        for (auto &input : inputs) {
            auto type = std::string{};
            if (input.contains("type")) {
                type = input["type"];
            } else if (input.contains("ctype")) {
                type = input["ctype"];
            } else {
                // 😐
                continue;
            }

            auto image_type = type_json_to_glsl_image_type(type);
            auto extra = type_json_to_glsl_image_type_extra(type);

            // Skip unsupported input types
            if (type != "image" && type != "buffer" && type != "cubemap" && type != "texture" && type != "keyboard" && type != "volume") {
                continue;
            }

            auto load_texture_type = [&](ShaderPassInputType texture_input_type, std::pair<daxa::ImageId, size_t> (*load_function)(void *, std::string const &)) {
                auto input_copy = ShaderPassInput{
                    .type = texture_input_type,
                    .channel = input["channel"],
                    .sampler = get_sampler(samplers, input),
                };
                // TEMPORARY HACK
                if (input_copy.sampler == samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3]) {
                    input_copy.sampler = samplers[static_cast<size_t>(ShaderToyFilter::LINEAR) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3];
                }
                if (input_copy.sampler == samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3]) {
                    input_copy.sampler = samplers[static_cast<size_t>(ShaderToyFilter::LINEAR) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3];
                }
                auto filepath = std::string{};
                auto path = std::string{};
                if (texture_input_type == ShaderPassInputType::VOLUME_TEXTURE) {
                    path = nlohmann::to_string(input["id"]);
                    replace_all(path, "\"", "");
                } else if (input.contains("filepath")) {
                    path = std::string{input["filepath"]};
                } else if (input.contains("src")) {
                    path = std::string{input["src"]};
                } else {
                    // 😐
                    return;
                }
                if (loaded_textures.contains(path)) {
                    auto &[loaded_texture, task_image_index] = loaded_textures.at(path);
                    input_copy.index = task_image_index;
                } else {
                    auto loaded_result = load_function(this, path);
                    loaded_textures[path] = loaded_result;
                    auto &[loaded_texture, task_image_index] = loaded_result;
                    input_copy.index = task_image_index;
                }
                temp_inputs.push_back(input_copy);
            };

            auto id = nlohmann::to_string(input["id"]);
            replace_all(id, "\"", "");

            // Inputs refer to other passes by their output id. Shadertoy's json isn't always consistent there
            // (e.g. a "Cube A" pass with output id 257 being referenced as id 41), so as a fallback, inputs whose
            // source is the preview image of a pass are resolved by that preview image.
            auto pass_input = std::optional<ShaderPassInput>{};
            if (auto iter = id_map.find(id); iter != id_map.end()) {
                pass_input = iter->second;
            } else {
                auto const src = input.contains("src") && input["src"].is_string() ? std::string{input["src"]} : std::string{};
                if (src.find("/media/previz/cubemap00") != std::string::npos && cube_pass_n > 0) {
                    pass_input = ShaderPassInput{.type = ShaderPassInputType::CUBE, .index = 0};
                } else if (auto pos = src.find("/media/previz/buffer0"); pos != std::string::npos && pos + 21 < src.size()) {
                    auto const buffer_id = std::to_string(257 + (src[pos + 21] - '0'));
                    if (auto buffer_iter = id_map.find(buffer_id); buffer_iter != id_map.end()) {
                        pass_input = buffer_iter->second;
                    }
                }
            }

            if (type == "cubemap" && !pass_input) {
                load_texture_type(ShaderPassInputType::CUBE_TEXTURE, [](void *self, std::string const &path) { return static_cast<Viewport *>(self)->load_cube_texture(path); });
            } else if (type == "buffer" || type == "cubemap") {
                if (pass_input) {
                    auto input_copy = *pass_input;
                    input_copy.channel = input["channel"];
                    input_copy.sampler = get_sampler(samplers, input);
                    temp_inputs.push_back(input_copy);
                } else {
                    temp_inputs.push_back({.type = ShaderPassInputType::NONE});
                }
            } else if (type == "keyboard") {
                temp_inputs.push_back({
                    .type = ShaderPassInputType::KEYBOARD,
                    .channel = input["channel"],
                    .sampler = get_sampler(samplers, input),
                });
            } else if (type == "texture") {
                load_texture_type(ShaderPassInputType::TEXTURE, [](void *self, std::string const &path) { return static_cast<Viewport *>(self)->load_texture(path); });
            } else if (type == "volume") {
                load_texture_type(ShaderPassInputType::VOLUME_TEXTURE, [](void *self, std::string const &id) { return static_cast<Viewport *>(self)->load_volume_texture(id); });
            }
            if (auto const channel = uint32_t{input["channel"]}; channel < channel_types.size()) {
                channel_types[channel] = {image_type, extra};
            }
        }
        for (uint32_t channel = 0; channel < channel_types.size(); ++channel) {
            auto const &[image_type, extra] = channel_types[channel];
            auto const channel_str = std::to_string(channel);
            pass_inputs_file.contents += image_type + " iChannel" + channel_str + " = " + image_type + "(daxa_push_constant.input_images.Channel[" + channel_str + "], daxa_push_constant.input_images.Channel_sampler[" + channel_str + "]" + extra + ");\n";
        }

        auto pass_file = daxa::VirtualFileInfo{
            .name = pass_file_name(pass_i),
            .contents = std::move(pass_codes[pass_i]),
        };
        pipeline_manager.add_virtual_file(pass_file);
        pipeline_manager.add_virtual_file(pass_inputs_file);

        auto extra_defines = std::vector<daxa::ShaderDefine>{};
        auto pass_format = daxa::Format::R32G32B32A32_SFLOAT;
        if (pass_type == "image") {
            pass_format = daxa::Format::R16G16B16A16_SFLOAT;
            extra_defines.push_back({.name = "_DS_MAIN_IMAGE", .value = "1"});
        } else if (pass_type == "cubemap") {
            pass_format = daxa::Format::R16G16B16A16_SFLOAT;
            extra_defines.push_back({.name = "_DS_CUBEMAP", .value = "1"});
        }
        extra_defines.push_back({.name = "_DESKTOP_SHADERTOY_USER_PASS" + std::to_string(pass_i), .value = "1"});
        // Debugging aid: DESKTOP_SHADERTOY_DISABLE=zero_init,clamp_indices,loop_limit,relaxed_const disables the
        // corresponding robustness features of viewport.glsl, to find out which one prevents a GPU crash.
        if (char const *disable = std::getenv("DESKTOP_SHADERTOY_DISABLE"); disable != nullptr) {
            auto const disabled = std::string_view{disable};
            for (auto const &[option, define] : std::array<std::pair<std::string_view, std::string_view>, 4>{{
                     {"zero_init", "_DS_DISABLE_ZERO_INIT"},
                     {"clamp_indices", "_DS_DISABLE_CLAMP_INDICES"},
                     {"loop_limit", "_DS_DISABLE_LOOP_LIMIT"},
                     {"relaxed_const", "_DS_DISABLE_RELAXED_CONST"},
                 }}) {
                if (disabled.find(option) != std::string_view::npos) {
                    extra_defines.push_back({.name = std::string{define}, .value = "1"});
                }
            }
        }

        const auto shader_include_dir = resource_dir / std::filesystem::path("src");
        // NOTE: `shader_include_dir` is also a root path of the pipeline manager (see the Viewport constructor).
        auto compile_result = pipeline_manager.add_raster_pipeline2({
            .vertex_shader_info = daxa::ShaderCompileInfo2{
                .source = daxa::ShaderFile{shader_include_dir / "app/viewport.glsl"},
                .defines = extra_defines,
            },
            .fragment_shader_info = daxa::ShaderCompileInfo2{
                .source = daxa::ShaderFile{shader_include_dir / "app/viewport.glsl"},
                .defines = extra_defines,
            },
            .color_attachments = {{
                .format = pass_format,
            }},
            .push_constant_size = sizeof(ShaderToyPush),
            .name = pipeline_name,
        });
        if (compile_result.is_err() || !compile_result.value()->is_valid()) {
            core::log_error(pipeline_name + ": " + compile_result.message());
            this->load_failed = true;
            // Unregister everything compiled for this (failed) load, including the null pipeline that
            // `register_null_pipelines_when_first_compile_fails` registers for the failed pass.
            if (compile_result.is_ok()) {
                pipeline_manager.remove_raster_pipeline(compile_result.value());
            }
            for (auto &pass : new_buffer_passes) {
                pipeline_manager.remove_raster_pipeline(pass.pipeline);
            }
            for (auto &pass : new_cube_passes) {
                pipeline_manager.remove_raster_pipeline(pass.pipeline);
            }
            if (new_image_pass.pipeline) {
                pipeline_manager.remove_raster_pipeline(new_image_pass.pipeline);
            }
            return;
        }

        if (pass_type == "image") {
            new_image_pass = {pipeline_name, temp_inputs, compile_result.value()};
        } else if (pass_type == "buffer") {
            new_buffer_passes.emplace_back(pipeline_name, temp_inputs, compile_result.value());
        } else if (pass_type == "cubemap") {
            new_cube_passes.emplace_back(pipeline_name, temp_inputs, compile_result.value());
        }
    }

    {
        auto mip_sampler0 = samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3];
        auto mip_sampler1 = samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3];
        auto pass_needs_mipmaps = [&](auto &pass) {
            for (auto &input : pass.inputs) {
                if (input.type == ShaderPassInputType::NONE) {
                    continue;
                }

                if (input.sampler == mip_sampler0 || input.sampler == mip_sampler1) {
                    switch (input.type) {
                    case ShaderPassInputType::BUFFER: new_buffer_passes[input.index].needs_mipmap = true; break;
                    case ShaderPassInputType::CUBE: new_cube_passes[input.index].needs_mipmap = true; break;
                    default: break;
                    }
                }
            }
        };
        for (auto &pass : new_buffer_passes) {
            pass_needs_mipmaps(pass);
        }
        for (auto &pass : new_cube_passes) {
            pass_needs_mipmaps(pass);
        }
        pass_needs_mipmaps(new_image_pass);
    }

    for (auto &pass : buffer_passes)
        if (pass.pipeline)
            pipeline_manager.remove_raster_pipeline(pass.pipeline);
    for (auto &pass : cube_passes)
        if (pass.pipeline)
            pipeline_manager.remove_raster_pipeline(pass.pipeline);
    if (image_pass.pipeline)
        pipeline_manager.remove_raster_pipeline(image_pass.pipeline);

    buffer_passes = std::move(new_buffer_passes);
    cube_passes = std::move(new_cube_passes);
    image_pass = std::move(new_image_pass);

    first_record_after_load = true;

    reset();
}
