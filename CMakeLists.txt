cmake_minimum_required(VERSION 4.2)
project(SFExporter VERSION 0.10.1 LANGUAGES CXX)

if(CMAKE_TOOLCHAIN_FILE STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-toolchain.cmake"
   AND NOT SFE_VCPKG_TOOLCHAIN_LOADED)
  message(FATAL_ERROR
    "This build directory still uses a configuration without vcpkg. "
    "Run cmake --fresh --preset <preset-name> once to reload the toolchain.")
endif()

if(NOT WIN32 AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "Supported targets: Windows, Linux, macOS and Android")
endif()
if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR "Use the windows-x64 preset with the Visual Studio 2026 C++ toolset")
endif()

add_subdirectory(src/SFECore)

if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  include_external_msproject(SFEAvalonia ${PROJECT_SOURCE_DIR}/src/SFEAvalonia/SFEAvalonia.csproj)
  add_dependencies(SFEAvalonia SFECore)
  set_target_properties(SFEAvalonia PROPERTIES
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
  )
endif()

configure_file(resource/cipher-table.json bin/resource/cipher-table.json COPYONLY)
configure_file(resource/cacert.pem bin/resource/cacert.pem COPYONLY)
install(DIRECTORY resource DESTINATION .)
