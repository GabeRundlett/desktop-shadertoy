cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/deps.cmake")

project(desktop-shadertoy VERSION 0.4.0)

add_executable(${PROJECT_NAME}
    "src/main.cpp"
    "src/app/viewport.cpp"
    "src/app/resources.cpp"
    "src/ui/app_window.cpp"
    "src/ui/app_ui.cpp"
    "src/ui/components/buffer_panel.cpp"
    "src/rml/render_daxa.cpp"
    "src/rml/system_glfw.cpp"
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

find_package(daxa CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(RmlUi CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Boost COMPONENTS asio beast)
find_package(OpenSSL REQUIRED)
find_package(unofficial-nativefiledialog CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)

find_package(glslang CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

target_link_libraries(${PROJECT_NAME}
PRIVATE
    daxa::daxa
    nlohmann_json::nlohmann_json
    glfw
    RmlCore RmlDebugger
    fmt::fmt
    unofficial::nativefiledialog::nfd
    efsw::efsw
    ${Boost_LIBRARIES}
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${Stb_INCLUDE_DIR}
    "${CMAKE_CURRENT_LIST_DIR}/src"
)

if(UNIX AND NOT APPLE)
    # On Linux, we use the system OpenSSL, so it needs to be linked like so:
    target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${OPENSSL_SSL_LIBRARY}
        ${OPENSSL_CRYPTO_LIBRARY}
    )
    target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${OPENSSL_INCLUDE_DIR}
    )
else()
    target_link_libraries(${PROJECT_NAME}
    PRIVATE
        OpenSSL::SSL
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    configure_file("packaging/main.rc.in" "${CMAKE_BINARY_DIR}/main.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/main.rc")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_options(${PROJECT_NAME} PRIVATE "-Wl,/ENTRY:mainCRTStartup,/SUBSYSTEM:WINDOWS")
    else()
        target_link_options(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS)
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE Dwmapi)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/packaging.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/appimage.cmake")
