add_library(SFECore SHARED
  core.cpp
  json.cpp
  util.cpp
  codec.cpp
  host.cpp
  ebook.cpp
  transport.cpp
  ${PROJECT_SOURCE_DIR}/include/SFECore.h
  ${PROJECT_SOURCE_DIR}/include/SFECore/json.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/util.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/codec.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/transport.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/ebook.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/host.hpp
)
target_include_directories(SFECore PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(SFECore PRIVATE cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(SFECore PRIVATE Threads::Threads)
find_package(CURL CONFIG REQUIRED)
target_link_libraries(SFECore PRIVATE CURL::libcurl)
if(WIN32)
  target_compile_definitions(SFECore PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(SFECore PRIVATE bcrypt crypt32 ole32)
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(SFECore PRIVATE OpenSSL::Crypto)
endif()
set_target_properties(SFECore PROPERTIES
  OUTPUT_NAME "SFECore"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
if(WIN32)
  set_target_properties(SFECore PROPERTIES PREFIX "")
endif()
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(SFECore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib"
  )
endforeach()
if(MSVC)
  target_compile_options(SFECore PRIVATE /utf-8 /W4)
  set_property(TARGET SFECore PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(SFECore PRIVATE -Wall -Wextra)
  if(ANDROID)
    # Android 15+ can use 16 KB pages. This ELF option must not reach Apple's linker.
    target_link_options(SFECore PRIVATE "-Wl,-z,max-page-size=16384")
  endif()
endif()

install(TARGETS SFECore RUNTIME DESTINATION . LIBRARY DESTINATION . ARCHIVE DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)
