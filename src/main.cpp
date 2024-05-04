#include <app/viewport.hpp>
#include <app/resources.hpp>

#include <cstdint>
#include <daxa/daxa.hpp>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/utils/task_graph.hpp>

#include <fmt/format.h>

#include <ui/app_ui.hpp>

#include <stb_image.h>

#include <fstream>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

struct ShaderApp {
    daxa::Instance daxa_instance;
    daxa::Device daxa_device;
    AppUi ui;
    daxa::TaskGraph main_task_graph;
    daxa::TaskImage task_swapchain_image;

    Viewport viewport;

    ShaderApp();
    ~ShaderApp();

    ShaderApp(const ShaderApp &) = delete;
    ShaderApp(ShaderApp &&) = delete;
    auto operator=(const ShaderApp &) -> ShaderApp & = delete;
    auto operator=(ShaderApp &&) -> ShaderApp & = delete;

    void update();
    auto should_close() -> bool;
    void render();
    void download_shadertoy(std::string const &input);
    auto record_main_task_graph() -> daxa::TaskGraph;
};

namespace core {
    void log_error(std::string const &msg) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", msg.c_str());
    }
} // namespace core

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

ShaderApp::ShaderApp()
    : daxa_instance{daxa::create_instance({})},
      daxa_device{daxa_instance.create_device({.flags = {}, .name = "device"})},
      ui{daxa_device},
      viewport{daxa_device},
      task_swapchain_image{daxa::TaskImageInfo{.swapchain_image = true}} {
    ui.app_window.on_resize = [&]() {
        if (ui.app_window.size.x <= 0 || ui.app_window.size.y <= 0) {
            return;
        }
        ui.rml_context->Update();
        main_task_graph = record_main_task_graph();
        render();
    };
    ui.app_window.on_drop = [&](std::span<char const *> paths) {
        ui.buffer_panel.load_shadertoy_json(nlohmann::json::parse(std::ifstream(paths[0])));
    };
    ui.app_window.on_mouse_move = std::bind(&Viewport::on_mouse_move, &viewport, std::placeholders::_1, std::placeholders::_2);
    ui.app_window.on_mouse_button = std::bind(&Viewport::on_mouse_button, &viewport, std::placeholders::_1, std::placeholders::_2);
    ui.app_window.on_key = [&](int32_t key_id, int32_t action) {
        viewport.on_key(key_id, action);
    };

    ui.on_reset = std::bind(&Viewport::reset, &viewport);
    ui.on_toggle_pause = std::bind(&Viewport::on_toggle_pause, &viewport, std::placeholders::_1);

    ui.on_toggle_fullscreen = [&](bool is_fullscreen) {
        ui.app_window.set_fullscreen(is_fullscreen);
        main_task_graph = record_main_task_graph();
    };

    ui.on_download = [&](Rml::String const &rml_input) {
        download_shadertoy(rml_input);
    };

    ui.buffer_panel.load_shadertoy_json(nlohmann::json::parse(std::ifstream(resource_dir + "default-shader.json")));
}

ShaderApp::~ShaderApp() {
    daxa_device.wait_idle();
    daxa_device.collect_garbage();
}

void ShaderApp::update() {
    if (!ui.paused) {
        viewport.update();
    }
    ui.update(viewport.gpu_input.Time, viewport.last_known_fps);
}

void ShaderApp::render() {
    auto &app_window = ui.app_window;
    if (app_window.size.x <= 0 || app_window.size.y <= 0) {
        return;
    }
    auto const swapchain_image = app_window.swapchain.acquire_next_image();
    if (swapchain_image.is_empty()) {
        return;
    }
    task_swapchain_image.set_images({.images = {&swapchain_image, 1}});

    if (ui.buffer_panel.dirty) {
        viewport.load_shadertoy_json(ui.buffer_panel.get_shadertoy_json());
        main_task_graph = record_main_task_graph();
        ui.buffer_panel.dirty = false;
    }

    viewport.render();

    main_task_graph.execute({});
    daxa_device.collect_garbage();

    ++viewport.gpu_input.Frame;
}

auto ShaderApp::should_close() -> bool {
    return ui.should_close.load();
}

void ShaderApp::download_shadertoy(std::string const &input) {
    auto shader_id = input;
    auto slash_pos = shader_id.find_last_of('/');
    if (slash_pos != std::string::npos) {
        shader_id = shader_id.substr(slash_pos + 1);
    }

    char const *shaderToyDomain = "www.shadertoy.com";
    char const *shaderToyPort = "443";
    char const *shaderToyKey = "Bt8jhH";
    char const *shaderToyURI = "/api/v1/shaders/";
    char const *userAgent = BOOST_BEAST_VERSION_STRING;

    auto ioc = boost::beast::net::io_context{};

    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto ssl_ctx = boost::asio::ssl::context(boost::asio::ssl::context::method::sslv23_client);
    auto results = resolver.resolve(shaderToyDomain, shaderToyPort);
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ioc, ssl_ctx);
    boost::beast::get_lowest_layer(stream).connect(results.begin(), results.end());
    stream.handshake(boost::asio::ssl::stream_base::client);

    std::string requestURI = shaderToyURI + shader_id + "?key=" + shaderToyKey;
    auto req = boost::beast::http::request<boost::beast::http::string_body>{boost::beast::http::verb::post, requestURI, 10};
    req.set(boost::beast::http::field::host, shaderToyDomain);
    req.set(boost::beast::http::field::user_agent, userAgent);

    boost::beast::http::write(stream, req);
    boost::beast::flat_buffer buffer;
    boost::beast::http::response<boost::beast::http::dynamic_body> res;
    boost::beast::http::read(stream, buffer, res);
    boost::beast::error_code ec;
    stream.shutdown(ec);

    if (ec) {
        core::log_error(ec.message());
        return;
    }

    nlohmann::json json = nlohmann::json::parse(boost::beast::buffers_to_string(res.body().data()));
    if (json.contains("Error")) {
        auto error = std::string{json["Error"]};
        if (error == "Shader not found") {
            core::log_error("Failed to download from shadertoy: " + error + ". This is usually because the creator does not allow API downloads on their shader");
        } else {
            core::log_error("Failed to download from shadertoy: " + error);
        }
        return;
    }

    if (ui.settings.export_downloads) {
        auto f = std::ofstream("test-shader.json");
        f << std::setw(4) << json["Shader"];
    }

    ui.buffer_panel.load_shadertoy_json(json["Shader"]);
}

auto ShaderApp::record_main_task_graph() -> daxa::TaskGraph {
    auto viewport_size = daxa_f32vec2(ui.viewport_element->GetClientWidth(), ui.viewport_element->GetClientHeight());
    if (ui.is_fullscreen) {
        viewport_size = daxa_f32vec2{
            static_cast<daxa_f32>(ui.app_window.size.x),
            static_cast<daxa_f32>(ui.app_window.size.y),
        };
    }
    viewport.gpu_input.Resolution = daxa_f32vec3{
        static_cast<daxa_f32>(viewport_size.x),
        static_cast<daxa_f32>(viewport_size.y),
        1.0f,
    };
    viewport.gpu_input.ChannelResolution[0] = daxa_f32vec3{
        static_cast<daxa_f32>(viewport_size.x),
        static_cast<daxa_f32>(viewport_size.y),
        1.0f,
    };

    auto &app_window = ui.app_window;

    auto task_graph = daxa::TaskGraph(daxa::TaskGraphInfo{
        .device = daxa_device,
        .swapchain = app_window.swapchain,
        .name = "main_tg",
    });
    task_graph.use_persistent_image(task_swapchain_image);

    task_graph.add_task({
        .attachments = {
            daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, task_swapchain_image),
        },
        .task = [this](daxa::TaskInterface const &ti) {
            auto &recorder = ti.recorder;
            auto swapchain_image = ti.get(task_swapchain_image).ids[0];
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

    auto viewport_render_image = viewport.record(task_graph);

    task_graph.add_task({
        .attachments = {
            daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_READ, daxa::ImageViewType::REGULAR_2D, viewport_render_image),
            daxa::inl_attachment(daxa::TaskImageAccess::TRANSFER_WRITE, daxa::ImageViewType::REGULAR_2D, task_swapchain_image),
        },
        .task = [viewport_render_image, viewport_size, this](daxa::TaskInterface const &ti) {
            auto &recorder = ti.recorder;
            auto image_size = ti.device.info_image(ti.get(viewport_render_image).ids[0]).value().size;
            auto viewport_pos0 = daxa_f32vec2{
                ui.viewport_element->GetAbsoluteLeft() + ui.viewport_element->GetClientLeft(),
                ui.viewport_element->GetAbsoluteTop() + ui.viewport_element->GetClientTop(),
            };
            auto viewport_pos1 = daxa_f32vec2{viewport_pos0.x + viewport_size.x, viewport_pos0.y + viewport_size.y};
            recorder.blit_image_to_image({
                .src_image = ti.get(viewport_render_image).ids[0],
                .src_image_layout = ti.get(viewport_render_image).layout,
                .dst_image = ti.get(task_swapchain_image).ids[0],
                .dst_image_layout = ti.get(task_swapchain_image).layout,
                .src_offsets = {{{static_cast<int32_t>(viewport_pos0.x), static_cast<int32_t>(viewport_pos0.y), 0}, {static_cast<int32_t>(viewport_pos1.x), static_cast<int32_t>(viewport_pos1.y), 1}}},
                .dst_offsets = {{{static_cast<int32_t>(viewport_pos0.x), static_cast<int32_t>(viewport_pos0.y), 0}, {static_cast<int32_t>(viewport_pos1.x), static_cast<int32_t>(viewport_pos1.y), 1}}},
                .filter = daxa::Filter::LINEAR,
            });
        },
        .name = "blit_image_to_image",
    });

    if (!ui.is_fullscreen) {
        task_graph.add_task({
            .attachments = {
                daxa::inl_attachment(daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D, task_swapchain_image),
            },
            .task = [this](daxa::TaskInterface ti) {
                auto &recorder = ti.recorder;
                ui.render(recorder, ti.get(task_swapchain_image).ids[0]);
            },
            .name = "ui draw",
        });
    }
    task_graph.submit({});
    task_graph.present({});
    task_graph.complete({});
    return task_graph;
}
