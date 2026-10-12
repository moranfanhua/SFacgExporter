add_library(SFECore SHARED
  core.cpp
  json.cpp
  util.cpp
  codec.cpp
  transport.cpp
  ebook.cpp
  ${PROJECT_SOURCE_DIR}/include/SFECore.h
  ${PROJECT_SOURCE_DIR}/include/SFECore/json.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/util.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/codec.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/transport.hpp
  ${PROJECT_SOURCE_DIR}/include/SFECore/ebook.hpp
)
target_include_directories(SFECore PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(SFECore PRIVATE cxx_std_20)
target_compile_definitions(SFECore PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(SFECore PRIVATE winhttp bcrypt crypt32 ole32)
set_target_properties(SFECore PROPERTIES
  PREFIX ""
  OUTPUT_NAME "SFECore"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(SFECore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib"
  )
endforeach()
target_compile_options(SFECore PRIVATE /utf-8 /W4)
set_property(TARGET SFECore PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

install(TARGETS SFECore RUNTIME DESTINATION . ARCHIVE DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)
