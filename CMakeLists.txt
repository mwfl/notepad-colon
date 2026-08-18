cmake_minimum_required(VERSION 3.21)
project(notepad_colon VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

FetchContent_Declare(notepad_colon_wasmtime
    URL https://github.com/bytecodealliance/wasmtime/releases/download/v36.0.10/wasmtime-v36.0.10-x86_64-windows-c-api.zip
    URL_HASH SHA256=a376367a026f1b7d03bf8bec2e77abf1634b06f56366b04df7dbc71862f40b0e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _use_prebuilt_c_api)
FetchContent_MakeAvailable(notepad_colon_wasmtime)
set(WASMTIME_INCLUDE_DIR "${notepad_colon_wasmtime_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(WASMTIME_LIBRARY "${notepad_colon_wasmtime_SOURCE_DIR}/lib/wasmtime.lib" CACHE FILEPATH "" FORCE)
set(TREE_SITTER_FEATURE_WASM ON CACHE BOOL "Enable Wasm in the isolated language host" FORCE)
set(TREE_SITTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TREE_SITTER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
FetchContent_Declare(notepad_colon_tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.26.11
    GIT_SHALLOW TRUE)
FetchContent_Declare(notepad_colon_tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _use_pregenerated_parser)
FetchContent_Declare(notepad_colon_tree_sitter_json
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-json.git
    GIT_TAG v0.24.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _use_pregenerated_parser)
FetchContent_Declare(notepad_colon_tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG v0.25.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _use_pregenerated_parser)
FetchContent_Declare(notepad_colon_tree_sitter_javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG v0.25.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _use_pregenerated_parser)
FetchContent_Declare(notepad_colon_tree_sitter_typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
    GIT_TAG v0.23.2
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _use_pregenerated_parser)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(notepad_colon_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(notepad_colon_tree_sitter notepad_colon_tree_sitter_cpp
                           notepad_colon_tree_sitter_json
                           notepad_colon_tree_sitter_python
                           notepad_colon_tree_sitter_javascript
                           notepad_colon_tree_sitter_typescript
                           notepad_colon_json)
target_compile_definitions(tree-sitter PRIVATE "WASM_API_EXTERN=")
add_library(tree-sitter-cpp STATIC
    "${notepad_colon_tree_sitter_cpp_SOURCE_DIR}/src/parser.c"
    "${notepad_colon_tree_sitter_cpp_SOURCE_DIR}/src/scanner.c")
target_include_directories(tree-sitter-cpp PRIVATE
    "${notepad_colon_tree_sitter_cpp_SOURCE_DIR}/src")
set_target_properties(tree-sitter-cpp PROPERTIES C_STANDARD 11)
add_library(tree-sitter-json STATIC
    "${notepad_colon_tree_sitter_json_SOURCE_DIR}/src/parser.c")
target_include_directories(tree-sitter-json PRIVATE
    "${notepad_colon_tree_sitter_json_SOURCE_DIR}/src")
set_target_properties(tree-sitter-json PROPERTIES C_STANDARD 11)
add_library(tree-sitter-python STATIC
    "${notepad_colon_tree_sitter_python_SOURCE_DIR}/src/parser.c"
    "${notepad_colon_tree_sitter_python_SOURCE_DIR}/src/scanner.c")
target_include_directories(tree-sitter-python PRIVATE
    "${notepad_colon_tree_sitter_python_SOURCE_DIR}/src")
set_target_properties(tree-sitter-python PROPERTIES C_STANDARD 11)
add_library(tree-sitter-javascript STATIC
    "${notepad_colon_tree_sitter_javascript_SOURCE_DIR}/src/parser.c"
    "${notepad_colon_tree_sitter_javascript_SOURCE_DIR}/src/scanner.c")
target_include_directories(tree-sitter-javascript PRIVATE
    "${notepad_colon_tree_sitter_javascript_SOURCE_DIR}/src")
set_target_properties(tree-sitter-javascript PROPERTIES C_STANDARD 11)
add_library(tree-sitter-typescript STATIC
    "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/typescript/src/parser.c"
    "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c")
target_include_directories(tree-sitter-typescript PRIVATE
    "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/typescript/src")
set_target_properties(tree-sitter-typescript PROPERTIES C_STANDARD 11)
add_library(tree-sitter-tsx STATIC
    "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/tsx/src/parser.c"
    "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/tsx/src/scanner.c")
target_include_directories(tree-sitter-tsx PRIVATE
    "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/tsx/src")
set_target_properties(tree-sitter-tsx PROPERTIES C_STANDARD 11)

# Declare Scintilla before mwfl so both projects share the verified source tree
# and Lexilla can use its required sibling-directory layout on ARM64.
FetchContent_Declare(mwfl_scintilla_source
    URL
        https://github.com/mwfl/vendor-assets/releases/download/scintilla-5.6.5/scintilla565.zip
        https://www.scintilla.org/scintilla565.zip
    URL_HASH SHA256=345140A60BF4CEEA3340942E8205E6A8FBDA8DB13EB48F827126B9BE15BD3DA1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/editor-runtime/scintilla"
    SOURCE_SUBDIR _not_a_cmake_project)

option(NOTEPAD_COLON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(MWFL_SOURCE_DIR)
    set(MWFL_BUILD_SCINTILLA ON CACHE BOOL "Build Scintilla" FORCE)
    add_subdirectory("${MWFL_SOURCE_DIR}" mwfl-source EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    set(MWFL_BUILD_SCINTILLA ON CACHE BOOL "Build Scintilla" FORCE)
    FetchContent_Declare(mwfl
        GIT_REPOSITORY https://github.com/mwfl/mwfl.git
        GIT_TAG v0.2.0)
    FetchContent_MakeAvailable(mwfl)
endif()

add_library(notepad_colon_model
    src/comparison.cpp
    src/configuration.cpp
    src/document.cpp
    src/editor_config.cpp
    src/git_status.cpp
    src/editing.cpp
    src/encoding_analysis.cpp
    src/large_file.cpp
    src/large_file_buffer.cpp
    src/language.cpp
    src/language_registry.cpp
    src/lightweight_completion.cpp
    src/macro.cpp
    src/mapped_file.cpp
    src/output.cpp
    src/preferences.cpp
    src/recovery.cpp
    src/session.cpp
    src/session_writer.cpp
    src/text.cpp
    src/tree_sitter_document.cpp
    src/workspace.cpp
    src/workspace_state.cpp)

target_include_directories(notepad_colon_model PUBLIC include)
target_compile_features(notepad_colon_model PUBLIC cxx_std_20)

add_executable(notepad-colon WIN32
    src/main.cpp
    src/scintilla_support.cpp
    src/wasm_syntax_client.cpp
    app.rc
    app.manifest)
add_executable(notepad-colon-language-host
    src/wasm_language_host.cpp)
set_property(SOURCE app.manifest PROPERTY VS_TOOL_OVERRIDE "Manifest")
target_compile_features(notepad-colon PRIVATE cxx_std_20)
target_link_libraries(notepad_colon_model PUBLIC mwfl::process tree-sitter tree-sitter-cpp tree-sitter-json
    tree-sitter-python tree-sitter-javascript tree-sitter-typescript tree-sitter-tsx
    nlohmann_json::nlohmann_json)
target_link_libraries(notepad-colon PRIVATE notepad_colon_model mwfl::scintilla mwfl::shell mwfl::printing oleacc)
target_link_libraries(notepad-colon-language-host PRIVATE tree-sitter)
target_include_directories(notepad-colon-language-host PRIVATE include "${WASMTIME_INCLUDE_DIR}")
target_compile_features(notepad-colon-language-host PRIVATE cxx_std_20)
target_compile_options(notepad-colon-language-host PRIVATE /W4 /permissive- /EHsc /utf-8 /WX)
target_compile_definitions(notepad-colon-language-host PRIVATE "WASM_API_EXTERN=")
FetchContent_GetProperties(mwfl_scintilla_binary)
FetchContent_GetProperties(mwfl_scintilla_source)
set(NOTEPAD_COLON_NATIVE_EDITOR_RUNTIME OFF)
if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
    set(NOTEPAD_COLON_NATIVE_EDITOR_RUNTIME ON)
    FetchContent_Declare(notepad_colon_lexilla_runtime
        GIT_REPOSITORY https://github.com/ScintillaOrg/lexilla.git
        GIT_TAG rel-5-5-1
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/editor-runtime/lexilla"
        SOURCE_SUBDIR _not_a_cmake_project)
    FetchContent_MakeAvailable(notepad_colon_lexilla_runtime)
    include_external_msproject(notepad_colon_scintilla_runtime_project
        "${mwfl_scintilla_source_SOURCE_DIR}/win32/Scintilla.vcxproj"
        PLATFORM ARM64)
    include_external_msproject(notepad_colon_lexilla_runtime_project
        "${notepad_colon_lexilla_runtime_SOURCE_DIR}/src/Lexilla.vcxproj"
        PLATFORM ARM64)
    add_dependencies(notepad-colon notepad_colon_scintilla_runtime_project
                                   notepad_colon_lexilla_runtime_project)
    set(NOTEPAD_COLON_SCINTILLA_DLL
        "${mwfl_scintilla_source_SOURCE_DIR}/win32/ARM64/$<CONFIG>/Scintilla.dll")
    set(NOTEPAD_COLON_LEXILLA_DLL
        "${notepad_colon_lexilla_runtime_SOURCE_DIR}/src/ARM64/$<CONFIG>/Lexilla.dll")
elseif(mwfl_scintilla_binary_SOURCE_DIR)
    set(NOTEPAD_COLON_SCINTILLA_DLL
        "${mwfl_scintilla_binary_SOURCE_DIR}/Scintilla.dll")
    set(NOTEPAD_COLON_LEXILLA_DLL
        "${mwfl_scintilla_binary_SOURCE_DIR}/Lexilla.dll")
endif()
if(NOTEPAD_COLON_SCINTILLA_DLL)
    add_custom_command(TARGET notepad-colon POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NOTEPAD_COLON_SCINTILLA_DLL}"
            "$<TARGET_FILE_DIR:notepad-colon>/Scintilla.dll")
    add_custom_command(TARGET notepad-colon POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NOTEPAD_COLON_LEXILLA_DLL}"
            "$<TARGET_FILE_DIR:notepad-colon>/Lexilla.dll")
endif()
add_dependencies(notepad-colon notepad-colon-language-host)
add_custom_command(TARGET notepad-colon POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tree-sitter-json.wasm"
        "$<TARGET_FILE_DIR:notepad-colon>/tree-sitter-json-test.wasm")
if(mwfl_scintilla_source_SOURCE_DIR)
    target_include_directories(notepad-colon PRIVATE
        "${mwfl_scintilla_source_SOURCE_DIR}/include")
endif()

foreach(target notepad_colon_model notepad-colon)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    if(NOTEPAD_COLON_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE /WX)
    endif()
endforeach()

if(BUILD_TESTING)
    add_executable(notepad_colon_model_tests tests/model_tests.cpp)
    target_link_libraries(notepad_colon_model_tests PRIVATE notepad_colon_model)
    target_compile_features(notepad_colon_model_tests PRIVATE cxx_std_20)
    target_compile_options(notepad_colon_model_tests PRIVATE /W4 /permissive- /EHsc /utf-8 /WX)
    add_test(NAME notepad-colon.model COMMAND notepad_colon_model_tests)
    add_test(NAME notepad-colon.gui COMMAND notepad-colon --self-test)
    set_tests_properties(notepad-colon.gui PROPERTIES TIMEOUT 30)
    add_test(NAME notepad-colon.single-instance
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/single_instance_test.ps1"
                "$<TARGET_FILE:notepad-colon>")
    set_tests_properties(notepad-colon.single-instance PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
    add_test(NAME notepad-colon.large-file-edit
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/large_file_test.ps1"
                "$<TARGET_FILE:notepad-colon>")
    set_tests_properties(notepad-colon.large-file-edit PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE
                         LABELS performance)
    add_executable(notepad_colon_performance_tests tests/performance_tests.cpp)
    target_link_libraries(notepad_colon_performance_tests PRIVATE notepad_colon_model)
    target_compile_features(notepad_colon_performance_tests PRIVATE cxx_std_20)
    target_compile_options(notepad_colon_performance_tests PRIVATE /W4 /permissive- /EHsc /utf-8 /WX)
    add_test(NAME notepad-colon.performance COMMAND notepad_colon_performance_tests)
    set_tests_properties(notepad-colon.performance PROPERTIES TIMEOUT 45 LABELS performance)
    add_executable(notepad_colon_wasm_tests
        tests/wasm_host_tests.cpp src/wasm_syntax_client.cpp)
    target_include_directories(notepad_colon_wasm_tests PRIVATE include src)
    target_compile_features(notepad_colon_wasm_tests PRIVATE cxx_std_20)
    target_compile_options(notepad_colon_wasm_tests PRIVATE /W4 /permissive- /EHsc /utf-8 /WX)
    target_link_libraries(notepad_colon_wasm_tests PRIVATE mwfl::process)
    add_dependencies(notepad_colon_wasm_tests notepad-colon-language-host)
    add_test(NAME notepad-colon.wasm-host COMMAND notepad_colon_wasm_tests
        "$<TARGET_FILE:notepad-colon-language-host>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tree-sitter-json.wasm")
    set_tests_properties(notepad-colon.wasm-host PROPERTIES TIMEOUT 20 LABELS security)
endif()

install(TARGETS notepad-colon notepad-colon-language-host RUNTIME DESTINATION . COMPONENT product)
if(NOTEPAD_COLON_SCINTILLA_DLL)
    install(FILES
        "${NOTEPAD_COLON_SCINTILLA_DLL}"
        "${NOTEPAD_COLON_LEXILLA_DLL}"
        DESTINATION . COMPONENT product)
endif()
install(FILES README.md LICENSE THIRD-PARTY-NOTICES.md DESTINATION . COMPONENT product)
install(FILES "${notepad_colon_wasmtime_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME WASMTIME-LICENSE COMPONENT product)
install(FILES "${notepad_colon_tree_sitter_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME TREE-SITTER-LICENSE COMPONENT product)
install(FILES "${notepad_colon_tree_sitter_cpp_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME TREE-SITTER-CPP-LICENSE COMPONENT product)
install(FILES "${notepad_colon_tree_sitter_json_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME TREE-SITTER-JSON-LICENSE COMPONENT product)
install(FILES "${notepad_colon_tree_sitter_python_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME TREE-SITTER-PYTHON-LICENSE COMPONENT product)
install(FILES "${notepad_colon_tree_sitter_javascript_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME TREE-SITTER-JAVASCRIPT-LICENSE COMPONENT product)
install(FILES "${notepad_colon_tree_sitter_typescript_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME TREE-SITTER-TYPESCRIPT-LICENSE COMPONENT product)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "Notepad-Colon")
set(CPACK_PACKAGE_FILE_NAME "notepad-colon-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENTS_ALL product)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
include(CPack)
