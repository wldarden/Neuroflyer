cmake_minimum_required(VERSION 3.24)
project(neuroflyer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerWarnings.cmake)

include(FetchContent)

# Library dependencies — declare all before MakeAvailable for version control
FetchContent_Declare(evolve
    GIT_REPOSITORY https://github.com/wldarden/evolve.git
    GIT_TAG        main
)
FetchContent_Declare(neuralnet
    GIT_REPOSITORY https://github.com/wldarden/neuralnet.git
    GIT_TAG        main
)
FetchContent_Declare(neuralnet-ui
    GIT_REPOSITORY https://github.com/wldarden/neuralnet-ui.git
    GIT_TAG        main
)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.6
)

# SDL2 (system-installed) — must be found before neuralnet-ui FetchContent
list(APPEND CMAKE_PREFIX_PATH "/usr/local")
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)

FetchContent_MakeAvailable(evolve neuralnet neuralnet-ui json imgui)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()
include(GoogleTest)

# Build ImGui as static library (imgui_lib to avoid FetchContent name collision)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2)
set_target_properties(imgui_lib PROPERTIES COMPILE_OPTIONS "")

# Main executable
add_executable(neuroflyer
    # Engine — pure logic, no SDL/ImGui
    src/engine/game.cpp
    src/engine/arena_session.cpp
    src/engine/evolution.cpp
    src/engine/config.cpp
    src/engine/snapshot_io.cpp
    src/engine/genome_manager.cpp
    src/engine/mrca_tracker.cpp
    src/engine/ray.cpp
    src/engine/sensor_engine.cpp
    src/engine/arena_sensor.cpp
    src/engine/team_evolution.cpp
    src/engine/arena_match.cpp
    src/engine/sector_grid.cpp
    src/engine/squad_leader.cpp
    src/engine/fighter_drill_session.cpp
    src/engine/attack_run_session.cpp
    src/engine/skirmish.cpp
    src/engine/skirmish_tournament.cpp
    src/engine/team_skirmish.cpp
    src/engine/paths.cpp

    # UI — SDL/ImGui rendering and interaction
    src/ui/main.cpp
    src/ui/renderer.cpp
    src/ui/framework/ui_manager.cpp
    src/ui/framework/ui_widget.cpp
    src/ui/framework/modals/confirm_modal.cpp
    src/ui/framework/modals/input_modal.cpp
    src/ui/framework/modals/editor_modal.cpp
    src/ui/modals/node_editor_modal.cpp
    src/ui/modals/layer_editor_modal.cpp
    src/ui/modals/fighter_pairing_modal.cpp
    src/ui/renderers/variant_net_render.cpp
    src/ui/views/game_view.cpp
    src/ui/views/fitness_editor.cpp
    src/ui/views/lineage_graph.cpp
    src/ui/views/net_designer_view.cpp
    src/ui/views/test_bench.cpp
    src/ui/views/structural_heatmap.cpp
    src/ui/views/pause_training.cpp
    src/ui/views/pause_evolution.cpp
    src/ui/screens/menu/main_menu_screen.cpp
    src/ui/screens/menu/settings_screen.cpp
    src/ui/screens/hangar/hangar_screen.cpp
    src/ui/screens/hangar/create_genome_screen.cpp
    src/ui/screens/hangar/variant_viewer_screen.cpp
    src/ui/screens/hangar/lineage_tree_screen.cpp
    src/ui/screens/hangar/variant_net_editor_screen.cpp
    src/ui/screens/game/fly_session_screen.cpp
    src/ui/screens/game/pause_config_screen.cpp
    src/ui/screens/arena/arena_config_screen.cpp
    src/ui/screens/arena/arena_game_screen.cpp
    src/ui/screens/arena/arena_pause_screen.cpp
    src/ui/screens/game/fighter_drill_screen.cpp
    src/ui/screens/game/attack_run_screen.cpp
    src/ui/screens/game/skirmish_screen.cpp
    src/ui/screens/game/skirmish_config_screen.cpp
    src/ui/screens/game/skirmish_pause_screen.cpp
    src/ui/screens/game/fighter_drill_pause_screen.cpp
    src/ui/screens/game/team_skirmish_config_screen.cpp
    src/ui/screens/game/team_skirmish_screen.cpp
    src/ui/screens/game/team_skirmish_pause_screen.cpp
    src/ui/screens/analysis/analysis.cpp
    src/ui/views/arena_config_view.cpp
    src/ui/views/arena_game_view.cpp
    src/ui/views/arena_game_info_view.cpp
    src/ui/views/topology_preview_view.cpp
    src/ui/views/net_viewer_view.cpp
    src/ui/views/test_bench_view.cpp
    game-ui/src/components/highlight_list.cpp
    game-ui/src/components/modal.cpp
)
target_include_directories(neuroflyer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/game-ui/include
)
target_link_libraries(neuroflyer PRIVATE
    neuralnet neuralnet-ui evolve SDL2::SDL2 SDL2_image::SDL2_image imgui_lib nlohmann_json::nlohmann_json project_warnings
)

add_subdirectory(tests)
