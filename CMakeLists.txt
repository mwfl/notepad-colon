cmake_minimum_required(VERSION 3.21)
project(notepad_colon VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(NOTEPAD_COLON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(MWFL_SOURCE_DIR)
    set(MWFL_BUILD_SCINTILLA ON CACHE BOOL "Build Scintilla" FORCE)
    add_subdirectory("${MWFL_SOURCE_DIR}" mwfl-source EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    set(MWFL_BUILD_SCINTILLA ON CACHE BOOL "Build Scintilla" FORCE)
    FetchContent_Declare(mwfl
        GIT_REPOSITORY https://github.com/mwfl/mwfl.git
        GIT_TAG 6561bb7321e8e05aac7a45b6e463e0af4f1e6fd9)
    FetchContent_MakeAvailable(mwfl)
endif()

add_library(notepad_colon_model
    src/document.cpp
    src/large_file.cpp
    src/language.cpp
    src/session.cpp
    src/text.cpp
    src/workspace.cpp)
target_include_directories(notepad_colon_model PUBLIC include)
target_compile_features(notepad_colon_model PUBLIC cxx_std_20)

add_executable(notepad-colon WIN32
    src/main.cpp
    src/scintilla_support.cpp
    app.manifest)
set_property(SOURCE app.manifest PROPERTY VS_TOOL_OVERRIDE "Manifest")
target_compile_features(notepad-colon PRIVATE cxx_std_20)
target_link_libraries(notepad-colon PRIVATE notepad_colon_model mwfl::scintilla mwfl::shell oleacc)
FetchContent_GetProperties(mwfl_scintilla_binary)
FetchContent_GetProperties(mwfl_scintilla_source)
if(mwfl_scintilla_binary_SOURCE_DIR)
    add_custom_command(TARGET notepad-colon POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${mwfl_scintilla_binary_SOURCE_DIR}/Scintilla.dll"
            "$<TARGET_FILE_DIR:notepad-colon>/Scintilla.dll")
    add_custom_command(TARGET notepad-colon POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${mwfl_scintilla_binary_SOURCE_DIR}/Lexilla.dll"
            "$<TARGET_FILE_DIR:notepad-colon>/Lexilla.dll")
endif()
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
    add_executable(notepad_colon_performance_tests tests/performance_tests.cpp)
    target_link_libraries(notepad_colon_performance_tests PRIVATE notepad_colon_model)
    target_compile_features(notepad_colon_performance_tests PRIVATE cxx_std_20)
    target_compile_options(notepad_colon_performance_tests PRIVATE /W4 /permissive- /EHsc /utf-8 /WX)
    add_test(NAME notepad-colon.performance COMMAND notepad_colon_performance_tests)
    set_tests_properties(notepad-colon.performance PROPERTIES TIMEOUT 15 LABELS performance)
endif()

install(TARGETS notepad-colon RUNTIME DESTINATION . COMPONENT product)
if(mwfl_scintilla_binary_SOURCE_DIR)
    install(FILES
        "${mwfl_scintilla_binary_SOURCE_DIR}/Scintilla.dll"
        "${mwfl_scintilla_binary_SOURCE_DIR}/Lexilla.dll"
        DESTINATION . COMPONENT product)
endif()
install(FILES README.md LICENSE DESTINATION . COMPONENT product)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "Notepad-Colon")
set(CPACK_PACKAGE_FILE_NAME "notepad-colon-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENTS_ALL product)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
include(CPack)
