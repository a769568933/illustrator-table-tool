cmake_minimum_required(VERSION 3.21)
project(IllustratorTableTool VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(TABLE_BUILD_PLUGIN "Build the Illustrator native plugin (requires the Adobe SDK)" ON)
include(CTest)
add_library(table_core STATIC src/core/TableModel.cpp src/core/Geometry.cpp src/core/Interaction.cpp)
target_include_directories(table_core PUBLIC include)
if(MSVC)
  target_compile_options(table_core PRIVATE /utf-8 /W4)
endif()
if(BUILD_TESTING)
  add_executable(table_core_tests tests/core_tests.cpp)
  target_link_libraries(table_core_tests PRIVATE table_core)
  if(MSVC)
    target_compile_options(table_core_tests PRIVATE /utf-8 /W4)
  endif()
  add_test(NAME table_core COMMAND table_core_tests)
endif()

if(TABLE_BUILD_PLUGIN)
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Illustrator 28.5 plugin requires Windows x64 and MSVC; use TABLE_BUILD_PLUGIN=OFF for core tests.")
  endif()
  set(ILLUSTRATOR_SDK_ROOT "$ENV{ILLUSTRATOR_SDK_ROOT}" CACHE PATH "Adobe Illustrator 2024 SDK root")
  include(cmake/IllustratorSdk.cmake)
  enable_language(RC)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(PIPL_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/plugin.pipl")
  add_custom_command(OUTPUT "${PIPL_OUTPUT}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/generate_pipl.py"
      --template "${ILLUSTRATOR_SDK_ROOT}/tools/pipl/pipl_gen/template_plugin.json"
      --output "${PIPL_OUTPUT}"
    DEPENDS scripts/generate_pipl.py
      "${ILLUSTRATOR_SDK_ROOT}/tools/pipl/pipl_gen/template_plugin.json")
  configure_file(resources/Plugin.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Plugin.rc" @ONLY)
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Plugin.rc" PROPERTIES OBJECT_DEPENDS "${PIPL_OUTPUT}")
  add_library(IllustratorTableTool SHARED src/plugin/Plugin.cpp src/plugin/DocumentAdapter.cpp
    "${ILLUSTRATOR_SDK_ROOT}/illustratorapi/illustrator/IAIUnicodeString.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/Plugin.rc" "${PIPL_OUTPUT}" resources/Plugin.def)
  target_include_directories(IllustratorTableTool PRIVATE
    "${ILLUSTRATOR_SDK_ROOT}/illustratorapi/illustrator"
    "${ILLUSTRATOR_SDK_ROOT}/illustratorapi/illustrator/actions"
    "${ILLUSTRATOR_SDK_ROOT}/illustratorapi/pica_sp"
    "${ILLUSTRATOR_SDK_ROOT}/illustratorapi/ate"
    "${ILLUSTRATOR_SDK_ROOT}/illustratorapi/adm")
  target_compile_definitions(IllustratorTableTool PRIVATE WIN_ENV _WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(IllustratorTableTool PRIVATE /utf-8 /Zp8 /Zc:wchar_t- /Zc:sizedDealloc- /EHsc)
  set_property(TARGET IllustratorTableTool PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  target_link_libraries(IllustratorTableTool PRIVATE user32 table_core)
  set_target_properties(IllustratorTableTool PROPERTIES PREFIX "" SUFFIX ".aip"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")
endif()
