cmake_minimum_required(VERSION 4.2)
project(SFExporter VERSION 0.9.4 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The current HTTP and encrypted-session backend targets Windows")
endif()
if(NOT MSVC)
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
install(DIRECTORY resource DESTINATION .)
