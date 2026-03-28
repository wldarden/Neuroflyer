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
    neuralnet neuralnet-ui evolve SDL2::SDL2 SDL2_image::SDL2_image imgui nlohmann_json::nlohmann_json project_warnings
)

add_subdirectory(tests)
