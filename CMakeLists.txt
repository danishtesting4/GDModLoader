cmake_minimum_required(VERSION 3.20)
project(GDModLoader VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)
include(FetchContent)

if(NOT WIN32 AND NOT ANDROID)
    message(FATAL_ERROR "GDModLoader supports Windows and Android targets.")
endif()
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Geometry Dash 2.2091 Windows loader requires x64.")
endif()

add_library(gdml_loader SHARED
    src/loader.cpp
)
target_compile_features(gdml_loader PUBLIC cxx_std_17)
target_include_directories(gdml_loader
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
set_target_properties(gdml_loader PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(ANDROID)
    target_link_libraries(gdml_loader PRIVATE dl)
endif()

add_library(example_mod SHARED
    examples/example_mod.cpp
)
target_compile_features(example_mod PRIVATE cxx_std_17)
target_include_directories(example_mod PRIVATE include)
set_target_properties(example_mod PROPERTIES
    PREFIX ""
    OUTPUT_NAME "example_mod"
)

if(WIN32)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/MinHook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(minhook)

    find_program(GDML_POWERSHELL_EXECUTABLE NAMES powershell pwsh REQUIRED)
    set(GDML_GENERATED_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${GDML_GENERATED_ASSETS}")
    execute_process(
        COMMAND "${GDML_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/assets/generate-icon.ps1"
            -OutputDirectory "${GDML_GENERATED_ASSETS}"
        RESULT_VARIABLE GDML_ICON_RESULT
    )
    if(NOT GDML_ICON_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not generate the GDModLoader icon.")
    endif()
    execute_process(
        COMMAND "${GDML_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/assets/generate-menu-button.ps1"
            -OutputPath "${GDML_GENERATED_ASSETS}/gdml-button.png"
        RESULT_VARIABLE GDML_MENU_BUTTON_RESULT
    )
    if(NOT GDML_MENU_BUTTON_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not generate the GDML menu button asset.")
    endif()
    file(TO_CMAKE_PATH "${GDML_GENERATED_ASSETS}/gdml.ico" GDML_ICON_RESOURCE_PATH)
    file(WRITE "${GDML_GENERATED_ASSETS}/gdml_icon.rc"
        "1 ICON \"${GDML_ICON_RESOURCE_PATH}\"\n"
    )

    target_compile_definitions(gdml_loader PRIVATE GDML_BUILDING_LOADER)

    add_library(gdml_bootstrap SHARED
        src/bootstrap.cpp
        src/game_ui.cpp
    )
    target_compile_features(gdml_bootstrap PRIVATE cxx_std_17)
    target_include_directories(gdml_bootstrap PRIVATE include)
    target_link_libraries(gdml_bootstrap PRIVATE gdml_loader minhook)
    add_custom_command(TARGET gdml_bootstrap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:gdml_bootstrap>/GDMods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GDML_GENERATED_ASSETS}/gdml-button.png"
            "$<TARGET_FILE_DIR:gdml_bootstrap>/GDMods/gdml-button.png"
    )

    add_library(gdml_qol_mod SHARED
        src/qol_mod.cpp
    )
    target_compile_features(gdml_qol_mod PRIVATE cxx_std_17)
    target_include_directories(gdml_qol_mod PRIVATE include)
    target_link_libraries(gdml_qol_mod PRIVATE gdml_loader minhook)
    set_target_properties(gdml_qol_mod PROPERTIES
        PREFIX ""
        OUTPUT_NAME "gdml_qol"
    )
    add_custom_command(TARGET gdml_qol_mod POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:gdml_qol_mod>/GDMods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:gdml_qol_mod>"
            "$<TARGET_FILE_DIR:gdml_qol_mod>/GDMods/gdml_qol.dll"
    )

    add_executable(gdml_launcher
        src/launcher.cpp
        "${GDML_GENERATED_ASSETS}/gdml_icon.rc"
    )
    target_compile_features(gdml_launcher PRIVATE cxx_std_17)
    target_link_libraries(gdml_launcher PRIVATE Shell32)
endif()

install(TARGETS gdml_loader
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
if(WIN32)
    install(TARGETS gdml_bootstrap gdml_launcher gdml_qol_mod
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
    install(FILES "$<TARGET_FILE:gdml_qol_mod>" DESTINATION bin/GDMods)
    install(FILES "${GDML_GENERATED_ASSETS}/gdml-button.png" DESTINATION bin/GDMods)
    install(FILES "${GDML_GENERATED_ASSETS}/gdml.ico" assets/gdml.svg DESTINATION share/gdml)
    install(FILES "${minhook_SOURCE_DIR}/LICENSE.txt"
        DESTINATION share/licenses/GDModLoader RENAME MinHook-LICENSE.txt
    )
    if(BUILD_TESTING)
        add_executable(gdml_test_game tests/loader_fixture.cpp)
        target_compile_features(gdml_test_game PRIVATE cxx_std_17)
        add_dependencies(gdml_test_game gdml_launcher example_mod)
        add_custom_command(TARGET gdml_test_game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:gdml_test_game>/GDMods"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:example_mod>"
                "$<TARGET_FILE_DIR:gdml_test_game>/GDMods/example_mod.dll"
        )
        add_test(
            NAME gdml_launcher_integration
            COMMAND gdml_launcher "$<TARGET_FILE:gdml_test_game>"
        )
        set_tests_properties(gdml_launcher_integration PROPERTIES TIMEOUT 75)
    endif()
endif()
install(DIRECTORY include/ DESTINATION include)
