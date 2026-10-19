cmake_minimum_required(VERSION 3.20)
project(MyFirstGDMLMod VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED GDML_SDK_ROOT)
    message(FATAL_ERROR
        "Set GDML_SDK_ROOT to the GDModLoader source or SDK directory."
    )
endif()

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Geometry Dash 2.2091 mods must be built for x64.")
endif()

add_library(my_first_mod SHARED
    src/mod.cpp
)
target_compile_features(my_first_mod PRIVATE cxx_std_17)
target_include_directories(my_first_mod PRIVATE "${GDML_SDK_ROOT}/include")
set_target_properties(my_first_mod PROPERTIES
    PREFIX ""
    OUTPUT_NAME "my_first_mod"
)
