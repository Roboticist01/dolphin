# Dolphin VR Support (OpenXR)

This implementation adds OpenXR-based VR support to Dolphin Emulator, allowing stereoscopic 3D games to be displayed on VR headsets like Meta Quest 3.

## Overview

The VR system integrates with Dolphin's existing stereoscopic rendering to provide an immersive virtual screen experience in VR. Instead of viewing games on a traditional display, users can wear a VR headset and see the game on a virtual screen floating in 3D space, with full stereoscopic depth.

## Architecture

### Components

1. **VRManager** (`VRManager.h/cpp`):
   - Handles OpenXR initialization and session management
   - Creates and manages VR swapchains for left/right eyes
   - Manages the virtual screen in VR space
   - Handles frame submission to the VR compositor

2. **Configuration** (`Core/Config/GraphicsSettings.h/cpp`):
   - `GFX_VR_ENABLED`: Enable/disable VR output
   - `GFX_VR_SCREEN_DISTANCE`: Distance of virtual screen (meters)
   - `GFX_VR_SCREEN_SIZE`: Size of virtual screen (meters)
   - `GFX_VR_SCREEN_CURVED`: Use curved screen (future feature)

3. **VideoConfig** (`VideoCommon/VideoConfig.h/cpp`):
   - Runtime VR settings integrated with existing video configuration
   - VR settings automatically loaded from config system

## How It Works

1. **Stereo Rendering**: Dolphin's existing stereo rendering creates separate left/right eye images using a 2-layer texture array (Layer 0 = left eye, Layer 1 = right eye)

2. **VR Integration**: When VR is enabled:
   - VRManager initializes an OpenXR session
   - Creates swapchains for each eye with recommended resolution
   - Each frame, the stereo layers are copied to VR swapchains
   - Frames are submitted to the VR compositor for display

3. **Virtual Screen**: The game output is rendered onto a virtual flat screen positioned in front of the user in VR space, simulating watching a large TV

## Usage

### Requirements

- VR Headset with OpenXR support (Quest 3, Quest 2, Index, etc.)
- SteamVR or Meta Quest Link (for PC VR)
- OpenXR runtime installed on the system

### Configuration

1. Enable stereoscopic rendering in Dolphin:
   - Graphics Settings → Enhancements → Stereoscopy
   - Choose a stereo mode (Side-by-Side or Top-and-Bottom work, but VR mode auto-selects best)

2. Enable VR output:
   - Set `GFX_VR_ENABLED=True` in Dolphin config
   - Adjust screen distance and size as desired

3. Launch a game and put on your VR headset!

## Implementation Status

### Completed
- ✅ OpenXR SDK integration as external dependency
- ✅ VRManager base structure
- ✅ Configuration system for VR settings
- ✅ CMake build system integration

### In Progress
- ⚠️ OpenXR session initialization (basic structure done, needs graphics binding)
- ⚠️ Graphics API binding (OpenGL/Vulkan)
- ⚠️ Virtual screen rendering
- ⚠️ Frame submission and compositor integration

### TODO
- ⬜ OpenGL graphics binding for OpenXR
- ⬜ Vulkan graphics binding for OpenXR
- ⬜ Stereo texture copy to VR swapchains
- ⬜ Virtual screen quad rendering in VR space
- ⬜ Integration with Presenter class
- ⬜ GUI settings panel for VR options
- ⬜ Per-eye resolution optimization
- ⬜ Curved screen support
- ⬜ Performance optimizations
- ⬜ Android/Quest native support (future)

## Technical Details

### OpenXR Integration

The implementation uses OpenXR 1.0+ with the following extensions:
- `XR_KHR_opengl_enable`: OpenGL graphics binding
- `XR_KHR_vulkan_enable`: Vulkan graphics binding

### Graphics Binding

VRManager needs to interface with the current graphics backend (OpenGL or Vulkan) to:
1. Share texture resources between Dolphin and OpenXR
2. Create compatible swapchain images
3. Copy stereo layers to VR swapchains

This requires platform-specific code for each backend:

**OpenGL**:
```cpp
XrGraphicsBindingOpenGLXlibKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
binding.xDisplay = display;
binding.glxContext = context;
binding.glxDrawable = drawable;
```

**Vulkan**:
```cpp
XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
binding.instance = vkInstance;
binding.physicalDevice = vkPhysicalDevice;
binding.device = vkDevice;
binding.queueFamilyIndex = queueFamily;
binding.queueIndex = 0;
```

### Virtual Screen Rendering

The virtual screen is a quad rendered in VR space using:
- OpenXR composition layers (XrCompositionLayerQuad)
- Position: 2 meters in front of user
- Size: 4 meters wide (simulating large screen)
- Orientation: Faces the user

## Building

### Prerequisites

```bash
# Install OpenXR loader (Linux)
sudo apt-get install libopenxr-dev

# Or build will use headers from Externals/OpenXR-SDK
```

### CMake Options

```bash
cmake .. -DENABLE_VR=ON
```

To disable VR support:
```bash
cmake .. -DENABLE_VR=OFF
```

## Testing

### Test Checklist

1. ⬜ OpenXR session creation succeeds
2. ⬜ Swapchains created with correct format
3. ⬜ Stereo rendering works (test with existing stereo modes)
4. ⬜ Frames submit to VR without errors
5. ⬜ Virtual screen visible in VR
6. ⬜ Stereoscopic depth works correctly
7. ⬜ Performance is acceptable (90 FPS on Quest 3)

### Supported VR Headsets

- Meta Quest 3 / Quest 3S (via Link/Air Link or native Android)
- Meta Quest 2
- Valve Index
- HTC Vive
- Windows Mixed Reality headsets
- Any OpenXR-compatible headset

## Future Enhancements

- Room-scale VR with positional tracking
- Motion controller integration for GameCube controller
- Curved screen option for more immersive viewing
- Per-game VR profiles
- Native Android OpenXR for standalone Quest
- Performance overlay in VR
- VR menu/UI for settings adjustment

## References

- [OpenXR Specification](https://www.khronos.org/openxr/)
- [OpenXR API Guide](https://github.com/KhronosGroup/OpenXR-SDK-Source)
- [Dolphin Stereoscopic 3D](https://dolphin-emu.org/docs/guides/3d-support/)

## Credits

Implementation by Claude (2026) for Dolphin Emulator VR support.
