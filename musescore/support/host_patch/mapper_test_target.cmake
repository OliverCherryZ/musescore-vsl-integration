# Include at the end of the official top-level CMakeLists.txt, after muse_global.
# This builds the real mapper against real MuseScore types, without linking the app.
if(NOT TARGET muse_global)
    message(FATAL_ERROR "Include mapper_test_target.cmake after MuseScore framework targets")
endif()
add_executable(muse_vst_expression_mapper_tests
    "${CMAKE_CURRENT_LIST_DIR}/mapper_tests.cpp"
    "${PROJECT_SOURCE_DIR}/src/framework/vst/internal/synth/vstarticulationmapper.cpp"
)
target_include_directories(muse_vst_expression_mapper_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/framework"
    "${PROJECT_SOURCE_DIR}/src/framework/global"
    "${PROJECT_SOURCE_DIR}/src/framework/vst/internal/synth"
)
target_link_libraries(muse_vst_expression_mapper_tests PRIVATE muse::global Qt::Core)
target_compile_features(muse_vst_expression_mapper_tests PRIVATE cxx_std_17)
