# Both targets compile the application's actual mapper sources by default.
# Override only for isolated, unapplied candidate checks; never mix headers and cpp.
set(MUSE_VST_MAPPER_TEST_SOURCE_ROOT "${PROJECT_SOURCE_DIR}"
    CACHE PATH "Source root supplying mapper cpp/header to extension and compatibility tests")
set(mapper_source_dir "${MUSE_VST_MAPPER_TEST_SOURCE_ROOT}/src/framework/vst/internal/synth")
foreach(test_name IN ITEMS extension baseline_compat)
    if(test_name STREQUAL "extension")
        set(test_source "${CMAKE_CURRENT_LIST_DIR}/extension_mapper_tests.cpp")
    else()
        set(test_source "${CMAKE_CURRENT_LIST_DIR}/baseline_mapper_tests.cpp")
    endif()
    set(target "muse_vst_mapper_${test_name}_tests")
    add_executable(${target}
        "${test_source}"
        "${mapper_source_dir}/vstarticulationmapper.cpp"
    )
    target_include_directories(${target} PRIVATE
        "${mapper_source_dir}"
        "${PROJECT_SOURCE_DIR}/src/framework"
        "${PROJECT_SOURCE_DIR}/src/framework/global"
    )
    target_link_libraries(${target} PRIVATE muse::global Qt::Core)
    target_compile_features(${target} PRIVATE cxx_std_17)
endforeach()

add_executable(muse_vst_harmonic_tremolo_mapper_tests
    "${CMAKE_CURRENT_LIST_DIR}/../harmonic_tremolo/vst/harmonic_tremolo_mapper_tests.cpp"
    "${mapper_source_dir}/vstarticulationmapper.cpp"
)
target_include_directories(muse_vst_harmonic_tremolo_mapper_tests PRIVATE
    "${mapper_source_dir}"
    "${PROJECT_SOURCE_DIR}/src/framework"
    "${PROJECT_SOURCE_DIR}/src/framework/global"
)
target_link_libraries(muse_vst_harmonic_tremolo_mapper_tests PRIVATE muse::global Qt::Core)
target_compile_features(muse_vst_harmonic_tremolo_mapper_tests PRIVATE cxx_std_17)
