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

#ifdef XR_USE_GRAPHICS_API_OPENGL
#ifdef _WIN32
#include <GL/gl.h>
#elif !defined(__ANDROID__)
#include <GL/glx.h>
#endif
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

// Always include Vulkan if the OpenXR Vulkan binding is enabled
// (this is controlled by CMake, not ENABLE_VULKAN)
#ifdef XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
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
#include "Common/GL/GLContext.h"
#ifdef _WIN32
#include "Common/GL/GLInterface/WGL.h"
#elif !defined(__ANDROID__)
#include "Common/GL/GLInterface/GLX.h"
#endif
#endif

#ifdef ENABLE_VULKAN
#include "VideoBackends/Vulkan/VKGfx.h"
#include "VideoBackends/Vulkan/VulkanContext.h"
extern std::unique_ptr<Vulkan::VulkanContext> g_vulkan_context;
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

#ifdef ENABLE_VULKAN
  if (api == APIType::Vulkan)
  {
    requested_extensions.push_back("XR_KHR_vulkan_enable");
  }
#endif

  // Create instance
  XrInstanceCreateInfo create_info{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strcpy(create_info.applicationInfo.applicationName, "Dolphin Emulator");
  create_info.applicationInfo.applicationVersion = 1;
  std::strcpy(create_info.applicationInfo.engineName, "Dolphin");
  create_info.applicationInfo.engineVersion = 1;
  create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
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

#ifdef ENABLE_VULKAN
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

  XrGraphicsBindingOpenGLXlibKHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
  graphics_binding.xDisplay = glXGetCurrentDisplay();
  graphics_binding.visualid = 0;  // Can be 0 for off-screen contexts
  graphics_binding.glxFBConfig = 0;  // Can be 0 for off-screen contexts
  graphics_binding.glxDrawable = glXGetCurrentDrawable();
  graphics_binding.glxContext = glXGetCurrentContext();

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

#ifdef ENABLE_VULKAN
bool VRManager::CreateVulkanSession(::AbstractGfx* gfx)
{
  if (!g_vulkan_context)
  {
    ERROR_LOG_FMT(VIDEO, "Vulkan context not available");
    return false;
  }

  XrGraphicsBindingVulkanKHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  graphics_binding.instance = g_vulkan_context->GetVulkanInstance();
  graphics_binding.physicalDevice = g_vulkan_context->GetPhysicalDevice();
  graphics_binding.device = g_vulkan_context->GetDevice();
  graphics_binding.queueFamilyIndex = g_vulkan_context->GetGraphicsQueueFamilyIndex();
  graphics_binding.queueIndex = 0;

  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &graphics_binding;
  session_info.systemId = m_system_id;

  XR_CHECK(xrCreateSession(m_instance, &session_info, &m_session), "xrCreateSession");

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

#ifdef ENABLE_VULKAN
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

  // Copy left eye texture to swapchain
  if (left_eye)
    CopyTextureToSwapchain(left_eye, 0);

  // Copy right eye texture to swapchain
  if (right_eye)
    CopyTextureToSwapchain(right_eye, 1);

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

  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = m_frame_state_predicted_display_time;
  end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end_info.layerCount = 1;
  end_info.layers = layers;

  xrEndFrame(m_session, &end_info);
}

void VRManager::EndFrame()
{
  // Frame end is handled in SubmitFrame
}

void VRManager::CopyTextureToSwapchain(::AbstractTexture* src, int eye_index)
{
  if (!src || eye_index < 0 || eye_index >= 2)
    return;

  // Acquire swapchain image
  XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  uint32_t image_index = 0;
  xrAcquireSwapchainImage(m_swapchains[eye_index], &acquire_info, &image_index);

  // Wait for swapchain image to be ready
  XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait_info.timeout = XR_INFINITE_DURATION;
  xrWaitSwapchainImage(m_swapchains[eye_index], &wait_info);

  // TODO: Implement actual texture copy using graphics API
  // For OpenGL: use glBlitFramebuffer or glCopyImageSubData
  // For Vulkan: use vkCmdBlitImage or vkCmdCopyImage
  // This requires creating framebuffers/image views for the swapchain images

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
