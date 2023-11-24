#include <app/viewport.hpp>

#include <cstdint>
#include <daxa/daxa.hpp>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/utils/task_graph.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

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
    ui.app_windows[0].on_resize = [&]() {
        if (ui.app_windows[0].size.x <= 0 || ui.app_windows[0].size.y <= 0) {
            return;
        }
        main_task_graph = record_main_task_graph();
        render();
    };
    ui.app_windows[0].on_drop = [&](std::span<char const *> paths) {
        viewport.load_shadertoy_json(nlohmann::json::parse(std::ifstream(paths[0])));
        main_task_graph = record_main_task_graph();
    };
    ui.app_windows[0].on_mouse_move = std::bind(&Viewport::on_mouse_move, &viewport, std::placeholders::_1, std::placeholders::_2);
    ui.app_windows[0].on_mouse_button = std::bind(&Viewport::on_mouse_button, &viewport, std::placeholders::_1, std::placeholders::_2);
    ui.app_windows[0].on_key = [&](int32_t key_id, int32_t action) {
        switch (key_id) {
        case GLFW_KEY_F11:
            if (action == GLFW_PRESS) {
                ui.toggle_fullscreen();
            }
            break;
        case GLFW_KEY_ESCAPE:
            if (action == GLFW_PRESS && ui.is_fullscreen) {
                ui.toggle_fullscreen();
            }
            break;
        default:
            break;
        }

        viewport.on_key(key_id, action);
    };

    ui.on_reset = std::bind(&Viewport::on_reset, &viewport);
    ui.on_toggle_pause = std::bind(&Viewport::on_toggle_pause, &viewport, std::placeholders::_1);

    ui.on_toggle_fullscreen = [&](bool is_fullscreen) {
        ui.app_windows[0].set_fullscreen(is_fullscreen);
        main_task_graph = record_main_task_graph();
    };

    ui.on_download = [&](Rml::String const &rml_input) {
        download_shadertoy(rml_input);
    };

    viewport.load_shadertoy_json(nlohmann::json::parse(std::ifstream("default-shader.json")));
    main_task_graph = record_main_task_graph();
}

ShaderApp::~ShaderApp() {
    ui.app_windows.clear();
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
    auto &app_window = ui.app_windows[0];
    if (app_window.size.x <= 0 || app_window.size.y <= 0) {
        return;
    }
    auto const swapchain_image = app_window.swapchain.acquire_next_image();
    if (swapchain_image.is_empty()) {
        return;
    }
    task_swapchain_image.set_images({.images = {&swapchain_image, 1}});

    viewport.render(ui.app_windows[0].size);

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
        core::log_error("Failed to download from shadertoy: " + std::string{json["Error"]});
        return;
    }

    if (ui.settings.export_downloads) {
        auto f = std::ofstream("test-shader.json");
        f << std::setw(4) << json["Shader"];
    }

    viewport.load_shadertoy_json(json["Shader"]);
    main_task_graph = record_main_task_graph();
}

auto ShaderApp::record_main_task_graph() -> daxa::TaskGraph {
    auto &app_window = ui.app_windows[0];
    viewport.gpu_input.Resolution = daxa_f32vec3{
        static_cast<daxa_f32>(app_window.size.x),
        static_cast<daxa_f32>(app_window.size.y),
        1.0f,
    };

    auto task_graph = daxa::TaskGraph(daxa::TaskGraphInfo{
        .device = daxa_device,
        .swapchain = app_window.swapchain,
        .name = "main_tg",
    });
    task_graph.use_persistent_image(task_swapchain_image);

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

    auto viewport_render_image = viewport.record(task_graph);

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

    if (!ui.is_fullscreen) {
        task_graph.add_task({
            .uses = {
                daxa::TaskImageUse<daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageViewType::REGULAR_2D>{task_swapchain_image},
            },
            .task = [this](daxa::TaskInterface ti) {
                auto &recorder = ti.get_recorder();
                ui.render(recorder, ti.uses[task_swapchain_image].image());
            },
            .name = "ui draw",
        });
    }
    task_graph.submit({});
    task_graph.present({});
    task_graph.complete({});
    return task_graph;
}
