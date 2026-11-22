# All plugins in one build. Windows from Linux:
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build-win -j8
# Linux:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
# One plugin only: cmake --build build --target swarm-synth-vst3  (or e.g. guitar-to-midi-live-clap)
cmake_minimum_required(VERSION 3.7...3.31)
set(CMAKE_CXX_STANDARD 17)
project(vst-plugins)

set(VST_SHARED ${CMAKE_CURRENT_SOURCE_DIR}/shared)
add_subdirectory(shared/dpf)

# adds one DPF plugin with the shared Dear ImGui widget; extra dpf_add_plugin options (USE_FILE_BROWSER, TARGETS ...) pass through
function(vst_add_plugin NAME)
  dpf_add_plugin(${NAME}
    ${ARGN}
    FILES_DSP
        src/PluginDSP.cpp
    FILES_UI
        src/PluginUI.cpp
        ${VST_SHARED}/dpf-widgets/opengl/DearImGui.cpp)
  target_include_directories(${NAME} PUBLIC
    src
    ${VST_SHARED}/dpf-widgets/generic
    ${VST_SHARED}/dpf-widgets/opengl)
endfunction()

add_subdirectory(plugins/swarm-chorus)
add_subdirectory(plugins/swarm-delay)
add_subdirectory(plugins/swarm-synth)
add_subdirectory(plugins/cleaver-slicer)
add_subdirectory(plugins/guitar-to-midi-live)
