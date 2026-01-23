# OpenXR SDK Headers
# The OpenXR loader is expected to be provided by the system or loaded dynamically

add_library(openxr-headers INTERFACE)

target_include_directories(openxr-headers
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# On Linux, link against the system OpenXR loader if available
if(UNIX AND NOT APPLE AND NOT ANDROID)
  find_library(OPENXR_LOADER openxr_loader
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
  )

  if(OPENXR_LOADER)
    message(STATUS "Found OpenXR loader: ${OPENXR_LOADER}")
    target_link_libraries(openxr-headers INTERFACE ${OPENXR_LOADER})
  else()
    # Try pkg-config
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(OPENXR openxr)
      if(OPENXR_FOUND)
        target_link_libraries(openxr-headers INTERFACE ${OPENXR_LIBRARIES})
        target_include_directories(openxr-headers INTERFACE ${OPENXR_INCLUDE_DIRS})
      else()
        message(STATUS "OpenXR loader not found on system. Linking will fail.")
        message(STATUS "To enable VR support, install: libopenxr-loader1 libopenxr-dev")
      endif()
    else()
      message(STATUS "OpenXR loader not found on system. Linking will fail.")
      message(STATUS "To enable VR support, install: libopenxr-loader1 libopenxr-dev")
    endif()
  endif()
endif()

# Windows builds use Visual Studio project files, not CMake
# OpenXR headers are provided via OpenXR-SDK.vcxproj
# No additional configuration needed here for Windows

# Define platform-specific graphics bindings (Linux/macOS only - Windows uses .vcxproj)
if(UNIX AND NOT APPLE)
  target_compile_definitions(openxr-headers INTERFACE XR_USE_PLATFORM_XLIB)
  target_compile_definitions(openxr-headers INTERFACE XR_USE_GRAPHICS_API_OPENGL)
  target_compile_definitions(openxr-headers INTERFACE XR_USE_GRAPHICS_API_VULKAN)
endif()

if(ANDROID)
  target_compile_definitions(openxr-headers INTERFACE XR_USE_PLATFORM_ANDROID)
  target_compile_definitions(openxr-headers INTERFACE XR_USE_GRAPHICS_API_OPENGL_ES)
  target_compile_definitions(openxr-headers INTERFACE XR_USE_GRAPHICS_API_VULKAN)
endif()
