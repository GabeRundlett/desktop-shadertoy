
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../deps/Daxa/CMakeLists.txt" OR
    NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../deps/gvox/CMakeLists.txt")
    find_package(Git REQUIRED)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        COMMAND_ERROR_IS_FATAL ANY)
endif()


if(GVOX_EDITOR_USE_VCPKG)
    include("${CMAKE_CURRENT_LIST_DIR}/vcpkg.cmake")
endif()
