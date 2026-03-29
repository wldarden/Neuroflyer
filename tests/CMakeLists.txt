add_executable(neuroflyer_tests
    game_test.cpp
    arena_session_test.cpp
    base_test.cpp
    ray_test.cpp
    evolution_test.cpp
    config_test.cpp
    scoring_test.cpp
    input_vector_test.cpp
    ship_design_test.cpp
    snapshot_io_test.cpp
    name_validation_test.cpp
    genome_manager_test.cpp
    snapshot_population_test.cpp
    mrca_tracker_test.cpp
    camera_test.cpp
    arena_sensor_test.cpp
    ../src/engine/game.cpp
    ../src/engine/arena_session.cpp
    ../src/engine/ray.cpp
    ../src/engine/sensor_engine.cpp
    ../src/engine/evolution.cpp
    ../src/engine/config.cpp
    ../src/engine/snapshot_io.cpp
    ../src/engine/genome_manager.cpp
    ../src/engine/mrca_tracker.cpp
    ../src/engine/arena_sensor.cpp
)
target_include_directories(neuroflyer_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(neuroflyer_tests PRIVATE
    neuralnet evolve nlohmann_json::nlohmann_json GTest::gtest_main project_warnings
)
gtest_discover_tests(neuroflyer_tests)
