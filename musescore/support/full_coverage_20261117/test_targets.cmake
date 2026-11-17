add_executable(muse_vsl_sample_state_tests
    "${CMAKE_CURRENT_LIST_DIR}/state_codec_tests.cpp"
    "${PROJECT_SOURCE_DIR}/src/framework/vst/internal/synth/vslsamplestate.cpp")
target_include_directories(muse_vsl_sample_state_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/framework/vst/internal/synth")
target_link_libraries(muse_vsl_sample_state_tests PRIVATE Qt::Core muse_vsl_zstd)
target_compile_features(muse_vsl_sample_state_tests PRIVATE cxx_std_17)

add_executable(muse_vsl_load_observation_tests "${CMAKE_CURRENT_LIST_DIR}/load_observation_tests.cpp")
target_include_directories(muse_vsl_load_observation_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/framework/vst/internal/synth")
target_compile_features(muse_vsl_load_observation_tests PRIVATE cxx_std_17)
