#include <RmlUi/Core/Types.h>
#include <daxa/daxa.inl>

#include "render_daxa.hpp"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Platform.h>

#include <daxa/gpu_resources.hpp>
#include <daxa/types.hpp>
#include <utility>
#include <iostream>
#include <cassert>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

struct Vertex {
    daxa_f32vec2 pos;
    daxa_u32 col;
    daxa_f32vec2 tex;
};
static_assert(sizeof(Vertex) == sizeof(Rml::Vertex));

DAXA_DECL_BUFFER_PTR(Vertex)

struct Push {
    daxa_f32mat4x4 projection;
    daxa_BufferPtr(Vertex) vbuffer_ptr;
    daxa_BufferPtr(daxa_u32) ibuffer_ptr;
    daxa_ImageViewId texture0_id;
    daxa_SamplerId sampler0_id;
    daxa_f32vec2 pos_offset;
};

constexpr auto SHADER_COMMON = R"glsl(
#include <daxa/daxa.inl>

struct Vertex {
    daxa_f32vec2 pos;
    daxa_u32 col;
    daxa_f32vec2 tex;
};
DAXA_DECL_BUFFER_PTR(Vertex)

struct Push {
    daxa_f32mat4x4 projection;
    daxa_BufferPtr(Vertex) vbuffer_ptr;
    daxa_BufferPtr(daxa_u32) ibuffer_ptr;
    daxa_ImageViewId texture0_id;
    daxa_SamplerId sampler0_id;
    daxa_f32vec2 pos_offset;
};
DAXA_DECL_PUSH_CONSTANT(Push, push)

daxa_f32vec4 srgb_to_linear(daxa_f32vec4 srgb) {
    return daxa_f32vec4(pow(srgb.rgb, daxa_f32vec3(2.2)), srgb.a);
}
daxa_f32vec4 linear_to_srgb(daxa_f32vec4 srgb) {
    return daxa_f32vec4(pow(srgb.rgb, daxa_f32vec3(1.0 / 2.2)), srgb.a);
}
)glsl";

RenderInterface_Daxa::RenderInterface_Daxa(daxa::Device device, daxa::Format format) : device(std::move(device)) {
    pipeline_manager = daxa::PipelineManager({
        .device = this->device,
        .shader_compile_options = {
            .root_paths = {
                DAXA_SHADER_INCLUDE_DIR,
            },
            .language = daxa::ShaderLanguage::GLSL,
        },
        .name = "pipeline_manager",
    });
    auto compile_result = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo{
        .vertex_shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderCode{std::string{SHADER_COMMON} + R"glsl(
                layout(location = 0) out struct {
                    daxa_f32vec4 Color;
                    daxa_f32vec2 UV;
                } Out;

                void main() {
                    Vertex vert = deref(push.vbuffer_ptr[gl_VertexIndex]);

                    daxa_f32vec2 aPos = vert.pos + push.pos_offset;
                    daxa_f32vec2 aUV = vert.tex;
                    daxa_u32 aColor = vert.col;

                    Out.Color.r = ((aColor >> 0x00) & 0xff) * 1.0 / 255.0;
                    Out.Color.g = ((aColor >> 0x08) & 0xff) * 1.0 / 255.0;
                    Out.Color.b = ((aColor >> 0x10) & 0xff) * 1.0 / 255.0;
                    Out.Color.a = ((aColor >> 0x18) & 0xff) * 1.0 / 255.0;
                    Out.Color = srgb_to_linear(Out.Color);
                    Out.UV = aUV;

                    gl_Position = push.projection * vec4(aPos, 0, 1);
                    gl_Position.z += 0.5;
                }
            )glsl"},
            .compile_options = {
                .language = daxa::ShaderLanguage::GLSL,
                .enable_debug_info = true,
            },
        },
        .fragment_shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderCode{std::string{SHADER_COMMON} + R"glsl(
                layout(location = 0) out daxa_f32vec4 fColor;
                layout(location = 0) in struct {
                    daxa_f32vec4 Color;
                    daxa_f32vec2 UV;
                } In;
                void main() {
                    vec4 tex_color = texture(daxa_sampler2D(push.texture0_id, push.sampler0_id), In.UV.st).rgba;
                    fColor = linear_to_srgb(In.Color.rgba * tex_color);
                }
            )glsl"},
            .compile_options = daxa::ShaderCompileOptions{
                .language = daxa::ShaderLanguage::GLSL,
                .enable_debug_info = true,
            },
        },
        .color_attachments = {{
            .format = format,
            .blend = daxa::BlendInfo{
                .src_color_blend_factor = daxa::BlendFactor::SRC_ALPHA,
                .dst_color_blend_factor = daxa::BlendFactor::ONE_MINUS_SRC_ALPHA,
            },
        }},
        .raster = {},
        .push_constant_size = sizeof(Push),
        .name = "imgui_pipeline",
    });

    assert(compile_result.is_ok());

    raster_pipeline = compile_result.value();

    recreate_vbuffer(4096);
    recreate_ibuffer(4096);

    this->default_sampler = this->device.create_sampler({.name = "rml default sampler"});

    auto source_bytes = 0xffffffff;
    GenerateTexture(*reinterpret_cast<Rml::TextureHandle *>(&default_texture), reinterpret_cast<Rml::byte *>(&source_bytes), {1, 1});
}

RenderInterface_Daxa::~RenderInterface_Daxa() {
    device.wait_idle();
    device.collect_garbage();
    device.destroy_image(default_texture);
    device.destroy_sampler(default_sampler);
    device.destroy_buffer(vbuffer);
    device.destroy_buffer(ibuffer);
}

void RenderInterface_Daxa::recreate_vbuffer(size_t vbuffer_new_size) {
    vbuffer = device.create_buffer({
        .size = static_cast<uint32_t>(vbuffer_new_size),
        .name = std::string("rml vertex buffer"),
    });
}

void RenderInterface_Daxa::recreate_ibuffer(size_t ibuffer_new_size) {
    ibuffer = device.create_buffer({
        .size = static_cast<uint32_t>(ibuffer_new_size),
        .name = std::string("rml index buffer"),
    });
}

void RenderInterface_Daxa::begin_frame(daxa::ImageId target_image, daxa::CommandRecorder &recorder) {
    using namespace std::literals;

    auto target_image_extent = device.info_image(target_image).value().size;

    projection = Rml::Matrix4f::ProjectOrtho(0, (float)target_image_extent.x, 0, (float)target_image_extent.y, -10000, 10000);
    SetTransform(nullptr);
    bound_texture = std::bit_cast<Rml::TextureHandle>(default_texture);
}

void RenderInterface_Daxa::end_frame(daxa::ImageId target_image, daxa::CommandRecorder &recorder) {
    auto vbuffer_current_size = device.info_buffer(vbuffer).value().size;
    auto vbuffer_needed_size = vertex_cache.size() * sizeof(Vertex);
    auto ibuffer_current_size = device.info_buffer(ibuffer).value().size;
    auto ibuffer_needed_size = index_cache.size() * sizeof(int);

    if (!this->image_uploads.empty()) {
        auto const upload_size = image_upload_data.size() * sizeof(uint8_t);

        auto texture_staging_buffer = this->device.create_buffer({
            .size = static_cast<uint32_t>(upload_size),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        });
        recorder.destroy_buffer_deferred(texture_staging_buffer);

        auto *staging_buffer_data = this->device.get_host_address_as<Rml::byte>(texture_staging_buffer).value();
        std::memcpy(staging_buffer_data, image_upload_data.data(), upload_size);

        for (auto const &image_upload : image_uploads) {
            recorder.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::HOST_WRITE,
                .dst_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
                .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .image_slice = {
                    .base_mip_level = 0,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_id = image_upload.image_id,
            });
        }
        for (auto const &image_upload : image_uploads) {
            recorder.copy_buffer_to_image({
                .buffer = texture_staging_buffer,
                .buffer_offset = image_upload.data,
                .image = image_upload.image_id,
                .image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .image_slice = {
                    .mip_level = 0,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_offset = {0, 0, 0},
                .image_extent = {static_cast<uint32_t>(image_upload.size.x), static_cast<uint32_t>(image_upload.size.y), 1},
            });
        }
        for (auto const &image_upload : image_uploads) {
            recorder.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::TRANSFER_WRITE,
                .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
                .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
                .image_slice = {
                    .base_mip_level = 0,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_id = image_upload.image_id,
            });
        }
    }

    if (vbuffer_needed_size > vbuffer_current_size) {
        auto vbuffer_new_size = vbuffer_needed_size + 4096;
        device.destroy_buffer(vbuffer);
        recreate_vbuffer(vbuffer_new_size);
    }
    if (ibuffer_needed_size > ibuffer_current_size) {
        auto ibuffer_new_size = ibuffer_needed_size + 4096;
        device.destroy_buffer(ibuffer);
        recreate_ibuffer(ibuffer_new_size);
    }

    auto staging_vbuffer = device.create_buffer({
        .size = static_cast<uint32_t>(vbuffer_needed_size),
        .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .name = std::string("rml vertex staging buffer"),
    });
    auto *vtx_dst = device.get_host_address_as<Rml::Vertex>(staging_vbuffer).value();
    std::copy(vertex_cache.begin(), vertex_cache.end(), vtx_dst);
    recorder.destroy_buffer_deferred(staging_vbuffer);
    auto staging_ibuffer = device.create_buffer({
        .size = static_cast<uint32_t>(ibuffer_needed_size),
        .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .name = std::string("rml index staging buffer"),
    });
    auto *idx_dst = device.get_host_address_as<int>(staging_ibuffer).value();
    std::copy(index_cache.begin(), index_cache.end(), idx_dst);
    recorder.destroy_buffer_deferred(staging_ibuffer);
    recorder.pipeline_barrier({
        .src_access = daxa::AccessConsts::HOST_WRITE,
        .dst_access = daxa::AccessConsts::TRANSFER_READ,
    });
    recorder.copy_buffer_to_buffer({
        .src_buffer = staging_ibuffer,
        .dst_buffer = ibuffer,
        .size = ibuffer_needed_size,
    });
    recorder.copy_buffer_to_buffer({
        .src_buffer = staging_vbuffer,
        .dst_buffer = vbuffer,
        .size = vbuffer_needed_size,
    });
    recorder.pipeline_barrier({
        .src_access = daxa::AccessConsts::TRANSFER_WRITE,
        .dst_access = daxa::AccessConsts::VERTEX_SHADER_READ | daxa::AccessConsts::INDEX_INPUT_READ,
    });

    auto target_image_extent = device.info_image(target_image).value().size;

    auto default_scissor = daxa::Rect2D{.x = 0, .y = 0, .width = target_image_extent.x, .height = target_image_extent.y};

    auto render_recorder = std::move(recorder).begin_renderpass({
        .color_attachments = std::array{daxa::RenderAttachmentInfo{.image_view = target_image.default_view(), .load_op = daxa::AttachmentLoadOp::LOAD}},
        .render_area = default_scissor,
    });

    render_recorder.set_pipeline(*raster_pipeline);
    render_recorder.set_index_buffer({
        .id = ibuffer,
        .offset = 0,
        .index_type = daxa::IndexType::uint32,
    });

    auto push = Push{};
    push.vbuffer_ptr = device.get_device_address(vbuffer).value();
    push.ibuffer_ptr = device.get_device_address(ibuffer).value();

    for (auto const &draw_handle : draw_order) {
        auto const &draw = draws[draw_handle - 1];
        if (draw.indices.empty()) {
            continue;
        }
        push.projection = *reinterpret_cast<daxa_f32mat4x4 const *>(&draw.transform);
        push.pos_offset = {draw.translation.x, draw.translation.y};
        auto texture_image = draw.actual_image;
        push.texture0_id = texture_image.default_view();
        push.sampler0_id = default_sampler;
        if (draw.scissor) {
            render_recorder.set_scissor(*draw.scissor);
        } else {
            render_recorder.set_scissor(default_scissor);
        }
        assert(device.is_id_valid(texture_image));
        assert(device.is_id_valid(std::bit_cast<daxa::ImageViewId>(push.texture0_id)));
        assert(device.is_id_valid(std::bit_cast<daxa::SamplerId>(push.sampler0_id)));
        render_recorder.push_constant(push);
        render_recorder.draw_indexed({
            .index_count = static_cast<uint32_t>(draw.indices.size()),
            .first_index = static_cast<uint32_t>(draw.index_offset),
            .vertex_offset = static_cast<int32_t>(draw.vertex_offset),
        });
    }

    recorder = std::move(render_recorder).end_renderpass();

    image_uploads.clear();
    image_upload_data.clear();
    draw_order.clear();

    vertex_cache_offset = 0;
    index_cache_offset = 0;
    scissor_enabled = false;

    for (auto handle : deferred_draw_releases) {
        ReleaseCompiledGeometry(handle);
    }
    deferred_draw_releases.clear();
}

void RenderInterface_Daxa::RenderGeometry(Rml::Vertex *vertices, int num_vertices, int *indices, int num_indices, const Rml::TextureHandle texture, const Rml::Vector2f &translation) {
    if (vertices == nullptr) {
        return;
    }

    auto geometry = CompileGeometry(vertices, num_vertices, indices, num_indices, texture);
    if (geometry != 0) {
        RenderCompiledGeometry(geometry, translation);
        deferred_draw_releases.push_back(geometry);
    }
}

auto RenderInterface_Daxa::CompileGeometry(Rml::Vertex *vertices, int num_vertices, int *indices, int num_indices, const Rml::TextureHandle texture) -> Rml::CompiledGeometryHandle {
    if (vertices == nullptr) {
        return {};
    }

    auto draw = Draw{
        .vertices = std::vector<Rml::Vertex>(vertices, vertices + num_vertices),
        .indices = std::vector<int>(indices, indices + num_indices),
        .texture = texture,
    };

    size_t draw_id = 0;
    if (draw_free_list.empty()) {
        draw_id = draws.size() + 1;
        draws.emplace_back(draw);
    } else {
        draw_id = draw_free_list.top();
        draw_free_list.pop();
        draws.at(draw_id - 1) = draw;
    }

    return draw_id;
}

void RenderInterface_Daxa::RenderCompiledGeometry(Rml::CompiledGeometryHandle handle, const Rml::Vector2f &translation) {
    auto &draw = draws.at(handle - 1);
    draw.translation = translation;
    auto num_vertices = draw.vertices.size();
    draw.vertex_offset = vertex_cache_offset;
    auto necessary_size = draw.vertex_offset + num_vertices;
    if (vertex_cache.size() < necessary_size) {
        vertex_cache.resize(necessary_size);
    }
    std::copy(draw.vertices.begin(), draw.vertices.end(), vertex_cache.begin() + draw.vertex_offset);
    auto num_indices = draw.indices.size();
    draw.index_offset = index_cache_offset;
    auto index_necessary_size = draw.index_offset + num_indices;
    if (index_cache.size() < index_necessary_size) {
        index_cache.resize(index_necessary_size);
    }
    std::copy(draw.indices.begin(), draw.indices.end(), index_cache.begin() + draw.index_offset);
    draw.transform = transform;
    if (scissor_enabled) {
        draw.scissor = current_scissor;
    }

    if (draw.texture == 0) {
        draw.actual_image = default_texture;
    } else {
        if (draw.texture != -1) {
            bound_texture = draw.texture;
        }
        draw.actual_image = std::bit_cast<daxa::ImageId>(bound_texture);
    }

    draw_order.push_back(handle);
    vertex_cache_offset += num_vertices;
    index_cache_offset += num_indices;
}

void RenderInterface_Daxa::ReleaseCompiledGeometry(Rml::CompiledGeometryHandle handle) {
    draws.at(handle - 1) = {};
    draw_free_list.push(handle);
}

void RenderInterface_Daxa::EnableScissorRegion(bool enable) {
    scissor_enabled = enable && !transform_enabled;
    assert(!transform_enabled);
}

void RenderInterface_Daxa::SetScissorRegion(int x, int y, int width, int height) {
    assert(!transform_enabled);
    current_scissor = daxa::Rect2D{
        .x = x,
        .y = y,
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
    };
    if (transform_enabled) {
        // // clear the stencil buffer
        // glStencilMask(GLuint(-1));
        // glClear(GL_STENCIL_BUFFER_BIT);
        // // fill the stencil buffer
        // glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        // glDepthMask(GL_FALSE);
        // glStencilFunc(GL_NEVER, 1, GLuint(-1));
        // glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
        // float fx = (float)x;
        // float fy = (float)y;
        // float fwidth = (float)width;
        // float fheight = (float)height;
        // // draw transformed quad
        // GLfloat vertices[] = {fx, fy, 0, fx, fy + fheight, 0, fx + fwidth, fy + fheight, 0, fx + fwidth, fy, 0};
        // glDisableClientState(GL_COLOR_ARRAY);
        // glVertexPointer(3, GL_FLOAT, 0, vertices);
        // GLushort indices[] = {1, 2, 0, 3};
        // glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, indices);
        // glEnableClientState(GL_COLOR_ARRAY);
        // // prepare for drawing the real thing
        // glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        // glDepthMask(GL_TRUE);
        // glStencilMask(0);
        // glStencilFunc(GL_EQUAL, 1, GLuint(-1));
    }
}

auto RenderInterface_Daxa::LoadTexture(Rml::TextureHandle &texture_handle, Rml::Vector2i &texture_dimensions, const Rml::String &source) -> bool {
    auto size_x = 0;
    auto size_y = 0;
    stbi_set_flip_vertically_on_load(0);
    auto image_data = stbi_load(source.c_str(), &size_x, &size_y, nullptr, 4);
    if (!image_data || size_x == 0 || size_y == 0) {
        return false;
    }
    auto result = GenerateTexture(texture_handle, reinterpret_cast<Rml::byte *>(image_data), {size_x, size_y});
    stbi_image_free(image_data);
    texture_dimensions.x = size_x;
    texture_dimensions.y = size_y;

    return result;
}

auto RenderInterface_Daxa::GenerateTexture(Rml::TextureHandle &texture_handle, const Rml::byte *source, const Rml::Vector2i &source_dimensions) -> bool {
    auto image_id = device.create_image({
        .format = daxa::Format::R8G8B8A8_SRGB,
        .size = {static_cast<uint32_t>(source_dimensions.x), static_cast<uint32_t>(source_dimensions.y), 1},
        .usage = daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        .name = "rml texture",
    });

    auto prev_byte_count = image_upload_data.size();

    image_uploads.push_back(ImageUpload{
        .image_id = image_id,
        .data = prev_byte_count,
        .size = source_dimensions,
    });

    auto const byte_count = static_cast<size_t>(4) * source_dimensions.x * source_dimensions.y;
    image_upload_data.resize(prev_byte_count + byte_count);
    auto in_bytes = std::span<Rml::byte const>{source, byte_count};
    std::copy(in_bytes.begin(), in_bytes.end(), image_upload_data.begin() + prev_byte_count);

    texture_handle = std::bit_cast<Rml::TextureHandle>(image_id);
    return true;
}

void RenderInterface_Daxa::ReleaseTexture(Rml::TextureHandle texture_handle) {
    device.destroy_image(std::bit_cast<daxa::ImageId>(texture_handle));
}

void RenderInterface_Daxa::SetTransform(const Rml::Matrix4f *new_transform) {
    transform_enabled = new_transform != nullptr;
    transform = projection * (transform_enabled ? *new_transform : Rml::Matrix4f::Identity());
}
