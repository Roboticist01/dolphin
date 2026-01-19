// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef HAS_OPENXR

#include <memory>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Matrix.h"

// Forward declarations to avoid including OpenXR in header
typedef struct XrInstance_T* XrInstance;
typedef struct XrSession_T* XrSession;
typedef struct XrSpace_T* XrSpace;
typedef struct XrSwapchain_T* XrSwapchain;
typedef struct XrView_T* XrView;
typedef struct XrCompositionLayerProjection_T* XrCompositionLayerProjection;
typedef uint64_t XrSystemId;
typedef int64_t XrTime;

namespace VideoCommon
{
class AbstractTexture;
class AbstractGfx;

// VRManager handles OpenXR integration for displaying Dolphin's stereoscopic output in VR
class VRManager
{
public:
  VRManager();
  ~VRManager();

  // Initialize OpenXR and create session
  bool Initialize(AbstractGfx* gfx);

  // Shutdown and cleanup OpenXR resources
  void Shutdown();

  // Check if VR is initialized and ready
  bool IsInitialized() const { return m_initialized; }
  bool IsSessionRunning() const { return m_session_running; }

  // Frame lifecycle
  bool BeginFrame();                          // Wait for VR compositor to be ready
  void SubmitFrame(AbstractTexture* left_eye, // Submit stereo textures to VR
                   AbstractTexture* right_eye);
  void EndFrame();                            // Complete frame submission

  // Configuration
  void SetVirtualScreenDistance(float meters);
  void SetVirtualScreenSize(float width_meters, float height_meters);
  void SetVirtualScreenCurved(bool curved) { m_screen_curved = curved; }

  // Get recommended render resolution per eye
  void GetRecommendedRenderSize(u32& width, u32& height) const;

private:
  bool CreateInstance();
  bool CreateSession(AbstractGfx* gfx);
  bool CreateSwapchains();
  bool CreateReferenceSpace();

  void DestroySwapchains();
  void PollEvents();

  bool CreateOpenGLSession(AbstractGfx* gfx);
  bool CreateVulkanSession(AbstractGfx* gfx);

  // Helper to copy texture to swapchain
  void CopyTextureToSwapchain(AbstractTexture* src, int eye_index);

  // OpenXR handles
  XrInstance m_instance{};
  XrSession m_session{};
  XrSystemId m_system_id{};
  XrSpace m_reference_space{};
  XrSwapchain m_swapchains[2]{};  // Left and right eye swapchains

  // State
  bool m_initialized = false;
  bool m_session_running = false;
  AbstractGfx* m_gfx = nullptr;

  // Configuration
  float m_screen_distance = 2.0f;       // Distance in meters
  float m_screen_width = 4.0f;          // Width in meters (simulates large screen)
  float m_screen_height = 2.25f;        // Height in meters (16:9 aspect)
  bool m_screen_curved = false;

  // Swapchain info
  int64_t m_swapchain_format = 0;
  u32 m_swapchain_width = 0;
  u32 m_swapchain_height = 0;
  std::vector<u32> m_swapchain_images[2];  // OpenGL texture IDs or Vulkan image handles

  // Frame state
  XrTime m_frame_state_predicted_display_time = 0;
  bool m_should_render = false;

  // View and projection info
  struct ViewInfo
  {
    float position[3];
    float orientation[4];  // Quaternion
    float fov_left;
    float fov_right;
    float fov_up;
    float fov_down;
  };
  ViewInfo m_views[2];  // Left and right eye view info
};

}  // namespace VideoCommon

#endif  // HAS_OPENXR
