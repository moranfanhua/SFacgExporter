# Use a stable preset path even when VCPKG_ROOT is not set.
set(SFE_VCPKG_ROOT "" CACHE PATH "vcpkg installation (auto-detected when empty)")
if(SFE_VCPKG_ROOT STREQUAL "")
  set(_sfe_vcpkg_candidates)
  foreach(_sfe_variable VCPKG_ROOT VCPKG_INSTALLATION_ROOT)
    if(DEFINED ENV{${_sfe_variable}} AND NOT "$ENV{${_sfe_variable}}" STREQUAL "")
      list(APPEND _sfe_vcpkg_candidates "$ENV{${_sfe_variable}}")
    endif()
  endforeach()
  list(APPEND _sfe_vcpkg_candidates "${CMAKE_CURRENT_LIST_DIR}/../build-tools/vcpkg")

  if(CMAKE_HOST_WIN32)
    if(CMAKE_GENERATOR_INSTANCE)
      list(APPEND _sfe_vcpkg_candidates "${CMAKE_GENERATOR_INSTANCE}/VC/vcpkg")
    endif()
    if(DEFINED ENV{VSINSTALLDIR})
      list(APPEND _sfe_vcpkg_candidates "$ENV{VSINSTALLDIR}/VC/vcpkg")
    endif()
    find_program(_sfe_vswhere NAMES vswhere.exe
      HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer" NO_CACHE)
    if(_sfe_vswhere)
      execute_process(COMMAND "${_sfe_vswhere}" -all -products * -property installationPath
        OUTPUT_VARIABLE _sfe_vs_installations OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _sfe_vswhere_result)
      if(_sfe_vswhere_result EQUAL 0)
        string(REPLACE "\r" "" _sfe_vs_installations "${_sfe_vs_installations}")
        string(REPLACE "\n" ";" _sfe_vs_installations "${_sfe_vs_installations}")
        foreach(_sfe_installation IN LISTS _sfe_vs_installations)
          list(APPEND _sfe_vcpkg_candidates "${_sfe_installation}/VC/vcpkg")
        endforeach()
      endif()
    endif()
  endif()

  find_program(_sfe_vcpkg_executable NAMES vcpkg vcpkg.exe NO_CACHE)
  if(_sfe_vcpkg_executable)
    get_filename_component(_sfe_vcpkg_directory "${_sfe_vcpkg_executable}" DIRECTORY)
    list(APPEND _sfe_vcpkg_candidates "${_sfe_vcpkg_directory}")
  endif()

  foreach(_sfe_candidate IN LISTS _sfe_vcpkg_candidates)
    if(EXISTS "${_sfe_candidate}/scripts/buildsystems/vcpkg.cmake")
      get_filename_component(_sfe_candidate "${_sfe_candidate}" REALPATH)
      set(SFE_VCPKG_ROOT "${_sfe_candidate}" CACHE PATH "vcpkg installation" FORCE)
      break()
    endif()
  endforeach()
endif()

if(NOT EXISTS "${SFE_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  message(FATAL_ERROR
    "vcpkg was not found. Install the Visual Studio vcpkg component, set VCPKG_ROOT, "
    "or pass -DSFE_VCPKG_ROOT=<vcpkg-directory>.")
endif()

# Reuse the selected installation during compiler try_compile calls.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SFE_VCPKG_ROOT)
set(SFE_VCPKG_TOOLCHAIN_LOADED TRUE)
include("${SFE_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
