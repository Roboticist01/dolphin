// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef HAS_OPENXR

#include "VideoCommon/VR/VRManager.h"

#include <algorithm>
#include <cstring>

#define XR_USE_GRAPHICS_API_OPENGL
#ifdef _WIN32
#define XR_USE_PLATFORM_WIN32
#elif defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#else
#define XR_USE_PLATFORM_XLIB
#endif

#ifdef ENABLE_VULKAN
#define XR_USE_GRAPHICS_API_VULKAN
#endif

#include <openxr.h>
#include <openxr_platform.h>

#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"

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

bool VRManager::Initialize(AbstractGfx* gfx)
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

  // Request required extensions
  std::vector<const char*> requested_extensions;

#if defined(XR_USE_GRAPHICS_API_OPENGL) && defined(XR_USE_PLATFORM_XLIB)
  requested_extensions.push_back("XR_KHR_opengl_enable");
#elif defined(XR_USE_GRAPHICS_API_OPENGL) && defined(_WIN32)
  requested_extensions.push_back("XR_KHR_opengl_enable");
#elif defined(XR_USE_GRAPHICS_API_VULKAN)
  requested_extensions.push_back("XR_KHR_vulkan_enable");
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

bool VRManager::CreateSession(AbstractGfx* gfx)
{
  // TODO: Set up graphics binding based on the backend (OpenGL/Vulkan)
  // For now, we'll create a placeholder session creation

  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.systemId = m_system_id;

  // Graphics binding will be added here based on backend
  // This is a placeholder - we'll implement the actual graphics binding next

  XR_CHECK(xrCreateSession(m_instance, &session_info, &m_session), "xrCreateSession");

  INFO_LOG_FMT(VIDEO, "OpenXR session created");

  return true;
}

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

  // TODO: Choose format based on backend
  int64_t chosen_format = formats[0];

  // Get recommended render size
  XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  XrSystemProperties system_props{XR_TYPE_SYSTEM_PROPERTIES};
  xrGetSystemProperties(m_instance, m_system_id, &system_props);

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
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.format = chosen_format;
    swapchain_info.sampleCount = 1;
    swapchain_info.width = m_swapchain_width;
    swapchain_info.height = m_swapchain_height;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = 1;
    swapchain_info.mipCount = 1;

    XR_CHECK(xrCreateSwapchain(m_session, &swapchain_info, &m_swapchains[eye]),
             "xrCreateSwapchain");
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

  return m_should_render;
}

void VRManager::SubmitFrame(AbstractTexture* left_eye, AbstractTexture* right_eye)
{
  // TODO: Implement frame submission
  // This will copy the left_eye and right_eye textures to the VR swapchains
  // and submit them to the compositor
}

void VRManager::EndFrame()
{
  if (!m_initialized || !m_session)
    return;

  // End frame
  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = m_frame_state_predicted_display_time;
  end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

  xrEndFrame(m_session, &end_info);
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
