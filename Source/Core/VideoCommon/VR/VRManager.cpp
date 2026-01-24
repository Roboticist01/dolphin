// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef HAS_OPENXR

#include <algorithm>
#include <cstring>

// Platform-specific headers must be included BEFORE OpenXR platform header
// because openxr_platform.h uses types from these headers

// Note: XR_USE_PLATFORM_* and XR_USE_GRAPHICS_API_* are defined by CMake
// We just need to include the right headers based on those definitions

#ifdef XR_USE_PLATFORM_WIN32
#include <windows.h>
#endif

#ifdef XR_USE_PLATFORM_XLIB
#include <X11/Xlib.h>
#endif

// For Vulkan support, include Dolphin's VulkanLoader BEFORE OpenXR headers
// VulkanLoader.h defines VK_NO_PROTOTYPES and includes vulkan/vulkan.h
#ifdef HAS_VULKAN
#include "VideoBackends/Vulkan/VulkanLoader.h"
#endif

// For OpenGL, we need GLX types for OpenXR but must avoid conflicts with Dolphin's GL headers
// Only include the minimal GL types needed for OpenXR
#ifdef XR_USE_GRAPHICS_API_OPENGL
#ifdef _WIN32
#include <GL/gl.h>
#elif !defined(__ANDROID__)
// For GLX types needed by OpenXR - included before Dolphin GL headers
#include <GL/glx.h>
#endif
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

// Now include OpenXR headers after platform headers
#include <openxr.h>
#include <openxr_platform.h>

// Include VRManager header after OpenXR (since it includes openxr.h)
#include "VideoCommon/VR/VRManager.h"

#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/VideoConfig.h"

// Backend-specific Dolphin includes
#ifdef HAS_OPENGL
#include "VideoBackends/OGL/OGLGfx.h"
#include "VideoBackends/OGL/OGLTexture.h"
#include "Common/GL/GLContext.h"
#ifdef _WIN32
#include "Common/GL/GLInterface/WGL.h"
#elif !defined(__ANDROID__)
#include "Common/GL/GLInterface/GLX.h"
#endif
#endif

#ifdef HAS_VULKAN
#include "VideoBackends/Vulkan/VKTexture.h"
#include "VideoBackends/Vulkan/VulkanContext.h"
using Vulkan::g_vulkan_context;
#endif

namespace VideoCommon
{

#define XR_CHECK(call, msg)                                                                        \
  do                                                                                               \
  {                                                                                                \
    XrResult result = call;                                                                        \
    if (XR_FAILED(result))                                                                         \
    {                                                                                              \
      ERROR_LOG_FMT(VIDEO, "OpenXR Error: {} failed with code {}", msg, static_cast<int>(result));\
      return false;                                                                                \
    }                                                                                              \
  } while (0)

VRManager::VRManager() = default;

VRManager::~VRManager()
{
  Shutdown();
}

bool VRManager::Initialize(::AbstractGfx* gfx)
{
  if (m_initialized)
    return true;

  m_gfx = gfx;

  if (!CreateInstance())
  {
    ERROR_LOG_FMT(VIDEO, "Failed to create OpenXR instance");
    return false;
  }

  if (!CreateSession(gfx))
  {
    ERROR_LOG_FMT(VIDEO, "Failed to create OpenXR session");
    Shutdown();
    return false;
  }

  if (!CreateReferenceSpace())
  {
    ERROR_LOG_FMT(VIDEO, "Failed to create OpenXR reference space");
    Shutdown();
    return false;
  }

  if (!CreateSwapchains())
  {
    ERROR_LOG_FMT(VIDEO, "Failed to create OpenXR swapchains");
    Shutdown();
    return false;
  }

  m_initialized = true;
  INFO_LOG_FMT(VIDEO, "OpenXR VR initialized successfully");

  return true;
}

void VRManager::Shutdown()
{
  if (!m_initialized)
    return;

  DestroySwapchains();

  if (m_reference_space != XR_NULL_HANDLE)
  {
    xrDestroySpace(m_reference_space);
    m_reference_space = XR_NULL_HANDLE;
  }

  if (m_session != XR_NULL_HANDLE)
  {
    xrDestroySession(m_session);
    m_session = XR_NULL_HANDLE;
  }

  if (m_instance != XR_NULL_HANDLE)
  {
    xrDestroyInstance(m_instance);
    m_instance = XR_NULL_HANDLE;
  }

  m_initialized = false;
  m_session_running = false;

  INFO_LOG_FMT(VIDEO, "OpenXR VR shut down");
}

bool VRManager::CreateInstance()
{
  // Query available extensions
  uint32_t ext_count = 0;
  xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
  std::vector<XrExtensionProperties> extensions(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});
  xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, extensions.data());

  INFO_LOG_FMT(VIDEO, "Available OpenXR extensions:");
  for (const auto& ext : extensions)
  {
    INFO_LOG_FMT(VIDEO, "  {}", ext.extensionName);
  }

  // Request required extensions based on graphics API
  std::vector<const char*> requested_extensions;

  APIType api = g_backend_info.api_type;

#ifdef HAS_OPENGL
  if (api == APIType::OpenGL)
  {
#if defined(XR_USE_PLATFORM_XLIB)
    requested_extensions.push_back("XR_KHR_opengl_enable");
#elif defined(_WIN32)
    requested_extensions.push_back("XR_KHR_opengl_enable");
#endif
  }
#endif

#ifdef HAS_VULKAN
  if (api == APIType::Vulkan)
  {
    requested_extensions.push_back("XR_KHR_vulkan_enable");
    INFO_LOG_FMT(VIDEO, "Requesting XR_KHR_vulkan_enable extension for Vulkan backend");
  }
#endif

  INFO_LOG_FMT(VIDEO, "Requesting {} OpenXR extensions", requested_extensions.size());
  for (const auto* ext : requested_extensions)
  {
    INFO_LOG_FMT(VIDEO, "  Requesting: {}", ext);
  }

  // Create instance
  XrInstanceCreateInfo create_info{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strcpy(create_info.applicationInfo.applicationName, "Dolphin Emulator");
  create_info.applicationInfo.applicationVersion = 1;
  std::strcpy(create_info.applicationInfo.engineName, "Dolphin");
  create_info.applicationInfo.engineVersion = 1;
  create_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  create_info.enabledExtensionCount = static_cast<uint32_t>(requested_extensions.size());
  create_info.enabledExtensionNames = requested_extensions.data();

  XR_CHECK(xrCreateInstance(&create_info, &m_instance), "xrCreateInstance");

  // Get system
  XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XR_CHECK(xrGetSystem(m_instance, &system_info, &m_system_id), "xrGetSystem");

  INFO_LOG_FMT(VIDEO, "OpenXR instance created, system ID: {}", static_cast<uint64_t>(m_system_id));

  return true;
}

bool VRManager::CreateSession(::AbstractGfx* gfx)
{
  APIType api = g_backend_info.api_type;

#ifdef HAS_OPENGL
  if (api == APIType::OpenGL)
  {
    return CreateOpenGLSession(gfx);
  }
#endif

#ifdef HAS_VULKAN
  if (api == APIType::Vulkan)
  {
    return CreateVulkanSession(gfx);
  }
#endif

  ERROR_LOG_FMT(VIDEO, "Unsupported graphics API for VR");
  return false;
}

#ifdef HAS_OPENGL
bool VRManager::CreateOpenGLSession(::AbstractGfx* gfx)
{
  // OpenXR requires calling xrGetOpenGLGraphicsRequirementsKHR before creating a session
  PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(
      m_instance, "xrGetOpenGLGraphicsRequirementsKHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&xrGetOpenGLGraphicsRequirementsKHR));
  if (XR_FAILED(result) || !xrGetOpenGLGraphicsRequirementsKHR)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to get xrGetOpenGLGraphicsRequirementsKHR function pointer");
    return false;
  }

  XrGraphicsRequirementsOpenGLKHR graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  XR_CHECK(xrGetOpenGLGraphicsRequirementsKHR(m_instance, m_system_id, &graphics_requirements),
           "xrGetOpenGLGraphicsRequirementsKHR");

  INFO_LOG_FMT(VIDEO, "OpenXR OpenGL requirements: min version {}.{}, max version {}.{}",
               XR_VERSION_MAJOR(graphics_requirements.minApiVersionSupported),
               XR_VERSION_MINOR(graphics_requirements.minApiVersionSupported),
               XR_VERSION_MAJOR(graphics_requirements.maxApiVersionSupported),
               XR_VERSION_MINOR(graphics_requirements.maxApiVersionSupported));

#if defined(_WIN32)
  auto* ogl_gfx = static_cast<OGL::OGLGfx*>(gfx);
  GLContext* gl_context = ogl_gfx->GetMainGLContext();
  auto* wgl_context = static_cast<GLContextWGL*>(gl_context);

  // Get WGL context info
  XrGraphicsBindingOpenGLWin32KHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
  graphics_binding.hDC = wglGetCurrentDC();
  graphics_binding.hGLRC = wglGetCurrentContext();

  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &graphics_binding;
  session_info.systemId = m_system_id;

  XR_CHECK(xrCreateSession(m_instance, &session_info, &m_session), "xrCreateSession");

#elif defined(__linux__) && !defined(__ANDROID__)
  auto* ogl_gfx = static_cast<OGL::OGLGfx*>(gfx);
  GLContext* gl_context = ogl_gfx->GetMainGLContext();
  auto* glx_context = static_cast<GLContextGLX*>(gl_context);

  // Get visualid from the FBConfig
  XVisualInfo* vi = glXGetVisualFromFBConfig(glx_context->GetDisplay(), glx_context->GetFBConfig());
  if (!vi)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to get XVisualInfo from FBConfig");
    return false;
  }
  VisualID visual_id = vi->visualid;
  XFree(vi);

  XrGraphicsBindingOpenGLXlibKHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
  graphics_binding.xDisplay = glx_context->GetDisplay();
  graphics_binding.visualid = static_cast<uint32_t>(visual_id);
  graphics_binding.glxFBConfig = glx_context->GetFBConfig();
  graphics_binding.glxDrawable = glx_context->GetDrawable();
  graphics_binding.glxContext = glx_context->GetContext();

  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &graphics_binding;
  session_info.systemId = m_system_id;

  XR_CHECK(xrCreateSession(m_instance, &session_info, &m_session), "xrCreateSession");

#else
  ERROR_LOG_FMT(VIDEO, "OpenGL VR not supported on this platform");
  return false;
#endif

  INFO_LOG_FMT(VIDEO, "OpenXR OpenGL session created");
  return true;
}
#endif

#ifdef HAS_VULKAN
bool VRManager::CreateVulkanSession(::AbstractGfx* gfx)
{
  if (!g_vulkan_context)
  {
    ERROR_LOG_FMT(VIDEO, "Vulkan context not available");
    return false;
  }

  // OpenXR requires calling xrGetVulkanGraphicsRequirementsKHR before creating a session
  PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(
      m_instance, "xrGetVulkanGraphicsRequirementsKHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsRequirementsKHR));
  if (XR_FAILED(result) || !xrGetVulkanGraphicsRequirementsKHR)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to get xrGetVulkanGraphicsRequirementsKHR function pointer");
    return false;
  }

  XrGraphicsRequirementsVulkanKHR graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
  XR_CHECK(xrGetVulkanGraphicsRequirementsKHR(m_instance, m_system_id, &graphics_requirements),
           "xrGetVulkanGraphicsRequirementsKHR");

  INFO_LOG_FMT(VIDEO, "OpenXR Vulkan requirements: min version {}.{}.{}, max version {}.{}.{}",
               VK_VERSION_MAJOR(graphics_requirements.minApiVersionSupported),
               VK_VERSION_MINOR(graphics_requirements.minApiVersionSupported),
               VK_VERSION_PATCH(graphics_requirements.minApiVersionSupported),
               VK_VERSION_MAJOR(graphics_requirements.maxApiVersionSupported),
               VK_VERSION_MINOR(graphics_requirements.maxApiVersionSupported),
               VK_VERSION_PATCH(graphics_requirements.maxApiVersionSupported));

  // Get the physical device OpenXR expects us to use
  PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
  result = xrGetInstanceProcAddr(
      m_instance, "xrGetVulkanGraphicsDeviceKHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsDeviceKHR));
  if (XR_FAILED(result) || !xrGetVulkanGraphicsDeviceKHR)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to get xrGetVulkanGraphicsDeviceKHR function pointer");
    return false;
  }

  VkPhysicalDevice xr_physical_device = VK_NULL_HANDLE;
  result = xrGetVulkanGraphicsDeviceKHR(m_instance, m_system_id,
                                        g_vulkan_context->GetVulkanInstance(),
                                        &xr_physical_device);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "xrGetVulkanGraphicsDeviceKHR failed with code {}", static_cast<int>(result));
    return false;
  }

  VkPhysicalDevice dolphin_physical_device = g_vulkan_context->GetPhysicalDevice();

  // Log device info for debugging
  INFO_LOG_FMT(VIDEO, "OpenXR expects physical device: {:p}", static_cast<void*>(xr_physical_device));
  INFO_LOG_FMT(VIDEO, "Dolphin is using physical device: {:p}", static_cast<void*>(dolphin_physical_device));

  if (xr_physical_device != dolphin_physical_device)
  {
    ERROR_LOG_FMT(VIDEO, "Physical device mismatch! OpenXR requires a different GPU than Dolphin is using.");
    ERROR_LOG_FMT(VIDEO, "This can happen with multi-GPU systems. VR may not work correctly.");
    // Continue anyway - it might still work if they're compatible
  }

  XrGraphicsBindingVulkanKHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  graphics_binding.next = nullptr;
  graphics_binding.instance = g_vulkan_context->GetVulkanInstance();
  graphics_binding.physicalDevice = g_vulkan_context->GetPhysicalDevice();
  graphics_binding.device = g_vulkan_context->GetDevice();
  graphics_binding.queueFamilyIndex = g_vulkan_context->GetGraphicsQueueFamilyIndex();
  graphics_binding.queueIndex = 0;

  // Validate all handles before proceeding
  if (graphics_binding.instance == VK_NULL_HANDLE)
  {
    ERROR_LOG_FMT(VIDEO, "VkInstance is null!");
    return false;
  }
  if (graphics_binding.physicalDevice == VK_NULL_HANDLE)
  {
    ERROR_LOG_FMT(VIDEO, "VkPhysicalDevice is null!");
    return false;
  }
  if (graphics_binding.device == VK_NULL_HANDLE)
  {
    ERROR_LOG_FMT(VIDEO, "VkDevice is null!");
    return false;
  }

  INFO_LOG_FMT(VIDEO, "Creating OpenXR session with Vulkan binding:");
  INFO_LOG_FMT(VIDEO, "  VkInstance: {:p}", static_cast<void*>(graphics_binding.instance));
  INFO_LOG_FMT(VIDEO, "  VkPhysicalDevice: {:p}", static_cast<void*>(graphics_binding.physicalDevice));
  INFO_LOG_FMT(VIDEO, "  VkDevice: {:p}", static_cast<void*>(graphics_binding.device));
  INFO_LOG_FMT(VIDEO, "  queueFamilyIndex: {}", graphics_binding.queueFamilyIndex);
  INFO_LOG_FMT(VIDEO, "  queueIndex: {}", graphics_binding.queueIndex);

  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.createFlags = 0;
  session_info.next = &graphics_binding;
  session_info.systemId = m_system_id;

  INFO_LOG_FMT(VIDEO, "Calling xrCreateSession...");
  result = xrCreateSession(m_instance, &session_info, &m_session);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "xrCreateSession failed with code {}", static_cast<int>(result));
    return false;
  }

  INFO_LOG_FMT(VIDEO, "OpenXR Vulkan session created");
  return true;
}
#endif

bool VRManager::CreateReferenceSpace()
{
  XrReferenceSpaceCreateInfo ref_space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  ref_space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  ref_space_info.poseInReferenceSpace.orientation.w = 1.0f;  // Identity rotation

  XR_CHECK(xrCreateReferenceSpace(m_session, &ref_space_info, &m_reference_space),
           "xrCreateReferenceSpace");

  INFO_LOG_FMT(VIDEO, "OpenXR reference space created");

  return true;
}

bool VRManager::CreateSwapchains()
{
  // Query recommended swapchain format and size
  uint32_t format_count = 0;
  xrEnumerateSwapchainFormats(m_session, 0, &format_count, nullptr);
  std::vector<int64_t> formats(format_count);
  xrEnumerateSwapchainFormats(m_session, format_count, &format_count, formats.data());

  if (formats.empty())
  {
    ERROR_LOG_FMT(VIDEO, "No swapchain formats available");
    return false;
  }

  // Choose appropriate format based on API
  m_swapchain_format = formats[0];
  APIType api = g_backend_info.api_type;

#ifdef HAS_OPENGL
  if (api == APIType::OpenGL)
  {
    // Prefer GL_RGBA8 or GL_SRGB8_ALPHA8
    for (int64_t fmt : formats)
    {
      if (fmt == 0x8058 || fmt == 0x8C43)  // GL_RGBA8 or GL_SRGB8_ALPHA8
      {
        m_swapchain_format = fmt;
        break;
      }
    }
  }
#endif

  // Get recommended render size
  XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  uint32_t view_count = 0;
  xrEnumerateViewConfigurationViews(m_instance, m_system_id, view_type, 0, &view_count, nullptr);
  std::vector<XrViewConfigurationView> config_views(view_count,
                                                      {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  xrEnumerateViewConfigurationViews(m_instance, m_system_id, view_type, view_count, &view_count,
                                     config_views.data());

  if (config_views.empty())
  {
    ERROR_LOG_FMT(VIDEO, "No view configurations available");
    return false;
  }

  m_swapchain_width = config_views[0].recommendedImageRectWidth;
  m_swapchain_height = config_views[0].recommendedImageRectHeight;

  INFO_LOG_FMT(VIDEO, "Recommended VR render size: {}x{}", m_swapchain_width, m_swapchain_height);

  // Create swapchains for left and right eyes
  for (int eye = 0; eye < 2; eye++)
  {
    XrSwapchainCreateInfo swapchain_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                                 XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                 XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    swapchain_info.format = m_swapchain_format;
    swapchain_info.sampleCount = 1;
    swapchain_info.width = m_swapchain_width;
    swapchain_info.height = m_swapchain_height;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = 1;
    swapchain_info.mipCount = 1;

    XR_CHECK(xrCreateSwapchain(m_session, &swapchain_info, &m_swapchains[eye]),
             "xrCreateSwapchain");

    // Enumerate swapchain images
    uint32_t image_count = 0;
    xrEnumerateSwapchainImages(m_swapchains[eye], 0, &image_count, nullptr);

#ifdef HAS_OPENGL
    if (api == APIType::OpenGL)
    {
      std::vector<XrSwapchainImageOpenGLKHR> images(image_count,
                                                      {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
      xrEnumerateSwapchainImages(m_swapchains[eye], image_count, &image_count,
                                  reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

      m_swapchain_images[eye].clear();
      for (const auto& img : images)
      {
        m_swapchain_images[eye].push_back(img.image);
      }
    }
#endif

#ifdef HAS_VULKAN
    if (api == APIType::Vulkan)
    {
      std::vector<XrSwapchainImageVulkanKHR> images(image_count,
                                                      {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
      xrEnumerateSwapchainImages(m_swapchains[eye], image_count, &image_count,
                                  reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

      m_swapchain_images[eye].clear();
      for (const auto& img : images)
      {
        m_swapchain_images[eye].push_back(static_cast<u32>(reinterpret_cast<uintptr_t>(img.image)));
      }
    }
#endif
  }

  INFO_LOG_FMT(VIDEO, "OpenXR swapchains created");

  return true;
}

void VRManager::DestroySwapchains()
{
  for (int i = 0; i < 2; i++)
  {
    if (m_swapchains[i] != XR_NULL_HANDLE)
    {
      xrDestroySwapchain(m_swapchains[i]);
      m_swapchains[i] = XR_NULL_HANDLE;
    }
    m_swapchain_images[i].clear();
  }
}

bool VRManager::BeginFrame()
{
  if (!m_initialized || !m_session)
    return false;

  PollEvents();

  if (!m_session_running)
    return false;

  // Wait for the compositor to be ready
  XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};

  XrResult result = xrWaitFrame(m_session, &wait_info, &frame_state);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "xrWaitFrame failed");
    return false;
  }

  m_frame_state_predicted_display_time = frame_state.predictedDisplayTime;
  m_should_render = frame_state.shouldRender;

  // Begin frame
  XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
  result = xrBeginFrame(m_session, &begin_info);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "xrBeginFrame failed");
    return false;
  }

  // Locate views
  XrViewLocateInfo locate_info{XR_TYPE_VIEW_LOCATE_INFO};
  locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locate_info.displayTime = m_frame_state_predicted_display_time;
  locate_info.space = m_reference_space;

  XrViewState view_state{XR_TYPE_VIEW_STATE};
  uint32_t view_count = 2;
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};

  xrLocateViews(m_session, &locate_info, &view_state, view_count, &view_count, views);

  // Store view info for rendering
  for (int i = 0; i < 2; i++)
  {
    m_views[i].position[0] = views[i].pose.position.x;
    m_views[i].position[1] = views[i].pose.position.y;
    m_views[i].position[2] = views[i].pose.position.z;
    m_views[i].orientation[0] = views[i].pose.orientation.x;
    m_views[i].orientation[1] = views[i].pose.orientation.y;
    m_views[i].orientation[2] = views[i].pose.orientation.z;
    m_views[i].orientation[3] = views[i].pose.orientation.w;
    m_views[i].fov_left = views[i].fov.angleLeft;
    m_views[i].fov_right = views[i].fov.angleRight;
    m_views[i].fov_up = views[i].fov.angleUp;
    m_views[i].fov_down = views[i].fov.angleDown;
  }

  return m_should_render;
}

void VRManager::SubmitFrame(::AbstractTexture* left_eye, ::AbstractTexture* right_eye)
{
  if (!m_initialized || !m_session || !m_should_render)
    return;

  APIType api = g_backend_info.api_type;
  bool textures_copied = false;

#ifdef HAS_OPENGL
  if (api == APIType::OpenGL)
  {
    // Copy left eye texture to swapchain
    if (left_eye)
      CopyTextureToSwapchain(left_eye, 0);

    // Copy right eye texture to swapchain
    if (right_eye)
      CopyTextureToSwapchain(right_eye, 1);

    textures_copied = true;
  }
#endif

#ifdef HAS_VULKAN
  if (api == APIType::Vulkan)
  {
    // TODO: Vulkan texture copy not yet implemented
    // For now, we still need to acquire/wait/release the swapchain images
    // but we won't submit layers since the images contain garbage
    for (int eye = 0; eye < 2; eye++)
    {
      if (m_swapchain_images[eye].empty())
        continue;

      XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      uint32_t image_index = 0;
      XrResult result = xrAcquireSwapchainImage(m_swapchains[eye], &acquire_info, &image_index);
      if (XR_FAILED(result))
        continue;

      XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      wait_info.timeout = XR_INFINITE_DURATION;
      xrWaitSwapchainImage(m_swapchains[eye], &wait_info);

      // TODO: Actually copy the texture here using Vulkan commands

      XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(m_swapchains[eye], &release_info);
    }

    textures_copied = false;  // Don't submit layers with garbage images
  }
#endif

  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = m_frame_state_predicted_display_time;
  end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

  if (textures_copied)
  {
    // Create composition layers
    XrCompositionLayerProjectionView projection_views[2] = {
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};

    for (int eye = 0; eye < 2; eye++)
    {
      projection_views[eye].pose.position.x = m_views[eye].position[0];
      projection_views[eye].pose.position.y = m_views[eye].position[1];
      projection_views[eye].pose.position.z = m_views[eye].position[2];
      projection_views[eye].pose.orientation.x = m_views[eye].orientation[0];
      projection_views[eye].pose.orientation.y = m_views[eye].orientation[1];
      projection_views[eye].pose.orientation.z = m_views[eye].orientation[2];
      projection_views[eye].pose.orientation.w = m_views[eye].orientation[3];
      projection_views[eye].fov.angleLeft = m_views[eye].fov_left;
      projection_views[eye].fov.angleRight = m_views[eye].fov_right;
      projection_views[eye].fov.angleUp = m_views[eye].fov_up;
      projection_views[eye].fov.angleDown = m_views[eye].fov_down;

      projection_views[eye].subImage.swapchain = m_swapchains[eye];
      projection_views[eye].subImage.imageRect.offset = {0, 0};
      projection_views[eye].subImage.imageRect.extent = {static_cast<int32_t>(m_swapchain_width),
                                                          static_cast<int32_t>(m_swapchain_height)};
      projection_views[eye].subImage.imageArrayIndex = 0;
    }

    XrCompositionLayerProjection projection_layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection_layer.space = m_reference_space;
    projection_layer.viewCount = 2;
    projection_layer.views = projection_views;

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection_layer)};

    end_info.layerCount = 1;
    end_info.layers = layers;

    xrEndFrame(m_session, &end_info);
  }
  else
  {
    // Submit empty frame - no layers to display
    end_info.layerCount = 0;
    end_info.layers = nullptr;

    xrEndFrame(m_session, &end_info);
  }
}

void VRManager::EndFrame()
{
  // Frame end is handled in SubmitFrame
}

void VRManager::CopyTextureToSwapchain(::AbstractTexture* src, int eye_index)
{
  if (!src || eye_index < 0 || eye_index >= 2)
    return;

  if (m_swapchain_images[eye_index].empty())
    return;

  // Acquire swapchain image
  XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  uint32_t image_index = 0;
  XrResult result = xrAcquireSwapchainImage(m_swapchains[eye_index], &acquire_info, &image_index);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "Failed to acquire swapchain image for eye {}", eye_index);
    return;
  }

  // Wait for swapchain image to be ready
  XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_info.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(m_swapchains[eye_index], &wait_info);
  if (XR_FAILED(result))
  {
    ERROR_LOG_FMT(VIDEO, "Failed to wait for swapchain image for eye {}", eye_index);
    XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(m_swapchains[eye_index], &release_info);
    return;
  }

  APIType api = g_backend_info.api_type;

#ifdef HAS_OPENGL
  if (api == APIType::OpenGL)
  {
    auto* ogl_texture = static_cast<OGL::OGLTexture*>(src);
    GLuint src_texture = ogl_texture->GetGLTextureId();
    GLenum src_target = ogl_texture->GetGLTarget();
    GLuint dst_texture = static_cast<GLuint>(m_swapchain_images[eye_index][image_index]);

    // Get source texture dimensions
    u32 src_width = src->GetWidth();
    u32 src_height = src->GetHeight();

    // Save current framebuffer bindings
    GLint prev_read_fbo = 0, prev_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

    // Create temporary framebuffers for the blit operation
    GLuint src_fbo = 0, dst_fbo = 0;
    glGenFramebuffers(1, &src_fbo);
    glGenFramebuffers(1, &dst_fbo);

    // Attach source texture to read framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
    if (src_target == GL_TEXTURE_2D_ARRAY || src_target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
    {
      // For array textures, attach layer 0
      glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src_texture, 0, 0);
    }
    else
    {
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src_target, src_texture, 0);
    }

    // Attach destination (swapchain) texture to draw framebuffer
    // OpenXR swapchain textures are typically GL_TEXTURE_2D
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           dst_texture, 0);

    // Check framebuffer completeness
    GLenum read_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    GLenum draw_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

    if (read_status == GL_FRAMEBUFFER_COMPLETE && draw_status == GL_FRAMEBUFFER_COMPLETE)
    {
      // Perform the blit
      glBlitFramebuffer(0, 0, src_width, src_height,
                        0, 0, m_swapchain_width, m_swapchain_height,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    else
    {
      if (read_status != GL_FRAMEBUFFER_COMPLETE)
        ERROR_LOG_FMT(VIDEO, "VR read framebuffer incomplete: 0x{:X}", read_status);
      if (draw_status != GL_FRAMEBUFFER_COMPLETE)
        ERROR_LOG_FMT(VIDEO, "VR draw framebuffer incomplete: 0x{:X}", draw_status);
    }

    // Restore previous framebuffer bindings
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);

    // Clean up temporary framebuffers
    glDeleteFramebuffers(1, &src_fbo);
    glDeleteFramebuffers(1, &dst_fbo);

    // Ensure all GL commands complete before releasing to OpenXR
    // Use glFinish() for stricter synchronization with the OpenXR compositor
    glFinish();
  }
#endif

#ifdef HAS_VULKAN
  if (api == APIType::Vulkan)
  {
    // TODO: Implement Vulkan texture copy
    // This requires command buffer recording and proper synchronization
  }
#endif

  // Release swapchain image
  XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(m_swapchains[eye_index], &release_info);
}

void VRManager::PollEvents()
{
  if (!m_instance)
    return;

  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(m_instance, &event) == XR_SUCCESS)
  {
    switch (event.type)
    {
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
    {
      auto* state_event = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
      switch (state_event->state)
      {
      case XR_SESSION_STATE_READY:
      {
        XrSessionBeginInfo begin_info{XR_TYPE_SESSION_BEGIN_INFO};
        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        xrBeginSession(m_session, &begin_info);
        m_session_running = true;
        INFO_LOG_FMT(VIDEO, "OpenXR session started");
        break;
      }
      case XR_SESSION_STATE_STOPPING:
        xrEndSession(m_session);
        m_session_running = false;
        INFO_LOG_FMT(VIDEO, "OpenXR session stopped");
        break;
      default:
        break;
      }
      break;
    }
    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
      INFO_LOG_FMT(VIDEO, "OpenXR instance loss pending");
      break;
    default:
      break;
    }

    event = {XR_TYPE_EVENT_DATA_BUFFER};
  }
}

void VRManager::SetVirtualScreenDistance(float meters)
{
  m_screen_distance = std::max(0.5f, meters);
}

void VRManager::SetVirtualScreenSize(float width_meters, float height_meters)
{
  m_screen_width = std::max(0.1f, width_meters);
  m_screen_height = std::max(0.1f, height_meters);
}

void VRManager::GetRecommendedRenderSize(u32& width, u32& height) const
{
  width = m_swapchain_width;
  height = m_swapchain_height;
}

}  // namespace VideoCommon

#endif  // HAS_OPENXR
