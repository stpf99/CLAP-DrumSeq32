cmake_minimum_required(VERSION 3.16)
project(DrumSeq32 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Visibility: only export clap_entry
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

add_library(DrumSeq32 MODULE
    plugin.cpp
    sample.cpp
    sequencer.cpp
    gui_gtk.cpp
)

target_include_directories(DrumSeq32 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # for "plugin.h", "third_party/..."
    ${CMAKE_CURRENT_SOURCE_DIR}           # for <clap/...>  (clap/ lives here)
)

target_link_libraries(DrumSeq32 PRIVATE PkgConfig::GTK3)

# Linux .clap = shared library without "lib" prefix
set_target_properties(DrumSeq32 PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "DrumSeq32"
)

# Optional install into user clap path
install(TARGETS DrumSeq32 LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/clap")

message(STATUS "Build DrumSeq32.clap then copy to ~/.clap/ or /usr/lib/clap/")
