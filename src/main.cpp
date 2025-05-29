#include <app/viewport.hpp>
#include <app/resources.hpp>

#include <chrono>
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

#include <iostream>
#include <format>
using Clock = std::chrono::high_resolution_clock;

struct Timer {
    Clock::time_point start;
    std::string name;
    Timer(std::string const &name) : name{name} { start = Clock::now(); }
    ~Timer() {
        auto end = Clock::now();
        std::cout << std::format("[{}] {}\n", name, std::chrono::duration<double>(end - start).count()) << std::flush;
    }
};

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

std::ofstream *f_ptr = nullptr;

namespace core {
    void log_error(std::string const &msg) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", msg.c_str());
        if (!f_ptr)
            return;
        (*f_ptr) << msg << std::endl;
    }
} // namespace core

void search_for_path_to_fix_working_directory(std::span<std::filesystem::path const> test_paths) {
    auto current_path = std::filesystem::current_path();
    while (true) {
        for (auto const &test_path : test_paths) {
            if (std::filesystem::exists(current_path / test_path)) {
                std::filesystem::current_path(current_path);
                return;
            }
        }
        if (!current_path.has_parent_path()) {
            break;
        }
        current_path = current_path.parent_path();
    }
}

#include "thread_pool.hpp"

char const *shaderToyDomain = "www.shadertoy.com";
char const *shaderToyPort = "443";
char const *shaderToyKey = "Bt8jhH";
char const *userAgent = BOOST_BEAST_VERSION_STRING;

struct ShadertoyApi {
    using Stream = boost::beast::ssl_stream<boost::beast::tcp_stream>;

    boost::beast::net::io_context ioc{};
    boost::asio::ip::tcp::resolver resolver{ioc};
    boost::asio::ssl::context ssl_ctx{boost::asio::ssl::context::method::sslv23_client};
    boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp> results = resolver.resolve(shaderToyDomain, shaderToyPort);

    ShadertoyApi() = default;
    auto operator=(const ShadertoyApi &) -> ShadertoyApi & = delete;
    auto operator=(ShadertoyApi &&) -> ShadertoyApi & = delete;
    ShadertoyApi(const ShadertoyApi &) = delete;
    ShadertoyApi(ShadertoyApi &&) = delete;
    ~ShadertoyApi() = default;

    auto request(std::string const &requestURI) -> std::string {
        Stream stream = Stream(ioc, ssl_ctx);
        boost::beast::get_lowest_layer(stream).connect(results.begin(), results.end());
        stream.handshake(boost::asio::ssl::stream_base::client);

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
        }

        return boost::beast::buffers_to_string(res.body().data());
    }
};

auto download_shadertoy_json_from_id_or_url(std::string const &input, ShadertoyApi &api) -> nlohmann::json {
    auto shader_id = input;
    auto slash_pos = shader_id.find_last_of('/');
    if (slash_pos != std::string::npos) {
        shader_id = shader_id.substr(slash_pos + 1);
    }

    return nlohmann::json::parse(api.request("/api/v1/shaders/" + shader_id + "?key=" + shaderToyKey));
}

using namespace std::literals;

void test_func(std::string const &id) {
    thread_local ShadertoyApi shadertoy_api{};

    auto t = Timer(id);
    auto shader_json = download_shadertoy_json_from_id_or_url(id, shadertoy_api);
    auto f = std::ofstream("shaders/" + id + ".json");
    f << std::setw(4) << shader_json;
    std::this_thread::sleep_for(1s);
}

void download_all_shadertoys() {
    nlohmann::json json;
    {
        auto shadertoy_api = ShadertoyApi{};

        auto result_str = std::string{};
        {
            Timer t0("load");
            result_str = shadertoy_api.request(std::string("/api/v1/shaders?key=") + shaderToyKey);
        }
        json = nlohmann::json::parse(result_str);
        auto f = std::ofstream("test2.json");
        f << std::setw(4) << json;
    }

    // nlohmann::json json = nlohmann::json::parse(std::ifstream("test.json"));

    auto shader_count = int(json["Shaders"]);
    auto results = json["Results"];

    ThreadPool thread_pool;
    thread_pool.start();

    for (int i = 0; i < shader_count; ++i) {
        std::string shader_id = results[i];
        if (!std::filesystem::exists("shaders/" + shader_id + ".json")) {
            thread_pool.enqueue([=]() { test_func(shader_id); });
        }
        if ((i % 100) == 0) {
            std::cout << std::format("{} done\n", i) << std::flush;
        }
    }

    while (thread_pool.busy()) {
        std::this_thread::sleep_for(1s);
    }
    thread_pool.stop();
}

void test_all_shadertoys() {
    auto dir_iter = std::filesystem::directory_iterator{"shaders"};

    int shaders_tested = 0;
    int shaders_failed = 0;

    auto black_list = std::array{
        std::string{"4ttyzn.json"},
        std::string{"7dsBRM.json"},
        std::string{"clyBzc.json"},
        std::string{"DdlSWr.json"},
        std::string{"flBGDy.json"},
        std::string{"Mlcczs.json"},
        std::string{"stf3Dj.json"},
        std::string{"ttGGDz.json"},
        std::string{"ttjSR3.json"},
        std::string{"wsKyRc.json"},
        std::string{"Xlcczj.json"},
    };

    auto f = std::ofstream("compilation.txt", std::ios_base::app);
    auto f2 = std::ofstream("compilation-times.txt", std::ios_base::app);
    f_ptr = &f;

    auto app = ShaderApp();
    while (true) {
        auto t0 = Clock::now();
        std::filesystem::path path;
        ++shaders_tested;
        if (!(dir_iter == std::default_sentinel_t{})) {
            auto const &dir_entry = *dir_iter;
            path = dir_entry.path();
            ++dir_iter;
        } else {
            break;
        }

        // if (shaders_tested <= black_list.back()) {
        //     continue;
        // }
        // if (shaders_tested <= 13576) {
        //     continue;
        // }

        if (std::find(black_list.begin(), black_list.end(), path.filename()) != black_list.end()) {
            continue;
        }

        auto json = nlohmann::json::parse(std::ifstream(path));
        app.ui.buffer_panel.load_shadertoy_json(json);

        std::cout << std::format("{}, // {}\n", shaders_tested, path.string()) << std::flush;
        app.update();
        if (app.should_close()) {
            break;
        }
        auto t1 = Clock::now();
        app.render();
        auto t2 = Clock::now();

        if (app.viewport.load_failed) {
            ++shaders_failed;
            f << std::format("{}: {}\n", shaders_tested, path.string());
            f << std::format("[failed] ({}/{} or {}%)\n", shaders_failed, shaders_tested, float(shaders_failed) / float(shaders_tested) * 100) << std::flush;
        } else {
            auto d01 = std::chrono::duration<float>(t1 - t0).count();
            auto d12 = std::chrono::duration<float>(t2 - t1).count();
            auto d02 = std::chrono::duration<float>(t2 - t0).count();
            f2 << std::format("{}, {}, {}, {}, {}\n", shaders_tested, path.string(), d01, d12, d02) << std::flush;
        }

        // try {
        //     app.daxa_device.wait_idle();
        // } catch (...) {
        //     std::ofstream outfile;
        //     outfile.open("device_lost.txt", std::ios_base::app);
        //     outfile << std::format("{}, // {}\n", shaders_tested, path.string());
        //     outfile.close();
        //     std::terminate();
        // }
    }
}

auto main() -> int {
    search_for_path_to_fix_working_directory(std::array{
        std::filesystem::path{"media"},
    });

    // download_all_shadertoys();
    // return 0;

    // test_all_shadertoys();
    // return 0;

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
      daxa_device{[&]() {
          auto required_implicit = daxa::ImplicitFeatureFlagBits::SWAPCHAIN;
          auto device_info = daxa::DeviceInfo2{};
          device_info.name = "Desktop Shadertoy";
          device_info = daxa_instance.choose_device(required_implicit, device_info);
          return daxa_instance.create_device_2(device_info);
      }()},
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
        // set_project_filepath(paths[0]);
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

    ui.buffer_panel.load_shadertoy_json(nlohmann::json::parse(std::ifstream(resource_dir / "default-shader.json")));
}

ShaderApp::~ShaderApp() {
    if (ui.render_interface.crashed)
        return;
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
    auto shadertoy_api = ShadertoyApi{};
    auto json = download_shadertoy_json_from_id_or_url(input, shadertoy_api);

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
            auto swapchain_image_full_slice = daxa_device.image_view_info(swapchain_image.default_view()).value().slice;
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
            auto image_size = ti.device.image_info(ti.get(viewport_render_image).ids[0]).value().size;
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
                .src_offsets = {{{static_cast<int32_t>(viewport_pos0.x), static_cast<int32_t>(viewport_pos1.y), 0}, {static_cast<int32_t>(viewport_pos1.x), static_cast<int32_t>(viewport_pos0.y), 1}}},
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
