#pragma once
#include <cstdint>
#include <string>

// What the rebuilt code needs from the machine it runs on. These replace the Win32,
// DirectX, MCI and WinMM calls the original makes; each front end (SDL, Godot,
// tests) supplies an implementation. Keep this small: add a call only when ported
// code needs it, and name the original API it stands in for.
namespace platform {

// Stands in for GetTickCount(): milliseconds, but counted from when the game
// started, not from when the machine booted (see gamez/zutil/ztimer.cpp).
class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t ticks_ms() = 0;
  virtual void sleep(uint32_t ms) { (void)ms; }  // Sleep()
};

// Stands in for the CD drive behind MCI "cdaudio". Plays one audio file at a time
// and reports when it reaches the end, as MCI's MM_MCINOTIFY did.
class MusicOutput {
 public:
  virtual ~MusicOutput() = default;
  virtual bool play(const std::string& path) = 0;  // false if it cannot be played
  virtual void stop() = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void set_volume(uint16_t left, uint16_t right) = 0;  // auxSetVolume range
};

// Stands in for the CD's table of contents: is there a file at this path?
class Files {
 public:
  virtual ~Files() = default;
  virtual bool exists(const std::string& path) = 0;
};

// Stands in for the MessageBoxA the engine shows before ExitProcess when it
// must abort (0x004eada0): tell the player, the caller then quits.
class Alert {
 public:
  virtual ~Alert() = default;
  virtual void abort_message(const std::string& message) = 0;
  // MessageBoxA(hwnd, text, caption, style) for the app's own boxes (startup.md):
  // style's low 4 bits as MB_* (0 OK, 1 OK/Cancel); returns IDOK 1 or IDCANCEL 2.
  virtual int32_t message_box(const std::string& text, const std::string& caption, uint32_t style) {
    (void)style;
    abort_message(caption + ": " + text);
    return 1;
  }
};

// Stands in for the main window: CWnd::CreateEx of the "Mech3Class" frame,
// SetWindowTextA, ShowWindow/UpdateWindow and GetClientRect (docs/startup.md).
// The handle stands for the HWND the game keeps.
class Window {
 public:
  virtual ~Window() = default;
  virtual uint32_t create(const std::string& title, int width, int height) = 0;  // 0 when it fails
  virtual void set_title(const std::string& title) = 0;
  virtual void show() = 0;
  virtual void client_size(int* width, int* height) = 0;
  // FIX (A) layer: the frame the game draws (the render rect), once the
  // screen is open, so the window can take its shape (resolution.md step 2),
  // and the 2D canvas over it (the display rect; 0: the frame's size).
  virtual void frame_size(int width, int height, int canvas_w = 0, int canvas_h = 0) {
    (void)width, (void)height, (void)canvas_w, (void)canvas_h;
  }
  // FIX (B) layer: the screen a first start (no InGameVMode saved) plays at,
  // the desktop's; false keeps the original's default (checks, shots).
  virtual bool first_start_size(int* width, int* height) {
    (void)width, (void)height;
    return false;
  }
};

// Stands in for the DirectDraw primary surface the menus' 16-bit back buffer
// is flipped or blitted to (0x00576ea0, docs/twod.md section 5): shows one
// frame of 16-bit pixels, RGB565 or RGB555, `pitch` bytes a row.
class Screen2D {
 public:
  virtual ~Screen2D() = default;
  virtual void begin_overlay_16(const uint16_t*, int, int, int) {}
  virtual void cancel_overlay_16() {}
  virtual void present_16(const uint16_t* pixels, int width, int height, int pitch, bool rgb565) = 0;
  // FIX (A): the back buffer locked over a Direct3D frame (0x00576d00 /
  // 0x00576d60): what the 2D code drew into the 16-bit layer goes over the
  // 3D frame, pixels still `key` (untouched) left out. Not presented. With
  // `alpha`, a pixel whose alpha is set and whose value is still `colour`'s
  // goes over with that alpha (zrndr::LayerCover).
  virtual void overlay_16(const uint16_t* pixels, int width, int height, int pitch, bool rgb565, uint16_t key,
                          const uint8_t* alpha = nullptr, const uint16_t* colour = nullptr) {
    (void)pixels, (void)width, (void)height, (void)pitch, (void)rgb565, (void)key, (void)alpha, (void)colour;
  }
};

// Stands in for DirectInput as zInput uses it (DINPUT.dll's DirectInputCreateA,
// 0x0051b0a0): the keyboard read buffered with GetDeviceData (0x0051a5d0, 0x80
// events, DIK scan codes), the mouse with GetDeviceState into a DIMOUSESTATE
// (0x0051b7d0, relative counts) and the first attached joystick with Poll and
// GetDeviceState into a DIJOYSTATE2 (0x0051dc20). Same structures and HRESULTs.
struct KeyEvent {                  // DIDEVICEOBJECTDATA, 16 bytes
  uint32_t ofs = 0;                // the DIK code
  uint32_t data = 0;               // 0x80 down, 0 up
  uint32_t timestamp = 0;          // ms
  uint32_t sequence = 0;
};
struct MouseState {                // DIMOUSESTATE, 0x10 bytes
  int32_t x = 0, y = 0, z = 0;     // mickeys since the last read (wheel in z)
  uint8_t buttons[4] = {};         // 0x80 down: left, right, middle, fourth
};
struct JoyState {                  // DIJOYSTATE2, 0x110 bytes
  int32_t x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
  int32_t slider[2] = {};
  uint32_t pov[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};  // hundredths of a degree, -1 centred
  uint8_t buttons[128] = {};
  int32_t rest[24] = {};           // velocities, accelerations, forces: never filled
};
static_assert(sizeof(KeyEvent) == 0x10 && sizeof(MouseState) == 0x10 && sizeof(JoyState) == 0x110,
              "DirectInput's layouts");

class Input {
 public:
  enum Device : int { kKeyboard = 0, kMouse = 1, kJoystick = 2 };
  static constexpr int32_t kInputLost = static_cast<int32_t>(0x8007001e);    // DIERR_INPUTLOST
  static constexpr int32_t kNotAcquired = static_cast<int32_t>(0x8007000c);  // DIERR_NOTACQUIRED
  static constexpr int32_t kBufferOverflow = 1;                              // DI_BUFFEROVERFLOW
  static constexpr int32_t kAxisMin = 0, kAxisMax = 65535;  // DirectInput's default DIPROP_RANGE

  virtual ~Input() = default;
  virtual bool open(Device d) = 0;         // CreateDevice + SetDataFormat + SetCooperativeLevel; false: none
  virtual void close(Device d) = 0;        // Unacquire + Release
  virtual int32_t acquire(Device d) = 0;   // IDirectInputDevice::Acquire
  virtual int32_t unacquire(Device d) = 0;
  // GetDeviceData(sizeof(KeyEvent), out, count, 0): up to *count events, oldest
  // first, removed; *count becomes the number given.
  virtual int32_t keyboard_data(KeyEvent* out, uint32_t* count) = 0;
  virtual int32_t mouse_state(MouseState* out) = 0;    // GetDeviceState(0x10)
  virtual int32_t joystick_state(JoyState* out) = 0;   // Poll, then GetDeviceState(0x110)

  // SetCooperativeLevel's DISCL_ flags (1 exclusive, 2 non-exclusive, 4
  // foreground, 8 background): the keyboard 10, the mouse the game's input
  // mode (5), the joystick 5 or 9 (docs/input.md).
  virtual void set_cooperative_level(Device, uint32_t) {}
  // The joystick's GetCapabilities (DIDEVCAPS dwFlags and dwAxes),
  // GetObjectInfo by offset (DIJOFS_X 0 .. DIJOFS_RZ 0x14: the axis's name;
  // an error when the stick has no such axis), and DIPROP_RANGE /
  // DIPROP_DEADZONE on an axis by offset.
  static constexpr int32_t kObjectNotFound = static_cast<int32_t>(0x80070002);
  static constexpr int32_t kUnsupported = static_cast<int32_t>(0x80004001);
  virtual int32_t joystick_caps(uint32_t* flags, uint32_t* axes) { *flags = *axes = 0; return 0; }
  // Ours: the joystick is a gamepad read in DirectInput's XInput layout (W56).
  virtual bool joystick_gamepad() const { return false; }
  virtual int32_t joystick_object_name(uint32_t, char* name, uint32_t) { name[0] = 0; return kObjectNotFound; }
  virtual int32_t joystick_set_range(uint32_t, int32_t, int32_t) { return kUnsupported; }
  virtual int32_t joystick_get_range(uint32_t, int32_t* min, int32_t* max) { *min = kAxisMin, *max = kAxisMax; return 0; }
  virtual int32_t joystick_set_deadzone(uint32_t, uint32_t) { return 0; }
};

// Stands in for the Windows message queue zGameApp::Run (0x0058fd70) drives:
// PeekMessageA(PM_NOREMOVE), CWinThread::PumpMessage, MsgWaitForMultipleObjects,
// PostQuitMessage and SetThreadPriority (docs/game-loop.md).
class MessagePump {
 public:
  virtual ~MessagePump() = default;
  virtual bool peek() = 0;               // an event is waiting
  virtual bool pump() = 0;               // handle one event; false once quit was posted
  virtual void wait(uint32_t ms) = 0;    // sleep until an event arrives or ms pass
  virtual void post_quit() = 0;          // make a later pump() return false
  virtual void raise_priority() {}       // THREAD_PRIORITY_HIGHEST for the game thread
};

// Stands in for the registry keys the settings store reads and writes
// (HKCU and HKLM \SOFTWARE\MicroProse\MechWarrior 3\1.0, docs/settings.md), call
// for call: per_user picks HKCU (else HKLM). A value is a registry type
// (4 REG_DWORD, 3 REG_BINARY) and raw bytes.
class Settings {
 public:
  virtual ~Settings() = default;
  // RegOpenKeyExA (KEY_READ) or, for writing, RegCreateKeyExA (KEY_WRITE).
  virtual bool open(const std::string& key, bool per_user, bool write) = 0;
  // RegQueryValueExA without data: false when the value is missing.
  virtual bool query(bool per_user, const std::string& name, uint32_t* size) = 0;
  // RegQueryValueExA with data: false when missing or larger than *size.
  virtual bool read(bool per_user, const std::string& name, void* data, uint32_t* size) = 0;
  // RegSetValueExA.
  virtual bool write(bool per_user, const std::string& name, uint32_t type, const void* data,
                     uint32_t size) = 0;
  virtual void close(bool per_user) = 0;  // RegCloseKey
};

}  // namespace platform
