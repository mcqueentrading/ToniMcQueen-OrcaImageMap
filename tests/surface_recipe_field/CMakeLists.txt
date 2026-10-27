cmake_minimum_required(VERSION 3.16)
project(SurfaceRecipeFieldImporterTests LANGUAGES CXX)

get_filename_component(SLICER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(nlohmann_json REQUIRED)
add_subdirectory("${SLICER_ROOT}/tests/catch2" "${CMAKE_CURRENT_BINARY_DIR}/catch2" EXCLUDE_FROM_ALL)

add_executable(surface_recipe_field_tests
    "${SLICER_ROOT}/src/libslic3r/SurfaceRecipeField.cpp"
    "${SLICER_ROOT}/tests/libslic3r/test_surface_recipe_field.cpp"
)
target_compile_features(surface_recipe_field_tests PRIVATE cxx_std_17)
set_target_properties(surface_recipe_field_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(surface_recipe_field_tests PRIVATE "${SLICER_ROOT}/src")
target_link_libraries(surface_recipe_field_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

enable_testing()
add_test(NAME surface_recipe_field_importer COMMAND surface_recipe_field_tests)

add_executable(surface_mix_recipe_preview_tests
    "${SLICER_ROOT}/src/libslic3r/SurfaceMixRecipePreview.cpp"
    "${SLICER_ROOT}/tests/libslic3r/test_surface_mix_recipe_preview.cpp"
)
target_compile_features(surface_mix_recipe_preview_tests PRIVATE cxx_std_17)
set_target_properties(surface_mix_recipe_preview_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(surface_mix_recipe_preview_tests PRIVATE "${SLICER_ROOT}/src")
target_link_libraries(surface_mix_recipe_preview_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
add_test(NAME surface_mix_recipe_preview COMMAND surface_mix_recipe_preview_tests)

add_executable(surface_mix_top_stack_tests
    "${SLICER_ROOT}/src/libslic3r/SurfaceMixRecipePreview.cpp"
    "${SLICER_ROOT}/src/libslic3r/SurfaceMixTopStack.cpp"
    "${SLICER_ROOT}/tests/libslic3r/test_surface_mix_top_stack.cpp"
)
target_compile_features(surface_mix_top_stack_tests PRIVATE cxx_std_17)
target_include_directories(surface_mix_top_stack_tests PRIVATE "${SLICER_ROOT}/src")
target_link_libraries(surface_mix_top_stack_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
add_test(NAME surface_mix_top_stack COMMAND surface_mix_top_stack_tests)
add_executable(surface_mix_side_offset_tests
    "${SLICER_ROOT}/src/libslic3r/SurfaceMixSideOffsets.cpp"
    "${SLICER_ROOT}/tests/libslic3r/test_surface_mix_side_offsets.cpp")
target_compile_features(surface_mix_side_offset_tests PRIVATE cxx_std_17)
target_include_directories(surface_mix_side_offset_tests PRIVATE "${SLICER_ROOT}/src")
target_compile_definitions(surface_mix_side_offset_tests PRIVATE
    SURFACEMIX_SIDE_CATALOGUE="${SLICER_ROOT}/resources/surfacemix/creator5-cmyw-side-offsets.v1.json")
target_link_libraries(surface_mix_side_offset_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
add_test(NAME surface_mix_side_offsets COMMAND surface_mix_side_offset_tests)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME surface_mix_side_wall_verifier COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/test_side_wall_paths.py")
    add_test(NAME surface_mix_path_binding_verifier COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/test_top_stack_path_binding.py")
endif()

add_executable(surface_mix_recipe_preview_check
    "${SLICER_ROOT}/src/libslic3r/SurfaceMixRecipePreview.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/check_recipe_preview.cpp"
)
target_compile_features(surface_mix_recipe_preview_check PRIVATE cxx_std_17)
set_target_properties(surface_mix_recipe_preview_check PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(surface_mix_recipe_preview_check PRIVATE "${SLICER_ROOT}/src")
target_link_libraries(surface_mix_recipe_preview_check PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME surface_mix_bundled_catalogue COMMAND surface_mix_recipe_preview_check
    "${SLICER_ROOT}/resources/surfacemix/creator5-cmyw-top-preview.v1.json")

add_executable(surface_recipe_field_fixture_check
    "${SLICER_ROOT}/src/libslic3r/SurfaceRecipeField.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/check_saved_fixtures.cpp"
)
target_compile_features(surface_recipe_field_fixture_check PRIVATE cxx_std_17)
set_target_properties(surface_recipe_field_fixture_check PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(surface_recipe_field_fixture_check PRIVATE "${SLICER_ROOT}/src")
target_link_libraries(surface_recipe_field_fixture_check PRIVATE nlohmann_json::nlohmann_json)
