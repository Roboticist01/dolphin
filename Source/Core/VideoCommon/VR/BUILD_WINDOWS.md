# Building Dolphin VR Support on Windows

This guide explains how to build Dolphin with OpenXR VR support on Windows.

## Prerequisites

### Required Software

1. **Visual Studio 2022** (or 2019)
   - Install "Desktop development with C++" workload
   - Install Windows 10/11 SDK

2. **CMake** 3.20 or later
   - Download from: https://cmake.org/download/
   - Add to PATH during installation

3. **Git**
   - Download from: https://git-scm.com/download/win
   - Or use GitHub Desktop

4. **VR Runtime** (at least one):
   - **SteamVR** (recommended): Provides OpenXR support
     - Install Steam and then SteamVR from the Steam store
     - SteamVR includes OpenXR loader automatically
   - **Oculus PC Software**: For Meta Quest via Link/Air Link
     - Download from Meta website
     - Also provides OpenXR runtime

### Optional: OpenXR SDK

While not strictly required (headers are included), you can install the full SDK:

- Download from: https://github.com/KhronosGroup/OpenXR-SDK
- Or install via vcpkg: `vcpkg install openxr-loader:x64-windows`

## Building

### Step 1: Clone the Repository

```cmd
git clone https://github.com/YOUR_USERNAME/dolphin.git
cd dolphin
git checkout claude/add-quest-vr-support-02sU5
git submodule update --init --recursive
```

### Step 2: Generate Visual Studio Solution

Create a build directory and run CMake:

```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DENABLE_VR=ON
```

Options:
- `-G "Visual Studio 17 2022"`: Use VS 2022 (use `"Visual Studio 16 2019"` for VS 2019)
- `-A x64`: Build 64-bit version
- `-DENABLE_VR=ON`: Enable VR support (default)
- `-DENABLE_VR=OFF`: Disable VR support if not needed

### Step 3: Build

#### Option A: Visual Studio IDE

1. Open `build/dolphin-emu.sln` in Visual Studio
2. Set build configuration to `Release` (or `Debug` for development)
3. Right-click on `dolphin-emu` project → `Set as Startup Project`
4. Press F7 to build or F5 to build and run

#### Option B: Command Line

```cmd
cmake --build . --config Release
```

For parallel build (faster):
```cmd
cmake --build . --config Release --parallel
```

### Step 4: Run

The built executable will be in:
- Release: `build/Binaries/Release/Dolphin.exe`
- Debug: `build/Binaries/Debug/Dolphin.exe`

## Configuration

### Enabling VR

1. Launch Dolphin
2. Go to `Graphics Settings` → `General`
3. Enable stereoscopic rendering (Side-by-Side or Top-and-Bottom)
4. Set `VR Enabled` to `True` in config file

Or edit `User/Config/Dolphin.ini`:
```ini
[VR]
VREnabled = True
ScreenDistance = 2.0
ScreenSize = 4.0
ScreenCurved = False

[Stereoscopy]
StereoMode = 1  # 1=SBS, 2=TAB
StereoDepth = 20
StereoConvergence = 20
```

### VR Settings

**Screen Distance**: How far away the virtual screen appears (in meters)
- Default: 2.0
- Range: 0.5 - 10.0
- Recommended: 1.5 - 3.0 for comfortable viewing

**Screen Size**: Width of the virtual screen (in meters)
- Default: 4.0 (like a large home theater)
- Range: 1.0 - 10.0
- Recommended: 3.0 - 5.0

**Screen Curved**: Whether the screen is curved
- Default: False
- Currently not implemented (future feature)

## VR Runtime Setup

### SteamVR (Recommended)

1. Install Steam if not already installed
2. Install SteamVR from Steam store (free)
3. Connect your VR headset:
   - **Quest 3/2 via Link**:
     - Use USB-C cable (Quest Link) or WiFi (Air Link)
     - Enable Link in Quest settings
   - **Valve Index/HTC Vive**: Connect via DisplayPort and USB
4. Launch SteamVR (it will auto-start when you launch Dolphin with VR enabled)
5. Put on headset and launch a game in Dolphin

### Oculus PC Software

1. Download and install Oculus PC software
2. Connect Quest via USB-C cable
3. Enable Link in the headset
4. Set Oculus as OpenXR runtime:
   ```cmd
   "C:\Program Files\Oculus\Support\oculus-runtime\OculusSetup.exe" /setdefaultruntime
   ```
5. Launch Dolphin and start a game

### Switching OpenXR Runtime

Windows can have multiple OpenXR runtimes. To switch:

**Set SteamVR as default:**
- Open SteamVR Settings → Show Advanced Settings → Developer
- Click "Set SteamVR as OpenXR Runtime"

**Set Oculus as default:**
- Run Oculus Setup with `/setdefaultruntime` flag (see above)

## Troubleshooting

### "Failed to create OpenXR instance"

**Causes:**
- No VR runtime installed
- VR runtime not set as active OpenXR runtime
- VR headset not connected

**Solutions:**
1. Install SteamVR or Oculus PC software
2. Ensure headset is connected and powered on
3. Set the runtime as default OpenXR runtime
4. Check Windows Event Viewer for OpenXR errors

### "OpenXR loader not found"

**Solution:**
- Install SteamVR (includes OpenXR loader)
- Or install Oculus PC software
- The loader is provided by the VR runtime, not Dolphin

### "Failed to create OpenXR session"

**Causes:**
- Graphics driver issue
- Graphics API mismatch
- VR compositor not running

**Solutions:**
1. Update your graphics drivers (NVIDIA/AMD)
2. Ensure SteamVR or Oculus software is running
3. Try different graphics backend:
   - Graphics Settings → Backend → Change between OpenGL/Vulkan
4. Restart VR runtime and Dolphin

### Build Errors

**"openxr.h not found":**
- Ensure you ran `git submodule update --init --recursive`
- Check that `Externals/OpenXR-SDK/include/` contains headers

**Linker errors about OpenXR:**
- This is usually okay - OpenXR loader is dynamically loaded
- Ensure VR runtime (SteamVR/Oculus) is installed

**GLX/WGL errors:**
- These are Linux/Windows specific - only the platform you're building for needs to compile

## Performance Tips

1. **Resolution**: Start with native Quest resolution (1832x1920 per eye for Quest 3)
   - Lower internal resolution in Dolphin if needed

2. **Graphics Backend**:
   - Vulkan generally performs better than OpenGL
   - Try both to see which works best for your GPU

3. **Frame Rate**:
   - Quest 3 runs at 90Hz or 120Hz
   - Ensure Dolphin can maintain the headset's refresh rate
   - Enable VSync in Dolphin graphics settings

4. **Stereo Depth**:
   - Start with default values (Depth: 20, Convergence: 20)
   - Adjust per-game for comfort

## Development/Debugging

### Debug Build

```cmd
cmake --build . --config Debug
```

### Enable OpenXR Logging

Set environment variable before running Dolphin:
```cmd
set XR_ENABLE_API_LAYERS=XR_APILAYER_LUNARG_core_validation
Dolphin.exe
```

### View Dolphin Logs

VR initialization logs are in:
```
User/Logs/dolphin.log
```

Search for "OpenXR" or "VR" to find relevant messages.

## System Requirements

**Minimum:**
- Windows 10 64-bit (1903 or later)
- DirectX 12 compatible GPU
- 8 GB RAM
- VR Headset with OpenXR support
- USB 3.0 port (for Quest Link)

**Recommended:**
- Windows 11 64-bit
- NVIDIA RTX 3060 or AMD RX 6600 XT or better
- 16 GB RAM
- WiFi 6 router (for Quest Air Link)

## Support

**Dolphin VR Issues:**
- GitHub: https://github.com/YOUR_USERNAME/dolphin/issues
- Include dolphin.log and system info

**OpenXR/SteamVR Issues:**
- SteamVR forums: https://steamcommunity.com/app/250820/discussions/
- Oculus Support: https://support.oculus.com/

## Credits

OpenXR integration by Claude (2026)
Based on Dolphin Emulator by Dolphin Team
OpenXR specification by Khronos Group
