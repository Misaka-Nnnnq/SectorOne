cmake_minimum_required(VERSION 4.3)
project(SectorOne)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(imgui CONFIG REQUIRED)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(MLN_WITH_GLFW OFF CACHE BOOL "" FORCE)
set(MLN_WITH_NODE OFF CACHE BOOL "" FORCE)
set(MLN_WITH_QT OFF CACHE BOOL "" FORCE)
set(MLN_WITH_OPENGL ON CACHE BOOL "" FORCE)
set(MLN_WITH_EGL OFF CACHE BOOL "" FORCE)
set(MLN_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(MLN_WITH_WERROR OFF CACHE BOOL "" FORCE)
set(MLN_USE_RUST OFF CACHE BOOL "" FORCE)
set(MLN_LEGACY_RENDERER ON CACHE BOOL "" FORCE)
set(MLN_DRAWABLE_RENDERER OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/maplibre-native EXCLUDE_FROM_ALL)

add_executable(SectorOne
        src/main.cpp
        src/map_view.cpp
)

target_include_directories(SectorOne PRIVATE
        third_party/maplibre-native/src
)

target_link_libraries(SectorOne PRIVATE
        imgui::imgui
        OpenGL::GL
        Mapbox::Map
)

target_compile_options(SectorOne PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>
)
