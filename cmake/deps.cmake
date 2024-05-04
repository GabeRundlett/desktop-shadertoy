
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../deps/Daxa/CMakeLists.txt")
    find_package(Git REQUIRED)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        COMMAND_ERROR_IS_FATAL ANY)
endif()

find_package(Git REQUIRED)
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/media")
    execute_process(COMMAND ${GIT_EXECUTABLE} clone https://github.com/GabeRundlett/desktop-shadertoy-media media
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND_ERROR_IS_FATAL ANY)
else()
    execute_process(COMMAND ${GIT_EXECUTABLE} pull
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/media
        COMMAND_ERROR_IS_FATAL ANY)
endif()

if(DESKTOP_SHADERTOY_USE_VCPKG)
    if(LINUX)
        list(APPEND VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/overlay-ports/openssl")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/vcpkg.cmake")
endif()
