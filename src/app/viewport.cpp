#include <app/viewport.hpp>

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
    auto image_size = ti.device.info_image(lower_mip).value().size;
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
    if (!pipeline || !pipeline->is_valid()) {
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

void replace_all(std::string &s, std::string const &toReplace, std::string const &replaceWith, bool wordBoundary = false) {
    std::string buf;
    std::size_t pos = 0;
    std::size_t prevPos{};

    // Reserves rough estimate of final size of string.
    buf.reserve(s.size());

    auto is_word_char = [](char c) -> bool {
        return (c >= 'a' && c < 'z') ||
               (c >= 'A' && c < 'Z') ||
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
        if (!is_word_bound_begin && !is_word_bound_end && wordBoundary) {
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

// static std::regex const SAMPLER2D_REGEX = std::regex(R"regex(\bsampler2D\b)regex");
// static std::regex const SAMPLER3D_REGEX = std::regex(R"regex(\bsampler3D\b)regex");
// static std::regex const SAMPLERCUBE_REGEX = std::regex(R"regex(\bsamplerCube\b)regex");
// static std::regex const TEXTURECUBE_REGEX = std::regex(R"regex(\btextureCube\b)regex");

void shader_preprocess(std::string &contents, std::filesystem::path const &path) {
    std::smatch matches = {};
    std::string line = {};
    std::stringstream file_ss{contents};
    std::stringstream result_ss = {};
    bool is_standard_code =
        path.filename() == "daxa.glsl" ||
        path.filename() == "daxa.inl" ||
        path.filename() == "task_graph.inl" ||
        path.filename() == "viewport.glsl" ||
        path.filename() == "viewport.inl";
    while (std::getline(file_ss, line)) {
        replace_all(line, "sampler2D", "CombinedImageSampler2D", true);
        replace_all(line, "sampler3D", "CombinedImageSampler3D", true);
        replace_all(line, "samplerCube", "CombinedImageSamplerCube", true);
        if (!is_standard_code) {
            // These are functions/names that may be used in shadertoy code, however
            // they're reserved names or functions in this dialect of Vulkan GLSL.
            // Here, we'll replace them with some working names
            replace_all(line, "textureCube", "ds_TextureCube", true);
            replace_all(line, "packUnorm2x16", "ds_PackUnorm2x16", true);
            replace_all(line, "packSnorm2x16", "ds_PackSnorm2x16", true);
            replace_all(line, "packUnorm4x8", "ds_PackUnorm4x8", true);
            replace_all(line, "packSnorm4x8", "ds_PackSnorm4x8", true);
            replace_all(line, "unpackUnorm2x16", "ds_UnpackUnorm2x16", true);
            replace_all(line, "unpackSnorm2x16", "ds_UnpackSnorm2x16", true);
            replace_all(line, "unpackUnorm4x8", "ds_UnpackUnorm4x8", true);
            replace_all(line, "unpackSnorm4x8", "ds_UnpackSnorm4x8", true);
            replace_all(line, "buffer", "ds_Buffer", true);
        }
        result_ss << line << "\n";
    }
    contents = result_ss.str();
}

Viewport::Viewport(daxa::Device a_daxa_device)
    : daxa_device{std::move(a_daxa_device)},
      pipeline_manager{[this]() {
          auto result = daxa::PipelineManager({
              .device = daxa_device,
              .shader_compile_options = {
                  .root_paths = {DAXA_SHADER_INCLUDE_DIR, "src"},
                  .language = daxa::ShaderLanguage::GLSL,
                  .enable_debug_info = true,
              },
              .register_null_pipelines_when_first_compile_fails = true,
              .custom_preprocessor = shader_preprocess,
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
    auto viewport_render_image = task_graph.create_transient_image({
        .format = daxa::Format::R16G16B16A16_SFLOAT,
        .size = {static_cast<uint32_t>(gpu_input.Resolution.x), static_cast<uint32_t>(gpu_input.Resolution.y), 1},
        .name = "viewport_render_image",
    });

    for (auto const &task_texture : task_textures) {
        task_graph.use_persistent_image(task_texture);
    }

    auto task_input_buffer = task_graph.create_transient_buffer({
        .size = static_cast<uint32_t>(sizeof(GpuInput)),
        .name = "input_buffer",
    });
    auto task_keyboard_image = task_graph.create_transient_image({
        .format = daxa::Format::R8_UINT,
        .size = {256, 3, 1},
        .name = "keyboard_image",
    });

    // GpuInputUploadTransferTask
    task_graph.add_task({
        .attachments = {
            daxa::inl_attachment(daxa::TaskBufferAccess::TRANSFER_WRITE, task_input_buffer),
        },
        .task = [this, task_input_buffer](daxa::TaskInterface const &ti) {
            auto &cmd_list = ti.recorder;
            GpuInputUploadTransferTask_record(daxa_device, cmd_list, ti.get(daxa::TaskBufferAttachmentIndex{0}).ids[0], gpu_input);
        },
        .name = "GpuInputUploadTransferTask",
    });

    // KeyboardInputUploadTask
    task_graph.add_task({
        .attachments = {
            daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, task_keyboard_image),
        },
        .task = [this, task_keyboard_image](daxa::TaskInterface const &ti) {
            auto &cmd_list = ti.recorder;
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
                .image = ti.get(task_keyboard_image).ids[0],
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
        case ShaderPassInputType::TEXTURE:
        case ShaderPassInputType::CUBE_TEXTURE:
        case ShaderPassInputType::VOLUME_TEXTURE:
            return task_textures[input.index];
        }
    };

    auto get_resource_view_slice = [get_resource_view](ShaderPassInput const &input) -> daxa::TaskImageView {
        auto view = get_resource_view(input);
        switch (input.type) {
        case ShaderPassInputType::BUFFER: return view.view({.level_count = MAX_MIP});
        case ShaderPassInputType::CUBE: return view.view({.layer_count = 6});
        case ShaderPassInputType::KEYBOARD:
        case ShaderPassInputType::TEXTURE:
        case ShaderPassInputType::VOLUME_TEXTURE: return view;
        case ShaderPassInputType::CUBE_TEXTURE: return view.view({.layer_count = 6});
        }
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
            temp_task_graph.use_persistent_image(pass.buffer.task_resources.history_resource);
            temp_task_graph.use_persistent_image(new_buffer.task_resources.output_resource);
            auto output_image_view_a = pass.buffer.task_resources.history_resource.view().view({.level_count = MAX_MIP});
            auto output_image_view_b = new_buffer.task_resources.output_resource.view().view({.level_count = MAX_MIP});
            temp_task_graph.add_task({
                .attachments = {
                    daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_READ, daxa::ImageViewType::REGULAR_2D, output_image_view_a),
                    daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, output_image_view_b),
                },
                .task = [output_image_view_a, output_image_view_b](daxa::TaskInterface const &ti) {
                    auto &recorder = ti.recorder;
                    auto size_a = ti.device.info_image(ti.get(output_image_view_a).ids[0]).value().size;
                    auto size_b = ti.device.info_image(ti.get(output_image_view_b).ids[0]).value().size;
                    auto size = daxa_i32vec2{
                        std::min(static_cast<int32_t>(size_a.x), static_cast<int32_t>(size_b.x)),
                        std::min(static_cast<int32_t>(size_a.y), static_cast<int32_t>(size_b.y)),
                    };
                    recorder.blit_image_to_image({
                        .src_image = ti.get(output_image_view_a).ids[0],
                        .src_image_layout = ti.get(output_image_view_a).layout,
                        .dst_image = ti.get(output_image_view_b).ids[0],
                        .dst_image_layout = ti.get(output_image_view_b).layout,
                        .src_offsets = {{{0, 0, 0}, {size.x, size.y, 1}}},
                        .dst_offsets = {{{0, 0, 0}, {size.x, size.y, 1}}},
                        .filter = daxa::Filter::NEAREST,
                    });
                },
                .name = "resize_blit",
            });
            temp_task_graph.submit({});
            temp_task_graph.complete({});
            temp_task_graph.execute({});
            pass.buffer = std::move(new_buffer);
        }
        pass.recording_buffer_view = pass.buffer.task_resources.history_resource;
        task_graph.use_persistent_image(pass.buffer.task_resources.output_resource);
        task_graph.use_persistent_image(pass.buffer.task_resources.history_resource);
    }

    for (auto &pass : cube_passes) {
        if (first_record_after_load) {
            pass.buffer = {};
        }
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
        auto uses = std::vector<daxa::TaskAttachmentInfo>{};
        uses.push_back(daxa::inl_attachment(daxa::TaskBufferAccess::FRAGMENT_SHADER_READ, task_input_buffer));
        for (auto const &input : pass.inputs) {
            if (input.type == ShaderPassInputType::CUBE || input.type == ShaderPassInputType::CUBE_TEXTURE) {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::CUBE, get_resource_view_slice(input)));
            } else if (input.type == ShaderPassInputType::VOLUME_TEXTURE) {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_3D, get_resource_view_slice(input)));
            } else {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_2D, get_resource_view_slice(input)));
            }
        }
        auto pipeline = pass.pipeline;
        auto inputs = pass.inputs;
        auto output_view = pass.buffer.task_resources.output_resource.view();
        uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D, output_view));
        task_graph.add_task({
            .attachments = uses,
            .task = [this, &pass, task_input_buffer, get_resource_view_slice, pipeline, inputs, output_view](daxa::TaskInterface const &ti) {
                auto &cmd_list = ti.recorder;
                auto input_images = InputImages{};
                auto size = ti.device.info_image(ti.get(output_view).ids[0]).value().size;
                for (auto const &input : pass.inputs) {
                    input_images.Channel[input.channel] = ti.get(get_resource_view_slice(input)).view_ids[0];
                    input_images.Channel_sampler[input.channel] = input.sampler;
                }
                ShaderToyTask_record(
                    pipeline,
                    cmd_list,
                    daxa_device.get_device_address(ti.get(task_input_buffer).ids[0]).value(),
                    input_images,
                    ti.get(output_view).ids[0],
                    daxa_u32vec2{size.x, size.y});
                pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
            },
            .name = std::string("buffer task ") + pass.name,
        });
        pass.recording_buffer_view = pass.buffer.task_resources.output_resource;
        if (pass.needs_mipmap) {
            for (uint32_t mip = 0; mip < MAX_MIP - 1; ++mip) {
                task_graph.add_task({
                    .attachments = {
                        daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_READ, daxa::ImageViewType::REGULAR_2D, output_view.view({.base_mip_level = mip})),
                        daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, output_view.view({.base_mip_level = mip + 1})),
                    },
                    .task = [mip](daxa::TaskInterface ti) {
                        auto &recorder = ti.recorder;
                        do_blit(ti, ti.get(daxa::TaskImageAttachmentIndex{0}).ids[0], ti.get(daxa::TaskImageAttachmentIndex{1}).ids[0], mip);
                    },
                    .name = std::string("mip map ") + std::to_string(mip),
                });
            }
        }
    }

    for (auto &pass : cube_passes) {
        auto uses = std::vector<daxa::TaskAttachmentInfo>{};
        uses.push_back(daxa::inl_attachment(daxa::TaskBufferAccess::FRAGMENT_SHADER_READ, task_input_buffer));
        for (auto const &input : pass.inputs) {
            if (input.type == ShaderPassInputType::CUBE || input.type == ShaderPassInputType::CUBE_TEXTURE) {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::CUBE, get_resource_view_slice(input)));
            } else if (input.type == ShaderPassInputType::VOLUME_TEXTURE) {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_3D, get_resource_view_slice(input)));
            } else {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_2D, get_resource_view_slice(input)));
            }
        }
        auto face_views = std::array<daxa::TaskImageView, 6>{};
        for (uint32_t i = 0; i < 6; ++i) {
            face_views[i] = pass.buffer.task_resources.output_resource.view().view({.base_array_layer = i});
            uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D, face_views[i]));
        }
        auto pipeline = pass.pipeline;
        auto inputs = pass.inputs;
        auto output_view = pass.buffer.task_resources.output_resource.view();
        task_graph.add_task({
            .attachments = uses,
            .task = [this, &pass, task_input_buffer, get_resource_view_slice, pipeline, inputs, output_view, face_views](daxa::TaskInterface const &ti) {
                auto &cmd_list = ti.recorder;
                auto input_images = InputImages{};
                auto size = ti.device.info_image(ti.get(output_view).ids[0]).value().size;
                for (auto const &input : pass.inputs) {
                    input_images.Channel[input.channel] = ti.get(get_resource_view_slice(input)).view_ids[0];
                    input_images.Channel_sampler[input.channel] = input.sampler;
                }
                for (uint32_t i = 0; i < 6; ++i) {
                    ShaderToyCubeTask_record(
                        pipeline,
                        cmd_list,
                        daxa_device.get_device_address(ti.get(task_input_buffer).ids[0]).value(),
                        input_images,
                        ti.get(face_views[i]).view_ids[0],
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
        auto uses = std::vector<daxa::TaskAttachmentInfo>{};
        uses.push_back(daxa::inl_attachment(daxa::TaskBufferAccess::FRAGMENT_SHADER_READ, task_input_buffer));
        for (auto const &input : pass.inputs) {
            if (input.type == ShaderPassInputType::CUBE || input.type == ShaderPassInputType::CUBE_TEXTURE) {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::CUBE, get_resource_view_slice(input)));
            } else if (input.type == ShaderPassInputType::VOLUME_TEXTURE) {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_3D, get_resource_view_slice(input)));
            } else {
                uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED, daxa::ImageViewType::REGULAR_2D, get_resource_view_slice(input)));
            }
        }
        auto pipeline = pass.pipeline;
        auto inputs = pass.inputs;
        auto output_view = viewport_render_image;
        uses.push_back(daxa::inl_attachment(daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D, output_view));
        task_graph.add_task({
            .attachments = uses,
            .task = [this, &pass, task_input_buffer, get_resource_view_slice, pipeline, inputs, output_view](daxa::TaskInterface const &ti) {
                auto &cmd_list = ti.recorder;
                auto input_images = InputImages{};
                auto size = ti.device.info_image(ti.get(output_view).ids[0]).value().size;
                for (auto const &input : pass.inputs) {
                    input_images.Channel[input.channel] = ti.get(get_resource_view_slice(input)).view_ids[0];
                    input_images.Channel_sampler[input.channel] = input.sampler;
                }
                ShaderToyTask_record(
                    pipeline,
                    cmd_list,
                    daxa_device.get_device_address(ti.get(task_input_buffer).ids[0]).value(),
                    input_images,
                    ti.get(output_view).ids[0],
                    daxa_u32vec2{size.x, size.y});
            },
            .name = "image task",
        });
        if (pass.needs_mipmap) {
            for (uint32_t mip = 0; mip < MAX_MIP - 1; ++mip) {
                task_graph.add_task({
                    .attachments = {
                        daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_READ, daxa::ImageViewType::REGULAR_2D, output_view.view({.base_mip_level = mip})),
                        daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, output_view.view({.base_mip_level = mip + 1})),
                    },
                    .task = [mip](daxa::TaskInterface ti) {
                        do_blit(ti, ti.get(daxa::TaskImageAttachmentIndex{0}).ids[0], ti.get(daxa::TaskImageAttachmentIndex{1}).ids[0], mip);
                    },
                    .name = std::string("mip map ") + std::to_string(mip),
                });
            }
        }
    }

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
    auto task_image = daxa::TaskImage({.name = path});
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
            temp_format = daxa::Format::UNDEFINED;
        }
    }
    auto image_id = daxa_device.create_image({
        .dimensions = 2,
        .format = temp_format,
        .size = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = "texture",
    });
    task_image.set_images({.images = std::array{image_id}});

    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    temp_task_graph.use_persistent_image(task_image);
    temp_task_graph.add_task({
        .attachments = {
            daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, task_image),
        },
        .task = [this, temp_data, size_x, size_y, image_id, pixel_size_bytes](daxa::TaskInterface task_runtime) {
            auto staging_buffer = daxa_device.create_buffer({
                .size = static_cast<uint32_t>(size_x * size_y * pixel_size_bytes),
                .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                .name = "staging_buffer",
            });
            auto *buffer_ptr = daxa_device.get_host_address_as<uint8_t>(staging_buffer).value();
            memcpy(buffer_ptr, temp_data, size_x * size_y * pixel_size_bytes);
            auto &cmd_list = task_runtime.recorder;
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
    auto task_image = daxa::TaskImage({.name = path});
    replace_all(path, "/media/a/", "media/images/");
    stbi_set_flip_vertically_on_load(0);
    auto *temp_data = stbi_load(path.c_str(), &size_x, &size_y, &channel_n, 4);
    auto image_id = daxa_device.create_image({
        .dimensions = 2,
        .format = daxa::Format::R8G8B8A8_UNORM,
        .size = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
        .array_layer_count = 6,
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = "cube texture",
    });
    task_image.set_images({.images = std::array{image_id}});
    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    temp_task_graph.use_persistent_image(task_image);
    auto loaded_buffers = std::vector<stbi_uc *>{};
    loaded_buffers.reserve(6);
    loaded_buffers.push_back(temp_data);
    for (uint32_t i = 1; i < 6; ++i) {
        auto temp_path = std::filesystem::path(path);
        auto new_path = temp_path.parent_path() / std::filesystem::path(temp_path.stem().string() + "_" + std::to_string(i) + temp_path.extension().string());
        auto *temp_data = stbi_load(new_path.string().c_str(), &size_x, &size_y, &channel_n, 4);
        loaded_buffers.push_back(temp_data);
    }
    for (uint32_t i = 0; i < 6; ++i) {
        temp_task_graph.add_task({
            .attachments = {
                daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, task_image),
            },
            .task = [this, &loaded_buffers, size_x, size_y, image_id, i](daxa::TaskInterface task_runtime) {
                auto staging_buffer = daxa_device.create_buffer({
                    .size = static_cast<uint32_t>(size_x * size_y * 4),
                    .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                    .name = "staging_buffer",
                });
                auto *buffer_ptr = daxa_device.get_host_address_as<uint8_t>(staging_buffer).value();
                memcpy(buffer_ptr, loaded_buffers[i], size_x * size_y * 4);
                auto &cmd_list = task_runtime.recorder;
                cmd_list.pipeline_barrier({
                    .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
                });
                cmd_list.destroy_buffer_deferred(staging_buffer);
                cmd_list.copy_buffer_to_image({
                    .buffer = staging_buffer,
                    .image = image_id,
                    .image_slice = {
                        .base_array_layer = i,
                    },
                    .image_extent = {static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), 1},
                });
            },
            .name = "upload_user_texture",
        });
    }
    temp_task_graph.submit({});
    temp_task_graph.complete({});
    temp_task_graph.execute({});
    task_textures.push_back(task_image);
    for (auto *loaded_buffer_ptr : loaded_buffers) {
        stbi_image_free(loaded_buffer_ptr);
    }
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

    auto task_image = daxa::TaskImage({.name = name});
    auto image_id = daxa_device.create_image({
        .dimensions = 3,
        .format = num_channels == 1 ? daxa::Format::R8_UNORM : daxa::Format::R8G8B8A8_UNORM,
        .size = {32, 32, 32},
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = name,
    });
    task_image.set_images({.images = std::array{image_id}});

    daxa::TaskGraph temp_task_graph = daxa::TaskGraph({
        .device = daxa_device,
        .name = "temp_task_graph",
    });
    temp_task_graph.use_persistent_image(task_image);
    temp_task_graph.add_task({
        .attachments = {
            daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, task_image),
        },
        .task = [this, &random_numbers, image_id](daxa::TaskInterface task_runtime) {
            auto staging_buffer = daxa_device.create_buffer({
                .size = static_cast<uint32_t>(random_numbers.size()),
                .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                .name = "staging_buffer",
            });
            auto *buffer_ptr = daxa_device.get_host_address_as<uint8_t>(staging_buffer).value();
            memcpy(buffer_ptr, random_numbers.data(), random_numbers.size());
            auto &cmd_list = task_runtime.recorder;
            cmd_list.pipeline_barrier({
                .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            });
            cmd_list.destroy_buffer_deferred(staging_buffer);
            cmd_list.copy_buffer_to_image({
                .buffer = staging_buffer,
                .image = image_id,
                .image_extent = {32, 32, 32},
            });
        },
        .name = "upload_user_texture",
    });
    temp_task_graph.submit({});
    temp_task_graph.complete({});
    temp_task_graph.execute({});
    task_textures.push_back(task_image);

    return std::pair<daxa::ImageId, size_t>{image_id, task_image_index};
}

void Viewport::load_shadertoy_json(nlohmann::json json) {
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
    replace_all(common_code, "\\n", "\n");
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
        auto &name = renderpass["name"];
        auto &pass_type = renderpass["type"];
        auto pipeline_name = std::string{name};
        // Skip unknown pass type
        if (pass_type != "image" && pass_type != "buffer" && pass_type != "cubemap") {
            continue;
        }
        user_code.contents += "#if defined(_DESKTOP_SHADERTOY_USER_PASS" + std::to_string(pass_i) + ")\n";
        user_code.contents += "#include <" + pipeline_name + "_inputs>\n";
        user_code.contents += "#include <" + pipeline_name + ">\n";
        user_code.contents += "#endif\n";
    }
    pipeline_manager.add_virtual_file(user_code);

    pass_i = size_t{0};
    for (auto &renderpass : renderpasses) {
        ++pass_i;
        auto &code = renderpass["code"];
        auto &description = renderpass["description"];
        auto &inputs = renderpass["inputs"];
        auto &name = renderpass["name"];
        auto &outputs = renderpass["outputs"];
        auto &pass_type = renderpass["type"];
        auto pipeline_name = std::string{name};
        // Skip unknown pass type
        if (pass_type != "image" && pass_type != "buffer" && pass_type != "cubemap") {
            continue;
        }

        auto temp_inputs = std::vector<ShaderPassInput>{};

        auto pass_inputs_file = daxa::VirtualFileInfo{
            .name = pipeline_name + "_inputs",
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

        pass_inputs_file.contents += "#define iChannel0 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[0], daxa_push_constant.input_images.Channel_sampler[0], 0)\n";
        pass_inputs_file.contents += "#define iChannel1 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[1], daxa_push_constant.input_images.Channel_sampler[1], 0)\n";
        pass_inputs_file.contents += "#define iChannel2 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[2], daxa_push_constant.input_images.Channel_sampler[2], 0)\n";
        pass_inputs_file.contents += "#define iChannel3 CombinedImageSampler2D(daxa_push_constant.input_images.Channel[3], daxa_push_constant.input_images.Channel_sampler[3], 0)\n";

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

            auto channel_str = std::to_string(uint32_t{input["channel"]});
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

            if (type == "cubemap" && !id_map.contains(id)) {
                load_texture_type(ShaderPassInputType::CUBE_TEXTURE, [](void *self, std::string const &path) { return static_cast<Viewport *>(self)->load_cube_texture(path); });
            } else if (type == "buffer" || type == "cubemap") {
                auto input_copy = id_map[id];
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
                load_texture_type(ShaderPassInputType::TEXTURE, [](void *self, std::string const &path) { return static_cast<Viewport *>(self)->load_texture(path); });
            } else if (type == "volume") {
                load_texture_type(ShaderPassInputType::VOLUME_TEXTURE, [](void *self, std::string const &id) { return static_cast<Viewport *>(self)->load_volume_texture(id); });
            }
            pass_inputs_file.contents += std::string{"#undef iChannel"} + channel_str + "\n" + std::string{"#define iChannel"} + channel_str + " " + image_type + "(daxa_push_constant.input_images.Channel[" + channel_str + "], daxa_push_constant.input_images.Channel_sampler[" + channel_str + "]" + extra + ")\n";
        }

        auto pass_file = daxa::VirtualFileInfo{
            .name = pipeline_name,
            .contents = code,
        };

        replace_all(pass_file.contents, "\\n", "\n");
        pipeline_manager.add_virtual_file(pass_file);
        pipeline_manager.add_virtual_file(pass_inputs_file);

        auto extra_defines = std::vector<daxa::ShaderDefine>{};
        auto pass_format = daxa::Format::R32G32B32A32_SFLOAT;
        if (pass_type == "image") {
            pass_format = daxa::Format::R16G16B16A16_SFLOAT;
            extra_defines.push_back({.name = "MAIN_IMAGE", .value = "1"});
        } else if (pass_type == "cubemap") {
            pass_format = daxa::Format::R16G16B16A16_SFLOAT;
            extra_defines.push_back({.name = "CUBEMAP", .value = "1"});
        }
        extra_defines.push_back({.name = "_DESKTOP_SHADERTOY_USER_PASS" + std::to_string(pass_i), .value = "1"});

        auto compile_result = pipeline_manager.add_raster_pipeline({
            .vertex_shader_info = daxa::ShaderCompileInfo{.source = daxa::ShaderFile{"app/viewport.glsl"}, .compile_options{.defines = extra_defines}},
            .fragment_shader_info = daxa::ShaderCompileInfo{.source = daxa::ShaderFile{"app/viewport.glsl"}, .compile_options{.defines = extra_defines}},
            .color_attachments = {{
                .format = pass_format,
            }},
            .push_constant_size = sizeof(ShaderToyPush),
            .name = pipeline_name,
        });
        if (compile_result.is_err() || !compile_result.value()->is_valid()) {
            core::log_error(pipeline_name + ": " + compile_result.message());
            return;
        }

        if (pass_type == "image") {
            new_image_pass = {name, temp_inputs, compile_result.value()};
        } else if (pass_type == "buffer") {
            new_buffer_passes.emplace_back(name, temp_inputs, compile_result.value());
        } else if (pass_type == "cubemap") {
            new_cube_passes.emplace_back(name, temp_inputs, compile_result.value());
        }
    }

    {
        auto mip_sampler0 = samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::CLAMP) * 3];
        auto mip_sampler1 = samplers[static_cast<size_t>(ShaderToyFilter::MIPMAP) + static_cast<size_t>(ShaderToyWrap::REPEAT) * 3];
        auto pass_needs_mipmaps = [&](auto &pass) {
            for (auto &input : pass.inputs) {
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

    buffer_passes = std::move(new_buffer_passes);
    cube_passes = std::move(new_cube_passes);
    image_pass = std::move(new_image_pass);

    first_record_after_load = true;

    reset();
}
