// The SDL front end's pieces that run without a window (docs/program.md):
// the DirectInput stand-in and the GDI text on SDL3_ttf.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cctype>
#include <cmath>
#include <chrono>
#include <csignal>
#include <thread>
#if !defined(_WIN32)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "mech3/data_folder.h"
#include "frontend/sdl/gdi_font_size.h"
#include "frontend/sdl/sdl_fit.h"
#include "frontend/sdl/launcher_notes.h"
#include "frontend/sdl/text_fit.h"
#include <SDL3_ttf/SDL_ttf.h>
#include "frontend/sdl/sdl_force.h"
#include "frontend/sdl/sdl_input.h"
#include "mech3/controls_cfg.h"
#include "frontend/sdl/sdl_install.h"
#include "frontend/sdl/sdl_platform.h"
#include "frontend/sdl/sdl_sound.h"
#include "frontend/sdl/sdl_twod_host.h"
#include "frontend/sdl/sdl_vfw.h"
#include "mech3/app_movie.h"
#include "mech3/movie_shape.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zrender/zrndr_blit.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/zvideo/zvid_format.h"
#include "gamez/ztwod/twod_host.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zsound/zsnd.h"
#include "mech3/app.h"
#include "mech3/app_engine.h"
#include "mech3/app_hud.h"
#include "mech3/app_mech.h"
#include "mech3/app_states.h"
#include "mech3/hud.h"
#include "mech3/mech_control.h"
#include "mech3/mission.h"
#include "mech3/options.h"
#include "mech3/pilot.h"
#include "platform/platform.h"
#include "platform/render3d.h"
#include "platform/silent_sound.h"
#include "mech3/hud.h"

#include <cstdio>
#include <map>

TEST(sdl_input_scancodes_map_to_dik_codes) {
  using sdlfe::dik_from_scancode;
  EXPECT(dik_from_scancode(SDL_SCANCODE_ESCAPE) == 0x01);
  EXPECT(dik_from_scancode(SDL_SCANCODE_1) == 0x02 && dik_from_scancode(SDL_SCANCODE_0) == 0x0b);
  EXPECT(dik_from_scancode(SDL_SCANCODE_Q) == 0x10 && dik_from_scancode(SDL_SCANCODE_P) == 0x19);
  EXPECT(dik_from_scancode(SDL_SCANCODE_A) == 0x1e && dik_from_scancode(SDL_SCANCODE_L) == 0x26);
  EXPECT(dik_from_scancode(SDL_SCANCODE_Z) == 0x2c && dik_from_scancode(SDL_SCANCODE_M) == 0x32);
  EXPECT(dik_from_scancode(SDL_SCANCODE_W) == 0x11 && dik_from_scancode(SDL_SCANCODE_S) == 0x1f);
  EXPECT(dik_from_scancode(SDL_SCANCODE_F1) == 0x3b && dik_from_scancode(SDL_SCANCODE_F10) == 0x44);
  EXPECT(dik_from_scancode(SDL_SCANCODE_F11) == 0x57 && dik_from_scancode(SDL_SCANCODE_F12) == 0x58);
  EXPECT(dik_from_scancode(SDL_SCANCODE_SPACE) == 0x39 && dik_from_scancode(SDL_SCANCODE_RETURN) == 0x1c);
  EXPECT(dik_from_scancode(SDL_SCANCODE_LCTRL) == 0x1d && dik_from_scancode(SDL_SCANCODE_RCTRL) == 0x9d);
  EXPECT(dik_from_scancode(SDL_SCANCODE_LALT) == 0x38 && dik_from_scancode(SDL_SCANCODE_RALT) == 0xb8);
  EXPECT(dik_from_scancode(SDL_SCANCODE_LSHIFT) == 0x2a && dik_from_scancode(SDL_SCANCODE_RSHIFT) == 0x36);
  EXPECT(dik_from_scancode(SDL_SCANCODE_UP) == 0xc8 && dik_from_scancode(SDL_SCANCODE_DOWN) == 0xd0);
  EXPECT(dik_from_scancode(SDL_SCANCODE_LEFT) == 0xcb && dik_from_scancode(SDL_SCANCODE_RIGHT) == 0xcd);
  EXPECT(dik_from_scancode(SDL_SCANCODE_KP_ENTER) == 0x9c && dik_from_scancode(SDL_SCANCODE_KP_5) == 0x4c);
  EXPECT(dik_from_scancode(SDL_SCANCODE_PAGEUP) == 0xc9 && dik_from_scancode(SDL_SCANCODE_DELETE) == 0xd3);
  // No two keys share a code (bar the ISO # key, where the US backslash is).
  int seen[256] = {};
  for (int s = 0; s < SDL_SCANCODE_COUNT; ++s)
    if (const uint8_t d = dik_from_scancode(static_cast<SDL_Scancode>(s))) ++seen[d];
  int dup = 0;
  for (int d = 0; d < 256; ++d) dup += seen[d] > 1 && d != 0x2b;
  EXPECT(dup == 0);
  EXPECT(dik_from_scancode(SDL_SCANCODE_F20) == 0);
}

namespace {
SDL_Event key(SDL_Scancode s, bool down, bool repeat = false) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.scancode = s, e.key.down = down, e.key.repeat = repeat;
  return e;
}
}  // namespace

// The joystick as zInput's setup sets it (0x0051d700: every axis
// -1000..1000, dead zones 10% on X and Y, none on Z, 25% on the rotations),
// on SDL's virtual devices (docs/input.md section 5): a flight stick read
// raw, its axes, dead zones, hat and buttons as DirectInput gave them; a
// gamepad in DirectInput's XInput layout (FIX (A)): the left stick X, Y, the
// triggers on one centred Z, the right stick RX, RY, no RZ, A..RS buttons
// 0..9, the d-pad the hat.
namespace {
void joy_setup_as_zinput(sdlfe::SdlInput& in) {
  for (uint32_t i = 0; i < 6; ++i) {
    char name[64];
    if (in.joystick_object_name(4 * i, name, sizeof name) != 0) continue;
    in.joystick_set_range(4 * i, -1000, 1000);
    in.joystick_set_deadzone(4 * i, i < 2 ? 1000 : i == 2 ? 0 : 2500);
  }
}
}  // namespace

TEST(sdl_input_virtual_stick_reads_as_directinput) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc d;
  SDL_INIT_INTERFACE(&d);
  d.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK, d.naxes = 6, d.nbuttons = 12, d.nhats = 1, d.name = "mw3 test stick";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
  EXPECT(id != 0);
  SDL_Joystick* v = SDL_OpenJoystick(id);  // the test's handle, to move it
  {
    sdlfe::SdlInput in;
    EXPECT(in.open(platform::Input::kJoystick));
    uint32_t flags = 1, axes = 0;
    in.joystick_caps(&flags, &axes);
    EXPECT(axes == 6);
    joy_setup_as_zinput(in);
    in.set_cooperative_level(platform::Input::kJoystick, 9);
    EXPECT(in.acquire(platform::Input::kJoystick) == 0);
    platform::JoyState st;
    // X a little off centre (inside 10%), Y full forward, Z (the throttle)
    // three quarters, RZ (the twist) inside 25%; the hat right; button 1.
    SDL_SetJoystickVirtualAxis(v, 0, 2000);
    SDL_SetJoystickVirtualAxis(v, 1, -32768);
    SDL_SetJoystickVirtualAxis(v, 2, 16384);
    SDL_SetJoystickVirtualAxis(v, 5, -6000);
    SDL_SetJoystickVirtualHat(v, 0, SDL_HAT_RIGHT);
    SDL_SetJoystickVirtualButton(v, 1, true);
    SDL_UpdateJoysticks();
    EXPECT(in.joystick_state(&st) == 0);
    std::printf("  stick: x %d y %d z %d rz %d pov %u buttons %x %x\n", st.x, st.y, st.z, st.rz, st.pov[0], st.buttons[0],
                st.buttons[1]);
    EXPECT(st.x == 0);                       // inside X's dead zone
    EXPECT(st.y == -1000);                   // full
    EXPECT(st.z >= 495 && st.z <= 505);      // no dead zone: 16384/32767 of the half range
    EXPECT(st.rz == 0);                      // inside 25%
    EXPECT(st.pov[0] == 9000 && st.pov[1] == 0xffffffffu);
    EXPECT(st.buttons[0] == 0 && st.buttons[1] == 0x80);
    // Past a dead zone the rest of the travel is scaled to the full range.
    SDL_SetJoystickVirtualAxis(v, 0, 32767);
    SDL_SetJoystickVirtualAxis(v, 5, 32767);
    SDL_UpdateJoysticks();
    in.joystick_state(&st);
    EXPECT(st.x == 1000 && st.rz == 1000);
    in.close(platform::Input::kJoystick);
  }
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);
}

// W54 (Reddit, v1.0: "I can't aim with my joystick (Thrustmaster T.16000m)"):
// SDL packs a raw stick's axes; the T.16000M's (X, Y, Rz twist, slider
// throttle) gave the twist as Z, the game's throttle, and no torso twist (Rz).
// Its descriptor as Windows reads it (ivyl/input-db,
// USB-0x044F_0xB10A-Thrustmaster-T.16000M.md).
namespace {
const uint8_t kT16000m[] = {
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45,
    0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02, 0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b,
    0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x81, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xff, 0x3f, 0x35,
    0x00, 0x46, 0xff, 0x3f, 0x65, 0x00, 0x75, 0x0e, 0x95, 0x01, 0x81, 0x02, 0x75, 0x02, 0x95, 0x01, 0x81, 0x01, 0x09,
    0x31, 0x75, 0x0e, 0x95, 0x01, 0x81, 0x02, 0x75, 0x02, 0x95, 0x01, 0x81, 0x01, 0x09, 0x35, 0x09, 0x36, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x35, 0x00, 0x46, 0xff, 0x00, 0x65, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0x75, 0x08, 0x95,
    0x37, 0x81, 0x01, 0x06, 0x00, 0xff, 0x27, 0xff, 0xff, 0x00, 0x00, 0x09, 0x01, 0x75, 0x08, 0x95, 0x04, 0xb1, 0xa2,
    0xc0};
SDL_JoystickID attach_stick(const char* name, int axes, int buttons, int hats, uint16_t vendor = 0, uint16_t product = 0) {
  SDL_VirtualJoystickDesc d;
  SDL_INIT_INTERFACE(&d);
  d.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK, d.naxes = static_cast<Uint16>(axes), d.nbuttons = static_cast<Uint16>(buttons);
  d.nhats = static_cast<Uint16>(hats), d.name = name, d.vendor_id = vendor, d.product_id = product;
  return SDL_AttachVirtualJoystick(&d);
}
void pump(sdlfe::SdlInput& in) {
  SDL_PumpEvents();
  SDL_Event e;
  while (SDL_PollEvent(&e)) in.on_event(e);
}
}  // namespace

TEST(sdl_joystick_axes_from_the_t16000m_descriptor) {
  const std::vector<uint16_t> u = sdlfe::hid_axis_usages(kT16000m, sizeof kT16000m);
  EXPECT((u == std::vector<uint16_t>{0x30, 0x31, 0x35, 0x36}));  // the hat (0x39) and the vendor page left out
  int s[8];
  EXPECT(sdlfe::axis_slots_from_usages(u, 4, s));
  EXPECT(s[0] == 0 && s[1] == 1 && s[2] == -1 && s[3] == -1 && s[4] == -1 && s[5] == 2 && s[6] == 3 && s[7] == -1);
  EXPECT(sdlfe::axis_slots_text(s, 4) == "X,Y,Rz,S0");
  EXPECT(!sdlfe::axis_slots_from_usages(u, 5, s));  // SDL counting otherwise: not trusted
  int t[8];
  EXPECT(sdlfe::axis_slots_from_text(" x, y ,RZ,s0", 4, t) && std::equal(s, s + 8, t));
  EXPECT(sdlfe::axis_slots_from_text("X,-,Z", 3, t) && t[0] == 0 && t[1] == -1 && t[2] == 2);
  EXPECT(!sdlfe::axis_slots_from_text("X,X", 2, t) && !sdlfe::axis_slots_from_text("X,Q", 2, t));
  // A Dial and a Wheel after a Slider: the second slider, then none.
  EXPECT(sdlfe::axis_slots_from_usages({0x30, 0x31, 0x36, 0x37, 0x38}, 5, s) && s[6] == 2 && s[7] == 3);
  EXPECT(sdlfe::joystick_rank(true, 6, 15) > sdlfe::joystick_rank(false, 4, 12) &&
         sdlfe::joystick_rank(false, 4, 12) > sdlfe::joystick_rank(false, 1, 2));
}

TEST(sdl_input_t16000m_twist_reaches_rz) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  const SDL_JoystickID id = attach_stick("T.16000M", 4, 16, 1, 0x044f, 0xb10a);
  SDL_Joystick* v = SDL_OpenJoystick(id);
  // From its descriptor, from the table (no descriptor read), from mw3.cfg.
  for (int how = 0; how < 3; ++how) {
    sdlfe::SdlInput in;
    in.hid_usages = [how](SDL_Joystick*) {
      return how == 0 ? sdlfe::hid_axis_usages(kT16000m, sizeof kT16000m) : std::vector<uint16_t>{};
    };
    if (how == 2) in.joystick_axes = "X,Y,Z,S1";  // a player's own map wins
    EXPECT(in.open(platform::Input::kJoystick));
    std::printf("  T.16000M: axes from %s\n", in.axis_source());
    static const char* const kFrom[3] = {"its HID descriptor", "the known sticks' table", "mw3.cfg's joystick_axes="};
    EXPECT(std::string(in.axis_source()) == kFrom[how]);  // the log's word for it, each in its turn
    uint32_t flags = 0, axes = 0;
    in.joystick_caps(&flags, &axes);
    EXPECT(axes == 4);
    joy_setup_as_zinput(in);
    in.joystick_set_range(0x18, -1000, 1000), in.joystick_set_range(0x1c, -1000, 1000);
    EXPECT(in.acquire(platform::Input::kJoystick) == 0);
    SDL_SetJoystickVirtualAxis(v, 2, 32767);   // the twist full right
    SDL_SetJoystickVirtualAxis(v, 3, -32768);  // the throttle lever at one end
    SDL_UpdateJoysticks();
    platform::JoyState st;
    EXPECT(in.joystick_state(&st) == 0);
    std::printf("  T.16000M: z %d rz %d slider %d %d\n", st.z, st.rz, st.slider[0], st.slider[1]);
    if (how < 2) {
      EXPECT(st.rz == 1000 && st.z == 0);  // the twist on Rz, the game's torso twist; nothing on Z
      EXPECT(st.slider[0] == -1000 && st.slider[1] == 0);
      char name[32];
      EXPECT(in.joystick_object_name(0x08, name, sizeof name) != 0);  // no Z
      EXPECT(in.joystick_object_name(0x14, name, sizeof name) == 0);  // Rz
    } else {
      EXPECT(st.z == 1000 && st.rz == 0 && st.slider[1] == -1000);
    }
    in.close(platform::Input::kJoystick);
  }
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);
}

// A T.16000M's lever through its map and the popup's binding (Invert as
// shown): the low end, its top, full ahead, the high end full reverse; a
// slider stays in DirectInput's default 0..65535 (zInput sets none on them).
TEST(sdl_input_t16000m_lever_up_is_ahead) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  const SDL_JoystickID id = attach_stick("T.16000M", 4, 16, 1, 0x044f, 0xb10a);
  SDL_Joystick* v = SDL_OpenJoystick(id);
  sdlfe::SdlInput in;
  in.hid_usages = [](SDL_Joystick*) { return sdlfe::hid_axis_usages(kT16000m, sizeof kT16000m); };
  EXPECT(in.open(platform::Input::kJoystick));
  joy_setup_as_zinput(in);
  EXPECT(in.acquire(platform::Input::kJoystick) == 0);
  const mech3::AxisBinding lever{7, -0.5f / 32767.5f, 1.0f, 0.0f};
  float at[2] = {};
  for (int k = 0; k < 2; ++k) {
    SDL_SetJoystickVirtualAxis(v, 3, k == 0 ? -32768 : 32767);
    SDL_UpdateJoysticks();
    platform::JoyState st;
    EXPECT(in.joystick_state(&st) == 0);
    EXPECT(st.slider[0] == (k == 0 ? 0 : 65535));
    at[k] = mech3::lever_throttle(lever, &st);
  }
  std::printf("  T.16000M lever: low end %.2f, high end %.2f\n", at[0], at[1]);
  EXPECT(at[0] == 1.0f && at[1] == -1.0f);
  in.close(platform::Input::kJoystick);
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);
}

TEST(sdl_input_picks_the_stick_over_an_odd_device) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  const SDL_JoystickID odd = attach_stick("mw3 test RGB keyboard", 1, 2, 0);  // first, as EnumDevices took
  const SDL_JoystickID stick = attach_stick("mw3 test flight stick", 4, 12, 1);
  for (const char* want : {"", "rgb KEY"}) {
    sdlfe::SdlInput in;
    in.hid_usages = [](SDL_Joystick*) { return std::vector<uint16_t>{}; };
    in.joystick_want = want;
    EXPECT(in.open(platform::Input::kJoystick));
    const char* name = SDL_GetJoystickName(in.joystick());
    std::printf("  joystick=%s: %s\n", want, name ? name : "?");
    EXPECT(name && std::string(name) == (*want ? "mw3 test RGB keyboard" : "mw3 test flight stick"));
    in.close(platform::Input::kJoystick);
  }
  SDL_DetachVirtualJoystick(odd);
  SDL_DetachVirtualJoystick(stick);
}

TEST(sdl_input_stick_taken_out_reads_centred) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  SDL_JoystickID id = attach_stick("mw3 test unplugged stick", 8, 12, 1);
  SDL_Joystick* v = SDL_OpenJoystick(id);
  sdlfe::SdlInput in;
  in.hid_usages = [](SDL_Joystick*) { return std::vector<uint16_t>{}; };
  EXPECT(in.open(platform::Input::kJoystick));
  joy_setup_as_zinput(in);
  EXPECT(in.acquire(platform::Input::kJoystick) == 0);
  pump(in);
  SDL_SetJoystickVirtualAxis(v, 0, 32767);   // turning hard
  SDL_SetJoystickVirtualAxis(v, 2, -16384);  // the throttle
  SDL_SetJoystickVirtualAxis(v, 6, 16384);   // a throttle bound to the slider
  SDL_SetJoystickVirtualButton(v, 0, true);  // firing
  SDL_UpdateJoysticks();
  platform::JoyState st;
  EXPECT(in.joystick_state(&st) == 0 && st.x == 1000 && st.buttons[0] == 0x80);
  const int32_t z = st.z, slider = st.slider[0];
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);  // taken out mid-mission
  pump(in);
  EXPECT(in.unplugged() && !in.joystick());
  EXPECT(in.joystick_state(&st) == 0);
  std::printf("  taken out: x %d z %d (was %d) button %x\n", st.x, st.z, z, st.buttons[0]);
  EXPECT(st.slider[0] == slider);  // a slider-bound throttle or the lever: no new reading, so no new speed
  EXPECT(st.x == 0 && st.z == z && st.buttons[0] == 0);  // no held turn or fire; the throttle where it was
  id = attach_stick("mw3 test unplugged stick", 8, 12, 1);  // back
  pump(in);
  EXPECT(!in.unplugged() && in.joystick());
  v = SDL_OpenJoystick(id);
  SDL_SetJoystickVirtualAxis(v, 1, 32767);
  SDL_UpdateJoysticks();
  EXPECT(in.joystick_state(&st) == 0 && st.y == 1000);
  in.close(platform::Input::kJoystick);
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);
}

TEST(sdl_input_virtual_gamepad_reads_as_directinput_xinput) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc d;
  SDL_INIT_INTERFACE(&d);
  d.type = SDL_JOYSTICK_TYPE_GAMEPAD, d.naxes = SDL_GAMEPAD_AXIS_COUNT, d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  d.name = "mw3 test pad";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
  EXPECT(id != 0 && SDL_IsGamepad(id));
  SDL_Joystick* v = SDL_OpenJoystick(id);
  {
    sdlfe::SdlInput in;
    EXPECT(in.open(platform::Input::kJoystick));
    uint32_t flags = 1, axes = 0;
    in.joystick_caps(&flags, &axes);
    char name[64];
    const bool has_rz = in.joystick_object_name(0x14, name, sizeof name) == 0;
    std::printf("  pad: %u axes, RZ %s\n", axes, has_rz ? "present" : "absent");
    EXPECT(axes == 5 && !has_rz);
    joy_setup_as_zinput(in);
    in.set_cooperative_level(platform::Input::kJoystick, 9);
    EXPECT(in.acquire(platform::Input::kJoystick) == 0);
    platform::JoyState st;
    // At rest (the triggers released): every axis centred.
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    SDL_UpdateJoysticks();
    in.joystick_state(&st);
    std::printf("  pad at rest: x %d y %d z %d rx %d ry %d rz %d\n", st.x, st.y, st.z, st.rx, st.ry, st.rz);
    EXPECT(st.x == 0 && st.y == 0 && st.z == 0 && st.rx == 0 && st.ry == 0 && st.rz == 0);
    // The right trigger down: Z to its low end (the default throttle, Z x
    // -0.5 + 0.5, full); the left stick right; the right stick up.
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_RIGHTY, -32768);
    SDL_SetJoystickVirtualButton(v, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(v, SDL_GAMEPAD_BUTTON_START, true);
    SDL_SetJoystickVirtualButton(v, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
    SDL_SetJoystickVirtualButton(v, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
    SDL_UpdateJoysticks();
    in.joystick_state(&st);
    std::printf("  pad: x %d z %d ry %d pov %u buttons A %x Start %x\n", st.x, st.z, st.ry, st.pov[0], st.buttons[0],
                st.buttons[7]);
    EXPECT(st.x == 1000 && st.z == -1000 && st.ry == -1000);
    EXPECT(st.buttons[0] == 0x80 && st.buttons[7] == 0x80 && st.buttons[1] == 0);
    EXPECT(st.pov[0] == 22500);  // down and left
    in.close(platform::Input::kJoystick);
  }
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);
}

// The same pad through zInput's own joystick (0x0051d520's setup, 0x0051dc20's
// read) and the controls' axis table (0x0042fe10 on its default throttle,
// Z x -0.5 + 0.5): at rest half (it counts only once moved), the right
// trigger full, the left trigger stopped; the turn (X x -1) and pitch (Y).
TEST(sdl_input_virtual_gamepad_through_zinput_drives_the_axes) {
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc d;
  SDL_INIT_INTERFACE(&d);
  d.type = SDL_JOYSTICK_TYPE_GAMEPAD, d.naxes = SDL_GAMEPAD_AXIS_COUNT, d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  d.name = "mw3 test pad";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
  SDL_Joystick* v = SDL_OpenJoystick(id);
  sdlfe::SdlInput in;
  platform::Input* was = zin::state().di;
  zin::state().di = &in;
  EXPECT(zin::joy_init(0) == 1 && zin::joy_present());
  const mech3::AxisTable t;  // the defaults (0x00410950)
  float values[3][4] = {};
  const int16_t lt[3] = {-32768, -32768, 32767}, rt[3] = {-32768, 32767, -32768};
  for (int k = 0; k < 3; ++k) {
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, lt[k]);
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, rt[k]);
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_LEFTX, k == 1 ? 32767 : 0);
    SDL_SetJoystickVirtualAxis(v, SDL_GAMEPAD_AXIS_LEFTY, k == 2 ? -32768 : 0);
    SDL_UpdateJoysticks();
    const platform::JoyState* j = zin::joy_read(false);
    EXPECT(j != nullptr);
    for (int a = 0; a < 4; ++a) values[k][a] = t.axes[a].value(j, nullptr);
  }
  std::printf("  throttle %.2f %.2f %.2f; twist %.2f; pitch %.2f; turn %.2f\n", values[0][0], values[1][0],
              values[2][0], values[0][1], values[2][2], values[1][3]);
  EXPECT(std::fabs(values[0][0] - 0.5f) < 0.01f);  // at rest: half
  EXPECT(std::fabs(values[1][0] - 1.0f) < 0.01f);  // the right trigger: full
  EXPECT(std::fabs(values[2][0] - 0.0f) < 0.01f);  // the left trigger: stopped
  EXPECT(values[0][1] == 0.0f);                    // no RZ: the twist still
  EXPECT(values[2][2] < -0.99f);                   // the stick forward: pitch -1
  EXPECT(values[1][3] < -0.99f);                   // the stick right: the turn x -1
  zin::joy_close();
  zin::state().di = was;
  SDL_CloseJoystick(v);
  SDL_DetachVirtualJoystick(id);
}

// Force feedback as rumble (FIX (A), sdl_force.h): the shipped force_eff.ifr
// read as IFORCE2 reads it (18 records), the exe's names found with '_' for
// ' ' ("i_fall" names nothing), a compound as its parts; a part on the slow
// or the fast motor by its kind and period, at Magnitude x gain, for its
// Duration; on SDL's virtual stick, whose Rumble the device drives.
namespace {
struct Rumbled {
  int calls = 0;
  uint16_t low = 0, high = 0;
} g_rumbled;
uint64_t g_ms = 1000;
}  // namespace
TEST(sdl_force_device_plays_the_ifr_effects_as_rumble) {
  if (!std::filesystem::exists(std::string(MW3_GAME_DIR) + "/force_eff.ifr")) return;
  EXPECT(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc d;
  SDL_INIT_INTERFACE(&d);
  d.type = SDL_JOYSTICK_TYPE_GAMEPAD, d.naxes = SDL_GAMEPAD_AXIS_COUNT, d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  d.name = "mw3 rumble pad";
  d.Rumble = [](void*, Uint16 low, Uint16 high) {
    ++g_rumbled.calls, g_rumbled.low = low, g_rumbled.high = high;
    return true;
  };
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
  SDL_Joystick* j = SDL_OpenJoystick(id);
  {
    sdlfe::SdlInput in;
    EXPECT(in.open(platform::Input::kJoystick));
    uint32_t flags = 0, axes = 0;
    in.joystick_caps(&flags, &axes);
    EXPECT(flags & 0x100);  // DIDC_FORCEFEEDBACK: it rumbles
    const zutl::Mounts kept = zutl::mounts();
    zutl::mounts() = zutl::Mounts{};
    zutl::mount(MW3_GAME_DIR);
    sdlfe::SdlForceDevice dev([&in] { return in.joystick(); });
    dev.now_ms = [] { return g_ms; };
    EXPECT(dev.load("force_eff.ifr"));
    std::vector<uint8_t> raw;
    {
      std::FILE* f = std::fopen((std::string(MW3_GAME_DIR) + "/force_eff.ifr").c_str(), "rb");
      uint8_t b[4096];
      for (size_t n; f && (n = std::fread(b, 1, sizeof b, f)) > 0;) raw.insert(raw.end(), b, b + n);
      if (f) std::fclose(f);
    }
    std::vector<sdlfe::SdlForceDevice::Record> recs;
    EXPECT(sdlfe::SdlForceDevice::parse(raw, &recs) && recs.size() == 18);
    const auto recoil = dev.create("Recoil"), spin = dev.create("Spin_Move"), jets = dev.create("jets1");
    const auto buzz = dev.create("laser_buzz"), fall = dev.create("i_fall"), spring = dev.create("Spring");
    std::printf("  parts: Recoil %zu, Spin_Move %zu, jets1 %zu, laser_buzz %zu, i_fall %zu, Spring %zu\n", recoil.size(),
                spin.size(), jets.size(), buzz.size(), fall.size(), spring.size());
    EXPECT(recoil.size() == 1 && spin.size() == 2 && jets.size() == 1 && buzz.size() == 1 && fall.empty() &&
           spring.size() == 1);
    // The fall's effect as the file names it (FIX (bug), force_feedback.cpp). 1.0's file has
    // one of that name too: its landing bump.
    EXPECT(dev.create("impact_fall").size() == 1);
    // Recoil (a vector force, 9874) at gain 60: the slow motor, 200 ms.
    g_rumbled = {};
    dev.set_gain(recoil[0], 6000);
    dev.start(recoil[0]);
    sdlfe::SdlForceDevice::Rumble r = dev.last();
    std::printf("  Recoil: low %u high %u for %u ms (the stick asked %d time(s): %u %u)\n", r.low, r.high, r.ms,
                g_rumbled.calls, g_rumbled.low, g_rumbled.high);
    EXPECT(r.low == 9874u * 6000 / 10000 * 0xffff / 10000 && r.high == 0 && r.ms == 200);
    EXPECT(g_rumbled.calls >= 1 && g_rumbled.low == r.low);
    // laser_buzz (periodic, 50 ms period, 5000) at full gain: the fast motor
    // with it; then 300 ms on, both over: nothing.
    dev.set_gain(buzz[0], 10000);
    dev.start(buzz[0]);
    r = dev.last();
    EXPECT(r.low > 0 && r.high == 5000u * 0xffff / 10000 && r.ms == 200);
    g_ms += 300;
    dev.stop(spin[0]);  // any change looks again
    r = dev.last();
    EXPECT(r.low == 0 && r.high == 0 && r.ms == 0);
    // jets1 (infinite): on until stopped; the Spring (a condition) never rumbles.
    dev.start(spring[0]);
    EXPECT(dev.last().low == 0 && dev.last().high == 0);
    dev.start(jets[0]);
    EXPECT(dev.last().high > 0 && dev.last().ms == 0xffffffffu);
    g_ms += 60000;
    dev.stop(spring[0]);
    EXPECT(dev.last().high > 0);
    dev.stop(jets[0]);
    EXPECT(dev.last().high == 0 && dev.last().low == 0);
    zutl::mounts() = kept;
    in.close(platform::Input::kJoystick);
  }
  SDL_CloseJoystick(j);
  SDL_DetachVirtualJoystick(id);
}

TEST(sdl_input_keyboard_buffers_like_getdevicedata) {
  sdlfe::SdlInput in;
  platform::KeyEvent ev[200];
  uint32_t n = 200;
  EXPECT(in.keyboard_data(ev, &n) == platform::Input::kNotAcquired && n == 0);
  EXPECT(in.open(platform::Input::kKeyboard) && in.acquire(platform::Input::kKeyboard) == 0);
  in.on_event(key(SDL_SCANCODE_W, true));
  in.on_event(key(SDL_SCANCODE_W, true, true));  // auto-repeat: not a new press
  in.on_event(key(SDL_SCANCODE_W, false));
  in.on_event(key(SDL_SCANCODE_F20, true));        // no DIK code: dropped
  n = 1;
  EXPECT(in.keyboard_data(ev, &n) == 0 && n == 1 && ev[0].ofs == 0x11 && ev[0].data == 0x80);
  n = 200;
  EXPECT(in.keyboard_data(ev, &n) == 0 && n == 1 && ev[0].ofs == 0x11 && ev[0].data == 0);
  // 0x80 events fit; the rest overflow, as with DIPROP_BUFFERSIZE 0x80.
  for (int i = 0; i < 70; ++i) in.on_event(key(SDL_SCANCODE_A, true)), in.on_event(key(SDL_SCANCODE_A, false));
  n = 200;
  EXPECT(in.keyboard_data(ev, &n) == platform::Input::kBufferOverflow && n == 0x80);
  EXPECT(ev[0].sequence < ev[1].sequence);
  in.unacquire(platform::Input::kKeyboard);
  in.on_event(key(SDL_SCANCODE_A, true));
  EXPECT(in.key_down(0x1e));  // the held state follows even when not acquired
  in.acquire(platform::Input::kKeyboard);
  n = 200;
  EXPECT(in.keyboard_data(ev, &n) == 0 && n == 0);
}

TEST(sdl_input_mouse_counts_relative_motion) {
  sdlfe::SdlInput in;
  EXPECT(in.open(platform::Input::kMouse) && in.acquire(platform::Input::kMouse) == 0);
  SDL_Event m{};
  m.type = SDL_EVENT_MOUSE_MOTION;
  m.motion.xrel = 2.5f, m.motion.yrel = -1.0f, m.motion.x = 100, m.motion.y = 50;
  in.on_event(m);
  in.on_event(m);
  SDL_Event b{};
  b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  b.button.button = SDL_BUTTON_RIGHT;
  in.on_event(b);
  platform::MouseState s;
  EXPECT(in.mouse_state(&s) == 0 && s.x == 5 && s.y == -2 && s.buttons[1] == 0x80 && s.buttons[0] == 0);
  EXPECT(in.mouse_state(&s) == 0 && s.x == 0 && s.y == 0);  // counts since the last read
  EXPECT(in.mouse_buttons == 2);
  in.unacquire(platform::Input::kMouse);
  EXPECT(in.mouse_state(&s) == platform::Input::kNotAcquired);
}

// Ours, for playing fullscreen (FIX (A)): Alt+Enter toggles and the game never
// sees that Enter; a captured mouse counts the device's own motion, as
// DirectInput's mickeys did at any screen size; leaving the window lets go of
// what was held, and the game's sounds play on silent.
TEST(sdl_fullscreen_keys_focus_and_mickeys) {
  sdlfe::SdlInput in;
  in.set_cooperative_level(platform::Input::kKeyboard, 10);
  EXPECT(in.open(platform::Input::kKeyboard) && in.acquire(platform::Input::kKeyboard) == 0);
  sdlfe::SdlPump pump(&in, nullptr);
  int toggles = 0, focus_lost = 0;
  pump.toggle_fullscreen = [&] { ++toggles; };
  pump.focus_changed = [&](bool on) { focus_lost += !on; };
  auto key = [&](bool down, SDL_Keymod mod, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = SDL_SCANCODE_RETURN, e.key.mod = mod, e.key.repeat = repeat;
    pump.handle(e);
  };
  key(true, SDL_KMOD_LALT);
  key(true, SDL_KMOD_LALT, true);  // held: no second toggle
  key(false, SDL_KMOD_NONE);       // Alt let go first: the Enter's release goes too
  platform::KeyEvent k[8];
  uint32_t n = 8;
  in.keyboard_data(k, &n);
  EXPECT(toggles == 1 && n == 0);
  key(true, SDL_KMOD_NONE);  // a plain Enter is the game's
  key(false, SDL_KMOD_NONE);
  n = 8;
  in.keyboard_data(k, &n);
  EXPECT(toggles == 1 && n == 2 && k[0].ofs == 0x1c && k[0].data == 0x80 && k[1].data == 0);

  // The mouse as the game holds it (DISCL_EXCLUSIVE | DISCL_FOREGROUND): a
  // count is a mickey whatever the window's size.
  sdlfe::FrameShape shape{640, 480, true};
  in.shape = &shape;
  in.mickey_scale = 0.5f;
  in.set_cooperative_level(platform::Input::kMouse, 5);
  EXPECT(in.open(platform::Input::kMouse) && in.acquire(platform::Input::kMouse) == 0 && in.captured());
  SDL_Event m{};
  m.type = SDL_EVENT_MOUSE_MOTION;
  m.motion.xrel = 96, m.motion.yrel = -48;
  pump.handle(m);
  SDL_Event b{};
  b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  b.button.button = SDL_BUTTON_LEFT;
  pump.handle(b);
  platform::MouseState ms;
  EXPECT(in.mouse_state(&ms) == 0 && ms.x == 96 && ms.y == -48 && ms.buttons[0] == 0x80);
  pump.handle(b);  // held down when the player switches away; let go outside
  SDL_Event f{};
  f.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  pump.handle(f);
  EXPECT(focus_lost == 1 && !in.captured() && in.mouse_state(&ms) == platform::Input::kInputLost);
  f.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  pump.handle(f);
  EXPECT(in.acquire(platform::Input::kMouse) == 0);
  EXPECT(in.mouse_state(&ms) == 0 && ms.buttons[0] == 0 && in.mouse_buttons == 0);  // not stuck down

  // A buffer the game made (no DSBCAPS_GLOBALFOCUS) is silent without the
  // focus and plays on meanwhile; one with it is heard.
  sdlfe::SdlSoundOutput out;
  platform::WaveFormat wf{1, 1, 44100, 88200, 2, 16};
  std::vector<int16_t> pcm(64, 16384);
  platform::SoundBuffer quiet = 0, global = 0;
  EXPECT(out.create_buffer({0x80, 128, &wf}, &quiet) == 0 && out.create_buffer({0x8080, 128, &wf}, &global) == 0);
  for (platform::SoundBuffer sb : {quiet, global}) {
    void *p1 = nullptr, *p2 = nullptr;
    uint32_t n1 = 0, n2 = 0;
    out.lock(sb, 0, 128, &p1, &n1, &p2, &n2);
    std::memcpy(p1, pcm.data(), n1);
    out.unlock(sb, p1, n1, p2, n2);
  }
  float mix[16] = {};
  out.play(quiet, 0);
  out.mix(mix, 8);
  EXPECT(mix[0] > 0.4f);
  out.set_focused(false);
  std::fill(std::begin(mix), std::end(mix), 0.0f);
  out.mix(mix, 8);
  uint32_t at = 0;
  out.get_current_position(quiet, &at, nullptr);
  EXPECT(mix[0] == 0.0f && mix[15] == 0.0f && at == 32);  // 16 frames played, 8 of them silent
  out.play(global, 0);
  out.mix(mix, 8);
  EXPECT(mix[0] > 0.4f);
}

// FIX (A), the mouse in a window (sdl_input.h, docs/input.md): the menus
// never capture and put zInput's mouse at the cursor; a mission captures;
// Ctrl+Alt alone lets go (the game then sees no motion or buttons), a click
// inside takes it back unseen; the pause lets go and its end takes it back;
// the focus lost lets go until a click. Fullscreen holds it as before.
// Playtest W19: "the mouse cursor sits in the centre of the screen" in a
// fullscreen mission, the aim working. SdlInput asked SDL for the relative
// mouse only when its own flag changed; one SDL refused or let go was never
// asked for again, so the system's cursor showed where the game puts it each
// frame (SetCursorPos, the centre) while the motion still aimed. Each read
// now asks SDL for its state: held means relative and no cursor, in the
// menus too (the game draws its own); let go in a window, the cursor shows.
TEST(sdl_held_mouse_keeps_the_cursor_hidden) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  sdlfe::SdlWindow w;
  EXPECT(w.init(true));
  w.create("mw3 cursor test", 640, 480);
  sdlfe::SdlInput in;
  sdlfe::SdlPump pump(&in, nullptr);
  in.window = w.sdl();
  bool windowed = false, mission = true;
  in.windowed = [&] { return windowed; };
  in.in_mission = [&] { return mission; };
  in.set_cooperative_level(platform::Input::kMouse, 5);
  EXPECT(in.open(platform::Input::kMouse) && in.acquire(platform::Input::kMouse) == 0);
  platform::MouseState ms;
  in.mouse_state(&ms);
  if (!SDL_GetWindowRelativeMouseMode(w.sdl())) {
    std::printf("no relative mouse for a hidden window here: not checked\n");
    return;
  }
  EXPECT(in.captured() && !SDL_CursorVisible());
  // Fullscreen, a mission: SDL lets go behind SdlInput's back.
  SDL_SetWindowRelativeMouseMode(w.sdl(), false);
  SDL_ShowCursor();
  in.mouse_state(&ms);
  std::printf("after SDL let go: relative %d, cursor %d\n", SDL_GetWindowRelativeMouseMode(w.sdl()), SDL_CursorVisible());
  EXPECT(SDL_GetWindowRelativeMouseMode(w.sdl()) && !SDL_CursorVisible());
  // Fullscreen menus: still held, no system cursor.
  mission = false;
  in.mouse_state(&ms);
  EXPECT(in.captured() && !SDL_CursorVisible());
  // A window, a mission, the focus lost: let go, the system's cursor shown.
  windowed = true, mission = true;
  SDL_Event f{};
  f.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  pump.handle(f);
  EXPECT(!in.captured() && !SDL_GetWindowRelativeMouseMode(w.sdl()) && SDL_CursorVisible());
  SDL_ShowCursor();
}

// Playtest W30: "as soon as it started, a bunch of lasers fired ... I
// hadn't fired anything" (c2m3, from the briefing's LAUNCH). A click from
// the screen before the mission, still latched or still held, read as the
// mission's first fire. Into the mission, a latched press is dropped and a
// held button is the game's only after it is let go; the next click fires.
TEST(sdl_mission_start_drops_the_launch_click) {
  sdlfe::SdlInput in;
  sdlfe::SdlPump pump(&in, nullptr);
  bool mission = false;
  in.windowed = [] { return false; };
  in.in_mission = [&] { return mission; };
  in.set_cooperative_level(platform::Input::kMouse, 5);
  EXPECT(in.open(platform::Input::kMouse) && in.acquire(platform::Input::kMouse) == 0);
  platform::MouseState ms;
  auto button = [&](bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    pump.handle(e);
  };
  // A click on LAUNCH within one frame: latched, unread.
  button(true), button(false);
  mission = true;
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  // LAUNCH pressed and still held as the mission starts.
  mission = false;
  in.mouse_state(&ms);
  button(true);
  mission = true;
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button(false);
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button(true);  // the next click is the game's
  EXPECT(in.mouse_state(&ms) == 0 && ms.buttons[0]);
  button(false);
}

TEST(sdl_window_lets_the_mouse_go) {
  sdlfe::SdlInput in;
  sdlfe::SdlPump pump(&in, nullptr);
  bool windowed = true, mission = false;
  float cx = -1, cy = -1;
  in.windowed = [&] { return windowed; };
  in.in_mission = [&] { return mission; };
  in.absolute_cursor = [&](float x, float y) { cx = x, cy = y; };
  in.set_cooperative_level(platform::Input::kMouse, 5);  // DISCL_EXCLUSIVE | DISCL_FOREGROUND, as zInput
  in.set_cooperative_level(platform::Input::kKeyboard, 10);
  EXPECT(in.open(platform::Input::kKeyboard) && in.acquire(platform::Input::kKeyboard) == 0);
  EXPECT(in.open(platform::Input::kMouse) && in.acquire(platform::Input::kMouse) == 0);
  platform::MouseState ms;
  auto move = [&](float x, float y, float dx, float dy) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x, e.motion.y = y, e.motion.xrel = dx, e.motion.yrel = dy;
    pump.handle(e);
  };
  auto button = [&](bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT, e.button.x = in.mouse_x, e.button.y = in.mouse_y;  // where the cursor is
    pump.handle(e);
  };
  auto key = [&](SDL_Scancode sc, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    pump.handle(e);
  };
  auto focus = [&](bool on) {
    SDL_Event e{};
    e.type = on ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    pump.handle(e);
  };
  // 1) The menus: free, absolute; the clicks are the game's.
  EXPECT(in.mouse_state(&ms) == 0 && !in.captured() && cx < 0);  // not moved yet: zInput's place stays
  move(410, 250, 30, 20);
  button(true);
  EXPECT(in.mouse_state(&ms) == 0 && !in.captured() && cx == 410 && cy == 250 && ms.x == 0 && ms.buttons[0]);
  button(false);
  // 2) A mission: held; Ctrl+Alt alone lets go.
  mission = true;
  EXPECT(in.mouse_state(&ms) == 0 && in.captured() && !in.released());
  move(0, 0, 12, -5);
  EXPECT(in.mouse_state(&ms) == 0 && ms.x == 12 && ms.y == -5);
  key(SDL_SCANCODE_LCTRL, true), key(SDL_SCANCODE_LALT, true), key(SDL_SCANCODE_W, true);  // another key between
  key(SDL_SCANCODE_W, false), key(SDL_SCANCODE_LALT, false), key(SDL_SCANCODE_LCTRL, false);
  EXPECT(in.captured() && !in.released());
  key(SDL_SCANCODE_LCTRL, true), key(SDL_SCANCODE_RALT, true), key(SDL_SCANCODE_RALT, false);
  EXPECT(!in.captured() && in.released());
  key(SDL_SCANCODE_LCTRL, false);
  move(500, 300, 40, 40);
  SDL_Event wheel{};
  wheel.type = SDL_EVENT_MOUSE_WHEEL, wheel.wheel.y = 1;
  pump.handle(wheel);
  EXPECT(in.mouse_state(&ms) == 0 && ms.x == 0 && ms.y == 0 && ms.z == 0 && !ms.buttons[0]);
  // A click inside takes it back; the game sees neither its press nor its release.
  button(true);
  EXPECT(in.captured() && !in.released() && in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button(false);
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button(true);  // the next click is the game's (fire)
  EXPECT(in.mouse_state(&ms) == 0 && ms.buttons[0]);
  button(false);
  // 3) The pause lets go (a menu: absolute); its end takes it back.
  mission = false;
  EXPECT(in.mouse_state(&ms) == 0 && !in.captured());
  move(320, 240, 1, 1);
  EXPECT(in.mouse_state(&ms) == 0 && cx == 320 && cy == 240);
  mission = true;
  EXPECT(in.mouse_state(&ms) == 0 && in.captured());
  // 4) The focus lost lets go; back (ours), a held mission takes it again at
  // once, no click needed. The click that brought it back is not the game's.
  auto focus_at = [&](bool on, uint64_t ms_at) {
    SDL_Event e{};
    e.type = on ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    e.common.timestamp = ms_at * 1000000ull;
    pump.handle(e);
  };
  auto button_at = [&](bool down, uint64_t ms_at) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT, e.button.timestamp = ms_at * 1000000ull;
    pump.handle(e);
  };
  focus_at(false, 1000);
  EXPECT(!in.captured() && in.mouse_state(&ms) == platform::Input::kInputLost);
  focus_at(true, 5000);
  EXPECT(in.acquire(platform::Input::kMouse) == 0 && in.captured() && !in.released());
  move(10, 10, 9, 9);
  EXPECT(in.mouse_state(&ms) == 0 && ms.x == 9);  // aims at once
  button_at(true, 5010);  // the activating click (the system's click-through)
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button_at(false, 5100);
  EXPECT(in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button_at(true, 6000);  // the next is the game's
  EXPECT(in.mouse_state(&ms) == 0 && ms.buttons[0]);
  button_at(false, 6100);
  // Back by Cmd+Tab, no click: the first click after is the game's.
  focus_at(false, 7000), focus_at(true, 8000);
  EXPECT(in.acquire(platform::Input::kMouse) == 0 && in.captured());
  button_at(true, 9000);
  EXPECT(in.mouse_state(&ms) == 0 && ms.buttons[0]);
  button_at(false, 9100);
  in.mouse_state(&ms);
  // Let go by Ctrl+Alt before the focus left: back, it stays let go until a click.
  key(SDL_SCANCODE_LCTRL, true), key(SDL_SCANCODE_LALT, true), key(SDL_SCANCODE_LALT, false), key(SDL_SCANCODE_LCTRL, false);
  EXPECT(!in.captured() && in.released());
  focus_at(false, 10000), focus_at(true, 11000);
  EXPECT(in.acquire(platform::Input::kMouse) == 0 && !in.captured() && in.released());
  button_at(true, 12000);
  EXPECT(in.captured() && in.mouse_state(&ms) == 0 && !ms.buttons[0]);
  button_at(false, 12100);
  // Fullscreen: held in the menus too, Ctrl+Alt does nothing, back from
  // away it is held again at once (as before).
  windowed = false, mission = false;
  EXPECT(in.mouse_state(&ms) == 0 && in.captured());
  mission = true;
  key(SDL_SCANCODE_LCTRL, true), key(SDL_SCANCODE_LALT, true), key(SDL_SCANCODE_LALT, false), key(SDL_SCANCODE_LCTRL, false);
  EXPECT(in.captured() && !in.released());
  focus(false), focus(true);
  EXPECT(in.acquire(platform::Input::kMouse) == 0 && in.captured());
}

// High DPI: the window's coordinates (the mouse's) and its pixels (the frame's
// draw) differ by the density (2 on a Retina Mac; 1 on Windows, where SDL3 is
// per-monitor DPI aware and 150% is only the display's scale). The frame's
// rect is the same fraction of either, so a click lands on the pixel drawn.
TEST(sdl_frame_maps_alike_in_points_and_pixels) {
  const sdlfe::FrameShape shapes[] = {{640, 480, true}, {1280, 720, true}, {640, 480, true, 960, 540}};
  for (const sdlfe::FrameShape& s : shapes)
    for (float density : {1.0f, 1.5f, 2.0f}) {
      const float pw = 2560, ph = 1440, ww = pw / density, wh = ph / density;  // fullscreen, 2560x1440 pixels
      const sdlfe::FitRect rp = sdlfe::fit_rect(pw, ph, s), rw = sdlfe::fit_rect(ww, wh, s);
      for (float fx : {0.0f, 100.5f, static_cast<float>(s.client_w()) - 1}) {
        float wx = 0, wy = 0, px = 0, py = 0;
        sdlfe::frame_to_window(fx, 40.0f, ww, wh, s, &wx, &wy);
        sdlfe::frame_to_window(fx, 40.0f, pw, ph, s, &px, &py);
        EXPECT(std::fabs(wx * density - px) < density + 0.5f && std::fabs(wy * density - py) < density + 0.5f);
      }
      EXPECT(std::fabs(rw.w * density - rp.w) <= density + 0.5f);
    }
}

// The overlay's empty test (performance.md): an unlock whose layer is all key
// draws nothing. Every pixel of every row counts, the row's padding does not.
TEST(sdl_layer_empty_sees_any_drawn_pixel) {
  const uint16_t key = 0xf81f;
  for (int w : {1, 3, 4, 7, 960}) {
    const int pitch_px = w + 3, h = 3;  // three pixels of padding a row
    std::vector<uint16_t> px(static_cast<size_t>(pitch_px) * h, key);
    for (int y = 0; y < h; ++y) px[static_cast<size_t>(y) * pitch_px + w] = 0x1234;  // padding, not the layer
    EXPECT(sdlfe::SdlScreen2D::layer_empty(px.data(), w, h, pitch_px * 2, key));
    for (int at : {0, w - 1, (h - 1) * pitch_px + w - 1, pitch_px + w / 2}) {
      px[static_cast<size_t>(at)] = 0;
      EXPECT(!sdlfe::SdlScreen2D::layer_empty(px.data(), w, h, pitch_px * 2, key));
      px[static_cast<size_t>(at)] = key;
    }
  }
}

// The layer's 16-bit to RGBA8 table (performance.md): every value of both
// formats, each channel widened by repeating its top bits, alpha opaque.
// The 2D layer onto a larger frame (resolution.md step 4, the art's scale):
// sharp bilinear, so at a whole scale every frame pixel is one texel's own
// colour (nearest), a lone texel beside the key keeps its colour (the layer
// is premultiplied), and at 1.5 a texel's first pixel is still its own.
TEST(sdl_layer_scales_sharp) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  sdlfe::SdlWindow w;
  EXPECT(w.init(true));
  std::vector<uint16_t> px(640 * 480, 0xf81f);
  for (int y = 100; y < 104; ++y)
    for (int x = 100; x < 104; ++x) px[y * 640 + x] = (x & 1) ? 0xf800 : 0xffff;  // red and white columns
  px[300 * 640 + 300] = 0xffff;                                                 // alone in the key
  auto shot = [&](int fw, int fh, std::vector<uint32_t>* argb) {
    w.create("mw3 layer test", fw, fh);
    w.frame_size(fw, fh, 640, 480);
    sdlfe::SdlScreen2D screen(w.render());
    w.render()->set_viewport(0, 0, fw, fh);
    w.render()->clear(true, 0, 0, 0);
    screen.overlay_16(px.data(), 640, 480, 640 * 2, true, 0xf81f, nullptr, nullptr);
    int rw = 0, rh = 0;
    return w.render()->read_back(argb, &rw, &rh) && rw == fw && rh == fh;
  };
  std::vector<uint32_t> f;
  if (!shot(1280, 960, &f)) return;  // a hidden window of that size (none on a headless runner)
  int mixed = 0;
  for (int y = 200; y < 208; ++y)
    for (int x = 200; x < 208; ++x) mixed += f[y * 1280 + x] != ((x / 2) & 1 ? 0xffff0000u : 0xffffffffu);
  std::printf("layer at 2x: %d of 64 pixels not their texel's; the lone texel %08x\n", mixed, f[600 * 1280 + 600]);
  EXPECT(mixed == 0);
  EXPECT(f[600 * 1280 + 600] == 0xffffffffu && f[601 * 1280 + 601] == 0xffffffffu && f[602 * 1280 + 602] == 0xff000000u);
  EXPECT(shot(960, 720, &f));
  const uint32_t first = f[450 * 960 + 450];
  std::printf("layer at 1.5x: the lone texel's first pixel %08x\n", first);
  EXPECT((first & 0xff) >= 0xf8 && (first >> 8 & 0xff) >= 0xf8 && (first >> 16 & 0xff) >= 0xf8);
}

// Playtest W22: the menus' whole-screen layer (present_16, the pause screen)
// on a 16:9 canvas set the 3D's viewport to the canvas and left it there;
// the mission resumed (the same mode: nothing set it again) drew its
// 1920x1080 frame's top-left 853x480 over the whole window. The 3D's
// viewport is the frame's again after either layer.
TEST(sdl_layers_keep_the_3d_viewport) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  sdlfe::SdlWindow w;
  EXPECT(w.init(true));
  w.create("mw3 viewport test", 1920, 1080);
  w.frame_size(1920, 1080, 853, 480);
  sdlfe::SdlScreen2D screen(w.render());
  std::vector<uint16_t> px(853 * 480, 0x1234);
  int v[4] = {};
  auto read = [&] { w.render()->viewport(&v[0], &v[1], &v[2], &v[3]); };
  w.render()->set_viewport(0, 0, 1920, 1080);
  screen.present_16(px.data(), 853, 480, 853 * 2, true);
  read();
  std::printf("after the whole-screen layer: %d %d %d %d\n", v[0], v[1], v[2], v[3]);
  EXPECT(v[0] == 0 && v[1] == 0 && v[2] == 1920 && v[3] == 1080);
  screen.overlay_16(px.data(), 853, 480, 853 * 2, true, 0xf81f, nullptr, nullptr);
  read();
  EXPECT(v[0] == 0 && v[1] == 0 && v[2] == 1920 && v[3] == 1080);
}


// W20 (FIX (A)): FOGCOLOR applies per draw, as in Direct3D. A saturated
// point light (the jump jets' flicker lights) sets the device fog colour for
// its polygons and the world's fog colour is set again for the next fogged
// ones; the GPU layer used the frame's last colour for every fogged draw,
// tinting the whole scene. Two fully fogged quads in red and blue, then green
// set with nothing drawn: red and blue they stay.
TEST(sdl_render3d_fog_colour_is_per_draw) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  sdlfe::SdlWindow w;
  EXPECT(w.init(true));
  w.create("mw3 fog test", 640, 480);
  w.frame_size(640, 480, 640, 480);
  platform::Render3D* r = w.render();
  r->set_viewport(0, 0, 640, 480);
  r->clear(true, 0, 0, 0);
  r->begin_scene();
  platform::DrawState st;
  st.fog = true;
  auto quad = [&](float x0, float x1) {
    const uint32_t white = 0xffffffffu, fogged = 0x00000000u;  // specular alpha 0: all fog
    const platform::TLVertex v[4] = {{x0, 100, 0.5f, 1, white, fogged, 0, 0}, {x1, 100, 0.5f, 1, white, fogged, 0, 0},
                                     {x1, 380, 0.5f, 1, white, fogged, 0, 0}, {x0, 380, 0.5f, 1, white, fogged, 0, 0}};
    r->draw(platform::Primitive::kFan, v, 4, st);
  };
  r->set_fog_color(255, 0, 0);
  quad(40, 280);
  r->set_fog_color(0, 0, 255);
  quad(360, 600);
  r->set_fog_color(0, 255, 0);  // the frame's last colour, on no draw
  r->end_scene();
  std::vector<uint32_t> f;
  int rw = 0, rh = 0;
  if (!r->read_back(&f, &rw, &rh) || rw != 640 || rh != 480) return;  // no window here (a headless runner)
  const uint32_t left = f[240 * 640 + 160], right = f[240 * 640 + 480];
  std::printf("fogged quads: left %08x, right %08x\n", left, right);
  EXPECT((left & 0xffffff) == 0xff0000u && (right & 0xffffff) == 0x0000ffu);
}
// Playtest W25: a message box over the lab's (or a mission's) GPU frame at
// 16:9 showed over the 16-bit buffer's 640x480 copy of it, low-res until
// answered. The frame shown is kept at its own size (hold_frame) and put
// back under each of the box's frames (restore_frame): after another frame
// drawn over, the kept one returns pixel for pixel at the frame's size.
TEST(sdl_render_keeps_a_shown_frame) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  sdlfe::SdlWindow w;
  EXPECT(w.init(true));
  w.create("mw3 hold test", 1280, 720);
  w.frame_size(1280, 720, 853, 480);
  auto* r = w.render();
  auto frame = [&](uint8_t red) {
    r->set_viewport(0, 0, 1280, 720);
    r->clear(true, red, 40, 200);
    r->begin_scene();
    r->end_scene();
    r->present();
  };
  frame(10);
  std::vector<uint32_t> a, b, c;
  int aw = 0, ah = 0, bw = 0, bh = 0, cw = 0, ch = 0;
  if (!r->read_back(&a, &aw, &ah)) return;  // no GPU on this runner
  EXPECT(r->hold_frame());
  frame(250);
  EXPECT(r->read_back(&b, &bw, &bh) && b != a);
  EXPECT(r->restore_frame());
  EXPECT(r->read_back(&c, &cw, &ch));
  std::printf("kept %dx%d, back %dx%d, same %d\n", aw, ah, cw, ch, c == a);
  EXPECT(cw == aw && ch == ah && c == a);
  r->drop_frame();
  EXPECT(!r->restore_frame());
}

// A mission's colour clear is off (start_mission): a new back buffer, at the first frame or after
// a size change, still starts a defined black where no draw reaches. Metal's undefined memory
// happened to be magenta; anything but the defined colour fails here.
// FIX (bug), Owen 2026-11-04: what no draw covers in a mission takes the fog's colour; black with fog off.
TEST(mission_frame_fill_is_the_fogs_colour) {
  const zmod::Fog kept = zmod::fog();
  const float tan[3] = {0.8f, 0.5f, 0.25f};
  zmod::fog().on = 1;
  for (int i = 0; i < 3; ++i) zmod::fog().color[i] = tan[i];
  uint8_t r = 1, g = 1, b = 1;
  zmod::fog_frame_fill(&r, &g, &b);
  EXPECT(r == 204 && g == 127 && b == 63);
  zmod::fog().on = 0;
  zmod::fog_frame_fill(&r, &g, &b);
  EXPECT(r == 0 && g == 0 && b == 0);
  zmod::fog() = kept;
}

TEST(sdl_render_new_frame_is_defined_with_the_colour_clear_off) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  sdlfe::SdlWindow w;
  EXPECT(w.init(true));
  w.create("mw3 new frame test", 640, 480);
  w.frame_size(640, 480, 640, 480);
  auto* r = w.render();
  auto scene = [&](bool colour, uint8_t red, bool quad) {
    r->set_viewport(0, 0, 640, 480);
    r->clear(colour, red, 0, 0);
    r->begin_scene();
    if (quad) {
      const uint32_t white = 0xffffffffu;
      const platform::TLVertex v[4] = {{0, 0, 0.5f, 1, white, white, 0, 0}, {320, 0, 0.5f, 1, white, white, 0, 0},
                                       {320, 480, 0.5f, 1, white, white, 0, 0}, {0, 480, 0.5f, 1, white, white, 0, 0}};
      r->draw(platform::Primitive::kFan, v, 4, platform::DrawState{});
    }
    r->end_scene();
  };
  auto resize = [&](int width, int height) {
    SDL_SetWindowSize(w.sdl(), width, height);
    SDL_SyncWindow(w.sdl());
  };
  // Every pixel of the frame's right half is `want`, the left half white when `quad`.
  auto check = [&](const char* what, uint32_t want, bool quad) {
    std::vector<uint32_t> f;
    int rw = 0, rh = 0;
    EXPECT(r->read_back(&f, &rw, &rh));
    int bad = 0;
    for (int y = 0; y < rh; ++y)
      for (int x = 0; x < rw; ++x) {
        if (x >= rw / 2 - 2 && x <= rw / 2 + 2) continue;  // the quad's edge
        bad += (f[y * rw + x] & 0xffffffu) != (quad && x < rw / 2 ? 0xffffffu : want);
      }
    std::printf("%s: %dx%d, %d wrong\n", what, rw, rh, bad);
    EXPECT(bad == 0);
  };
  scene(false, 200, true);
  std::vector<uint32_t> probe;
  int pw = 0, ph = 0;
  if (!r->read_back(&probe, &pw, &ph)) return;  // no GPU on this runner
  check("first frame, clear off", 0, true);
  scene(false, 0, false);
  check("second frame keeps it", 0, true);

  resize(800, 600);
  scene(false, 200, true);  // a colour asked with the clear off is not used
  check("size change, clear off", 0, true);
  resize(640, 480);
  scene(true, 90, false);
  check("size change, clear on: the colour asked", 90u << 16, false);

  // A restored frame is not cleared over, though the size change made a new texture for it.
  scene(true, 0, true);
  std::vector<uint32_t> held, back;
  int hw = 0, hh = 0;
  EXPECT(r->read_back(&held, &hw, &hh) && r->hold_frame());
  resize(800, 600);
  scene(true, 40, false);
  resize(640, 480);
  EXPECT(r->restore_frame());
  scene(false, 0, false);
  int bw = 0, bh = 0;
  EXPECT(r->read_back(&back, &bw, &bh));
  std::printf("restored: %dx%d, same %d\n", bw, bh, back == held);
  EXPECT(bw == hw && bh == hh && back == held);
  r->drop_frame();
}

TEST(sdl_layer_rgba_table_matches_the_bits) {
  int bad = 0;
  for (int f = 0; f < 2; ++f)
    for (uint32_t p = 0; p < 65536; ++p) {
      const bool rgb565 = f == 1;
      const uint32_t r5 = rgb565 ? p >> 11 : (p >> 10) & 31, b5 = p & 31;
      const uint32_t g6 = rgb565 ? (p >> 5) & 63 : ((p >> 5) & 31) << 1;
      const uint32_t want = (r5 << 3 | r5 >> 2) | (g6 << 2 | g6 >> 4) << 8 | (b5 << 3 | b5 >> 2) << 16 | 0xff000000u;
      bad += sdlfe::SdlScreen2D::rgba(static_cast<uint16_t>(p), rgb565) != want;
    }
  EXPECT(bad == 0);
}

namespace {
struct Mounted {
  zutl::Mounts saved = zutl::mounts();
  Mounted() {
    zutl::mounts() = zutl::Mounts{};
    zutl::mount(MW3_GAME_DIR);
  }
  ~Mounted() { zutl::mounts() = saved; }
};
bool have_fonts() { return std::filesystem::exists(std::string(MW3_GAME_DIR) + "/arial.ttf"); }
}  // namespace

TEST(sdl_host_faces_use_the_games_ttf_files) {
  EXPECT(sdlfe::SdlTwodHost::font_file("Arial") == "arial.ttf");
  EXPECT(sdlfe::SdlTwodHost::font_file("Lucida Console") == "lucon.ttf");
  EXPECT(sdlfe::SdlTwodHost::font_file("Impact") == "impact.ttf");
  EXPECT(sdlfe::SdlTwodHost::font_file("Times New Roman") == "arial.ttf");
}

TEST(sdl_host_gdi_text_measures_and_draws) {
  if (!have_fonts()) return;
  Mounted m;
  zvid::set_pixel_format(5, 6, 5);
  sdlfe::SdlTwodHost h(nullptr, nullptr, nullptr);
  const zgame::Handle f = h.CreateFontA(-16, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 1, 0, "Arial");
  EXPECT(f != 0);
  const zgame::Handle dc = h.CreateCompatibleDC(0);
  EXPECT(h.SelectObject(dc, f) == 0);
  int32_t w1 = 0, hh = 0, w2 = 0;
  EXPECT(h.GetTextExtentPoint32A(dc, "MECH", 4, &w1, &hh) == 1);
  EXPECT(h.GetTextExtentPoint32A(dc, "MECHMECH", 8, &w2, &hh) == 1);
  EXPECT(w1 > 20 && w2 > w1 * 3 / 2 && hh >= 16 && hh <= 22);
  ztwod::TextMetrics tm;
  EXPECT(h.GetTextMetricsA(dc, &tm) == 1 && tm.height == hh && tm.ascent > 0 && tm.ascent < tm.height);
  ztwod::Rect r{};
  EXPECT(h.DrawTextA(dc, "MECH\nWARRIOR", -1, &r, 0x400) == 2 * hh);  // DT_CALCRECT
  EXPECT(r.bottom == 2 * hh && r.right > w1);
  // Drawn into a 16-bit image in the text colour, nothing else touched.
  zimg::Texture img{};
  img.flags = zimg::kTwoBytes;
  img.width = static_cast<int16_t>(r.right), img.height = static_cast<int16_t>(r.bottom);
  std::vector<uint16_t> px(static_cast<size_t>(r.right) * r.bottom, 0);
  img.pixels = px.data();
  const zgame::Handle idc = h.image_dc(&img);
  h.SelectObject(idc, f);
  h.SetTextColor(idc, 0x0000ff);  // COLORREF red
  h.SetBkMode(idc, 1);            // TRANSPARENT
  h.DrawTextA(idc, "MECH\nWARRIOR", -1, &r, 0);
  const uint16_t red = zvid::pack_bytes(255, 0, 0);
  int lit = 0, other = 0, top = 0;
  for (size_t i = 0; i < px.size(); ++i) {
    lit += px[i] == red, other += px[i] != red && px[i] != 0;
    top += px[i] == red && i < px.size() / 2;
  }
  EXPECT(lit > 50 && other == 0);  // no antialiasing: the key-colour blit keeps clean edges
  EXPECT(top > 0 && top < lit);    // two lines
  h.image_dc_release(&img, idc);
  // Opaque mode fills the text's cells with the background colour first.
  std::fill(px.begin(), px.end(), uint16_t{0});
  const zgame::Handle idc2 = h.image_dc(&img);
  h.SelectObject(idc2, f);
  h.SetTextColor(idc2, 0x0000ff);
  h.SetBkMode(idc2, 2);
  h.SetBkColor(idc2, 0x00ff00);
  h.DrawTextA(idc2, "MECH", -1, &r, 0);
  const uint16_t green = zvid::pack_bytes(0, 255, 0);
  int greens = 0;
  for (uint16_t p : px) greens += p == green;
  EXPECT(greens > 50 && px[px.size() - 1] == 0);  // the first line's cells only
  h.DeleteDC(idc2);
  // A positive height is the cell height, as GDI's.
  const zgame::Handle cell = h.CreateFontA(20, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 1, 0, "Arial");
  h.SelectObject(dc, cell);
  EXPECT(h.GetTextMetricsA(dc, &tm) == 1 && tm.height >= 19 && tm.height <= 21);
  h.DeleteDC(dc);
  h.DeleteObject(f);
  h.DeleteObject(cell);
}

// GDI's font sizes and widths (twod.md section 6, Text widths): a cell
// height takes the largest VDMX size that fits (else the win metrics
// scaled), the ascent and descent its record's; the advances are the
// hinted ones the fonts' hdmx tables record for GDI, and nothing is kerned.
TEST(sdl_host_fonts_take_gdis_sizes_and_widths) {
  sdlfe::GdiFaceMetrics m;
  m.upem = 2048, m.win_ascent = 1854, m.win_descent = 434;  // Arial's
  m.vdmx = {{9, 9, -3}, {10, 10, -3}, {11, 11, -3}, {12, 12, -3}, {13, 13, -3}, {14, 13, -3}, {15, 14, -3}};
  auto size = [&](int h) { return sdlfe::gdi_font_size(m, h); };
  // 16: 13 and 14 both have a 16 cell; the first that matches (Wine's load_VDMX).
  EXPECT(size(16).ppem == 13 && size(16).ascent == 13 && size(16).descent == 3);
  EXPECT(size(12).ppem == 9 && size(12).ascent == 9 && size(12).descent == 3);  // as Windows (Wine's tests)
  EXPECT(size(-10).ppem == 10 && size(-10).ascent == 10 && size(-10).descent == 3);
  EXPECT(size(30).ppem == 27);  // past the table: scaled (30 x 2048 / 2288, rounded)
  EXPECT(size(-40).ppem == 40 && size(-40).ascent == 36 && size(-40).descent == 8);
  m.vdmx.clear();
  EXPECT(size(16).ppem == 14 && size(12).ppem == 11);  // 12: 10.74 rounds to 11, 11's cell 12.3 -> 12
  m.upem = 1000, m.win_ascent = 1150, m.win_descent = 350;
  EXPECT(size(7).ppem == 4);  // 4.67 rounds to 5, whose cell (7.5 -> 8) is over 7: one less
  if (!have_fonts()) return;
  Mounted mnt;
  zvid::set_pixel_format(5, 6, 5);
  sdlfe::SdlTwodHost h(nullptr, nullptr, nullptr);
  const zgame::Handle dc = h.CreateCompatibleDC(0);
  auto extent = [&](int32_t height, const char* face, const char* text, int32_t* cy = nullptr) {
    const zgame::Handle f = h.CreateFontA(height, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 1, 0, face);
    h.SelectObject(dc, f);
    int32_t w = 0, hh = 0;
    h.GetTextExtentPoint32A(dc, text, static_cast<int32_t>(std::strlen(text)), &w, &hh);
    if (cy) *cy = hh;
    h.SelectObject(dc, 0);
    h.DeleteObject(f);
    return w;
  };
  // Arial's hdmx at 11, 13, 15 and 16 pixels: "Current Tonnage" 81, 95, 110, 116.
  EXPECT(extent(-11, "Arial", "Current Tonnage") == 81);
  EXPECT(extent(-13, "Arial", "Current Tonnage") == 95);
  EXPECT(extent(-15, "Arial", "Current Tonnage") == 110);
  EXPECT(extent(-16, "Arial", "Current Tonnage") == 116);
  EXPECT(extent(-16, "Arial", "AVATAR") == extent(-16, "Arial", "A") * 3 + extent(-16, "Arial", "V") +
                                              extent(-16, "Arial", "T") + extent(-16, "Arial", "R"));  // no kerning
  int32_t cy = 0;
  extent(16, "Arial", "x", &cy);
  EXPECT(cy == 16);  // the salvage screen's lab_large fonts: 13 pixels, a 16 cell
  extent(12, "Arial", "x", &cy);
  EXPECT(cy == 12);
  extent(12, "Lucida Console", "x", &cy);
  EXPECT(cy == 12);  // no VDMX: 1616 + 432 units scaled
  h.DeleteDC(dc);
}

TEST(sdl_host_word_break_wraps_in_the_box) {
  if (!have_fonts()) return;
  Mounted m;
  sdlfe::SdlTwodHost h(nullptr, nullptr, nullptr);
  const zgame::Handle f = h.CreateFontA(-12, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 1, 0, "Arial");
  const zgame::Handle dc = h.CreateCompatibleDC(0);
  h.SelectObject(dc, f);
  ztwod::Rect r{0, 0, 80, 0};
  const int32_t one = h.DrawTextA(dc, "A", -1, &r, 0x400);
  r = {0, 0, 80, 0};
  const int32_t many = h.DrawTextA(dc, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG", -1, &r, 0x410);
  EXPECT(many >= 3 * one && r.right <= 80);
  r = {0, 0, 0, 0};
  int32_t w_amp = 0, w_plain = 0, hh = 0;
  h.DrawTextA(dc, "Save &As", -1, &r, 0x400);
  w_amp = r.right;
  h.GetTextExtentPoint32A(dc, "Save As", 7, &w_plain, &hh);
  EXPECT(w_amp == w_plain);  // the & prefix is not drawn
  h.DeleteDC(dc);
  h.DeleteObject(f);
}

// The main menu's ripple (0x00558dd0, zrndr::ripple) through the SDL host:
// on the finish target the program's buffer stands for, the ring's pixels
// move, the inside and everything past the square stay, and the clip holds.
TEST(sdl_host_ripple_moves_the_ring_on_the_screen) {
  const int w = 640, h = 480;
  std::vector<uint16_t> screen(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) screen[static_cast<size_t>(y) * w + x] = static_cast<uint16_t>(x * 7 + y * 13);
  const std::vector<uint16_t> before = screen;
  zrndr::set_finish_target(screen.data(), w, h, w * 2);
  sdlfe::SdlTwodHost host(nullptr, nullptr, nullptr);
  host.ripple(320, 240, 64, 100, 7, 12.0f, 0.0f, nullptr);  // a menu ring: 3 waves of 12 inside 100
  int moved = 0, inside_moved = 0, outside_moved = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const int d2 = (x - 320) * (x - 320) + (y - 240) * (y - 240);
      const bool changed = screen[i] != before[i];
      if (d2 < 60 * 60) inside_moved += changed;
      else if (std::abs(x - 320) > 100 || std::abs(y - 240) > 100) outside_moved += changed;
      else moved += changed;
    }
  EXPECT(moved > 5000);
  EXPECT(inside_moved == 0);
  EXPECT(outside_moved == 0);

  // Clipped to the left half: nothing right of the clip changes.
  screen = before;
  const ztwod::Rect clip{0, 0, 300, h - 1};
  host.ripple(320, 240, 64, 100, 7, 12.0f, 0.0f, &clip);
  int right = 0, left = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      (x >= 300 ? right : left) += screen[i] != before[i];
    }
  EXPECT(right == 0);
  EXPECT(left > 1000);
  zrndr::set_finish_target(nullptr, 0, 0, 0);
}

// zImage's release (0x00517b10) as the host does it: a named image is shared
// and counted, a fresh copy read once the count is back to 0; a grab is
// freed on its release, and a second release of it is caught (FIX (bug))
// instead of freeing it twice.
TEST(sdl_host_image_release_counts_and_frees) {
  if (!std::filesystem::exists(std::string(MW3_GAME_DIR) + "/zbd/rimage.zbd")) return;
  zutl::Mounts saved = zutl::mounts();
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(std::string(MW3_GAME_DIR) + "/zbd");
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  zimg::register_image_library(1);
  {
    sdlfe::SdlTwodHost h(nullptr, nullptr, nullptr);
    zimg::Texture* a = h.image_get("dmg_head_blue");
    zimg::Texture* b = h.image_get("dmg_head_blue");
    EXPECT(a && a == b);  // one image, counted twice
    EXPECT(h.image_release(a) == nullptr);
    EXPECT(h.image_get("dmg_head_blue") == a);  // still held once: the same
    h.image_release(a);
    h.image_release(a);  // the count at 0: freed
    zimg::Texture* c = h.image_get("dmg_head_blue");
    EXPECT(c && c->width > 0 && c->pixels);  // a fresh copy, readable
    h.image_release(c);

    std::vector<uint16_t> buf(8 * 4, 0x1234);
    zrndr::Target2D& t = zrndr::target2d();
    const zrndr::Target2D keep = t;
    t.base = reinterpret_cast<uint8_t*>(buf.data()), t.width = 8, t.height = 4, t.pitch = 16;
    zimg::Texture* g = h.screen_grab(1);
    EXPECT(g && g->width == 8 && static_cast<uint16_t*>(g->pixels)[5] == 0x1234);
    h.image_release(g);  // a grab: freed now
    h.image_release(g);  // again: reported, not freed twice
    t = keep;
  }
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  zutl::mounts() = saved;
}

// The MFD's damage diagram (mode 2) with the shipped silhouettes, drawn into
// the program's keyed layer as the HUD draws it: each part's pixels show
// opaque by the overlay's rule (app_engine, sdl_platform's upload) at every
// level, full health blue (hud.md section 16). Seven parts' blue outlines are
// the colour of the translucent blue fill under them (0x001f); drawn
// see-through until opaque writes ended the fill's record (Owen's playtest:
// only the right arm, 0x003f, showed).
TEST(sdl_host_damage_diagram_shows_every_part) {
  if (!std::filesystem::exists(std::string(MW3_GAME_DIR) + "/zbd/rimage.zbd")) return;
  zutl::Mounts saved = zutl::mounts();
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(std::string(MW3_GAME_DIR) + "/zbd");
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  zimg::register_image_library(1);
  zvid::set_pixel_format(5, 6, 5);
  struct Levels : mech3::HudHooks {
    float level[8] = {1, 1, 1, 1, 1, 1, 1, 1};  // by the view's parts' order (0x00447a50)
    float part_internal(void*, uint32_t part, int32_t section) override {
      const int i = part == 0x3a4 ? 0 : part == 0x39c ? (section == 1 ? 1 : section == 2 ? 2 : 3)
                  : part == 0x394 ? 4 : part == 0x398 ? 5 : part == 0x38c ? 6 : 7;
      return level[i];
    }
  } levels;
  const mech3::DamageSet kept = mech3::damage_set();
  mech3::damage_set() = mech3::DamageSet{};
  {
    sdlfe::SdlTwodHost h(nullptr, nullptr, nullptr);
    ztwod::set_host(&h);
    mech3::set_hud_hooks(&levels);
    mech3::HudDamageView view(0, 0, false);
    view.bg.set_image(mech3::damage_set().bg[0]);  // the MFD's (0x00442060)
    view.bg.show_for(-1.0f);
    static int fake_mech;
    view.set_mech(&fake_mech);
    const uint16_t key = mech3::GameEngine::kLayerKey;
    const int w = 80, hgt = 100;
    std::vector<uint16_t> layer(w * hgt);
    std::vector<uint8_t> cover_a(w * hgt);
    std::vector<uint16_t> cover_c(w * hgt);
    zrndr::Target2D& t = zrndr::target2d();
    const zrndr::Target2D keep = t;
    t.base = reinterpret_cast<uint8_t*>(layer.data()), t.width = w, t.height = hgt, t.pitch = w * 2, t.format = 6;
    // Full health, then every part at each damaged band: its outline's
    // texels opaque in the layer, in the image's colour.
    const float bands[] = {1.0f, 0.8f, 0.5f, 0.3f, 0.1f};
    for (float b : bands)
      for (int p = 0; p < 8; ++p) {
        for (float& l : levels.level) l = 1.0f;
        levels.level[p] = b;
        view.update_levels();
        std::fill(layer.begin(), layer.end(), key);
        std::fill(cover_a.begin(), cover_a.end(), 0);
        zrndr::layer_cover() = {cover_a.data(), cover_c.data(), key};
        view.update(0.0f);
        zrndr::layer_cover() = {};
        for (int q = 0; q < 8; ++q) {
          const mech3::HudDamagePart& part = view.parts[q];
          const zimg::Texture* img = part.image;
          int texels = 0, opaque = 0;
          if (img)
            for (int y = 0; y < img->height; ++y)
              for (int x = 0; x < img->width; ++x) {
                const uint16_t c = static_cast<const uint16_t*>(img->pixels)[y * img->width_again + x];
                if (!c) continue;  // keyed (black)
                ++texels;
                const int at = (part.y + y) * w + part.x + x;
                const bool shown = layer[at] == c && !(cover_a[at] && cover_c[at] == layer[at]);
                opaque += shown;
              }
          if (!img || opaque != texels)
            std::printf("  level %.1f on part %d: part %d shows %d of %d texels opaque\n", b, p, q, opaque, texels);
          EXPECT(img && texels > 0 && opaque == texels);
        }
      }
    t = keep;
    mech3::set_hud_hooks(nullptr);
    ztwod::set_host(nullptr);
  }
  mech3::damage_set() = kept;
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  zutl::mounts() = saved;
}

// Playtest #8: a HUD weapon line through the SDL host with the game's fonts.
// Half recharged, the bar's left half is the name in wepstring_green and the
// right half the name in wepstring_red (hud.md section 13).
TEST(sdl_host_hud_weapon_line_shows_the_recharge) {
  if (!have_fonts()) return;
  Mounted m;
  zvid::set_pixel_format(5, 6, 5);
  zrdr::init();
  EXPECT(zrdr::open_archive("zbd\\reader.zbd", true));  // fonts.zrd
  sdlfe::SdlTwodHost h(nullptr, nullptr, nullptr);
  ztwod::set_host(&h);
  {
    mech3::HudWeaponLine line;
    line.red = 0;
    EXPECT(line.set_look(true, false));  // live with ammo: the name red, the rest green
    line.name.set_text_plain("ER LARGE LASER");
    line.text2.set_text_plain("ER LARGE LASER");
    line.set_pos(100, 100);
    line.show_for(-1.0f);
    line.update(0.0f);
    EXPECT(line.fill != nullptr && line.empty != nullptr);
    std::vector<uint16_t> buf(640 * 480, 0);
    zrndr::Target2D& t = zrndr::target2d();
    const zrndr::Target2D saved = t;
    t.base = reinterpret_cast<uint8_t*>(buf.data()), t.width = 640, t.height = 480, t.pitch = 1280;
    if (line.fill && line.empty) {
      line.set_value(0.5f);
      line.draw();
      const int cut = 100 + line.fill_src.right;
      std::map<uint16_t, int> left, right;
      for (int y = 100; y < 100 + line.fill->height; ++y)
        for (int x = 100; x < 100 + line.empty->width; ++x)
          if (uint16_t p = buf[static_cast<size_t>(y) * 640 + x]) ++(x < cut ? left : right)[p];
      auto top = [](const std::map<uint16_t, int>& c) {
        uint16_t best = 0;
        int n = 0;
        for (auto [p, k] : c)
          if (k > n) best = p, n = k;
        return best;
      };
      std::printf("weapon line: fill %dx%d cut %d, left %04x x%zu, right %04x x%zu\n", line.fill->width,
                  line.fill->height, line.fill_src.right, top(left), left.size(), top(right), right.size());
      EXPECT(!left.empty() && !right.empty());
      EXPECT(top(left) != top(right));  // green filled, red still to fill
    }
    t = saved;
  }
  ztwod::set_host(nullptr);
  zrdr::close_archives(true);
}

namespace {
// The program's platform without a window (test_program.cpp's fakes), the
// presented 16-bit frame kept.
struct NoWindow : platform::Window {
  uint32_t create(const std::string&, int, int) override { return 0x1234; }
  void set_title(const std::string&) override {}
  void show() override {}
  void client_size(int* w, int* h) override { *w = 640, *h = 480; }
};
struct NoAlert : platform::Alert {
  void abort_message(const std::string&) override {}
};
struct StepClock : platform::Clock {
  uint32_t now = 0;
  uint32_t ticks_ms() override { return now += 16; }
};
// zFMV's clock: a second a call, so the Intro's SPLASH wait passes in a few frames.
struct MovieClock : platform::Clock {
  uint32_t now = 0;
  uint32_t ticks_ms() override { return now += 1000; }
};
platform::Clock& fast_movie_clock() {
  static MovieClock c;
  return c;
}
struct KeepScreen : platform::Screen2D {
  std::vector<uint16_t> last;  // the 2D frame shown, or the layer laid over the 3D frame
  void keep(const uint16_t* px, int w, int h, int pitch) {
    if (w != 640 || h != 480) return;
    last.resize(640 * 480);
    for (int y = 0; y < h; ++y) std::copy(px + y * (pitch / 2), px + y * (pitch / 2) + w, last.begin() + y * w);
  }
  void present_16(const uint16_t* px, int w, int h, int pitch, bool) override { keep(px, w, h, pitch); }
  void overlay_16(const uint16_t* px, int w, int h, int pitch, bool, uint16_t, const uint8_t*,
                  const uint16_t*) override {
    keep(px, w, h, pitch);
  }
};
// A Direct3D device that draws nothing (test_program's pause test).
struct NoGpu : platform::Render3D {
  int32_t create_texture(int, int, const uint8_t*, uint8_t) override { return 1; }
  void release_texture(int32_t) override {}
  void update_texture(int32_t, int, int, const uint8_t*) override {}
  void set_viewport(int, int, int, int) override {}
  void clear(bool, uint8_t, uint8_t, uint8_t) override {}
  void begin_scene() override {}
  void end_scene() override {}
  void draw(platform::Primitive, const platform::TLVertex*, int, const platform::DrawState&) override {}
  void set_fog_color(uint8_t, uint8_t, uint8_t) override {}
  void present() override {}
  bool read_back(std::vector<uint32_t>* argb, int* w, int* h) override {
    argb->assign(320 * 240, 0xff000000u);
    *w = 320, *h = 240;
    return true;
  }
};
struct Passes : platform::MessagePump {
  int passes = 0, limit = 200;
  bool peek() override { return ++passes > limit; }
  bool pump() override { return mech3::missions().cleanup(false), false; }
  void wait(uint32_t) override {}
  void post_quit() override {}
};
struct FireKey : platform::Input {
  std::vector<platform::KeyEvent> queue;
  uint32_t t = 0;
  void key(uint32_t dik, bool down) { queue.push_back({dik, down ? 0x80u : 0u, ++t, t}); }
  bool open(Device d) override { return d == kKeyboard; }
  void close(Device) override {}
  int32_t acquire(Device) override { return 0; }
  int32_t unacquire(Device) override { return 0; }
  int32_t keyboard_data(platform::KeyEvent* out, uint32_t* count) override {
    const uint32_t n = std::min<uint32_t>(*count, static_cast<uint32_t>(queue.size()));
    std::copy(queue.begin(), queue.begin() + n, out);
    queue.erase(queue.begin(), queue.begin() + n);
    *count = n;
    return 0;
  }
  int32_t mouse_state(platform::MouseState*) override { return kNotAcquired; }
  int32_t joystick_state(platform::JoyState*) override { return kNotAcquired; }
};
}  // namespace

// Playtest #8 end to end: c1m1 with the SDL host's 2D (no window; the
// software path, or a Direct3D device that draws nothing with the layer laid
// over it), fire, and the weapon list shows red (the bars still to refill)
// for a while, then all green again.
struct BarRun {
  int max_red = 0, red_frames = 0, green_before = 0, red_before = 0, red_end = -1;
};
void weapon_list_run(bool d3d, BarRun& out) {
  Mounted m;
  zutl::set_write_dir(std::string(MW3_TEST_TMP) + "/sdl_bars_write");
  mech3::reset_states();
  if (!zsnd::state().open) zsnd::state() = zsnd::State{};
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  mech3::damage_set() = mech3::DamageSet{};
  mech3::weapon_set() = mech3::WeaponSet{};  // its fonts are the host's handles: a new host each run
  mech3::missions() = mech3::Missions{};
  mech3::mech_types_forget();
  mech3::player_controls_instance() = nullptr;
  mech3::options() = mech3::Options{};
  FireKey keys;
  NoWindow window;
  NoAlert alert;
  StepClock clock;
  KeepScreen screen;
  platform::SilentSoundOutput sound;
  mech3::MountFiles files;
  mech3::Platform p;
  p.window = &window, p.alert = &alert, p.clock = &clock, p.movie_clock = &fast_movie_clock(), p.screen = &screen, p.sound = &sound, p.files = &files;
  p.input = &keys;
  NoGpu gpu;
  if (d3d) p.render = &gpu;
  int& max_red = out.max_red;
  int& red_frames = out.red_frames;
  int& green_before = out.green_before;
  int& red_before = out.red_before;
  int& red_end = out.red_end;
  {
    mech3::GameEngine engine(p, "mech3.exe -nofmv");
    engine.quiet = true;
    sdlfe::SdlTwodHost host(&engine, nullptr, &window);
    ztwod::set_host(&host);
    mech3::Mech3App app(&engine);
    mech3::set_app(&app);
    EXPECT(app.init_instance());
    mech3::GameEngine::Launch l;
    engine.set_launch(l);
    app.post_intro_ = &mech3::states().load;
    const uint16_t red = zvid::pack_bytes(235, 0, 0), green = zvid::pack_bytes(0, 235, 0);
    int frames = 0;
    uint32_t fire = 0;
    engine.on_present = [&](int) {
      if (app.current() != &mech3::states().core || screen.last.empty()) return;
      ++frames;
      int r = 0, g = 0;
      for (int y = 28; y < 130; ++y)  // the weapon list's ten lines: (530, 30), 10 pixels a line
        for (int x = 500; x < 640; ++x) {
          const uint16_t px = screen.last[static_cast<size_t>(y) * 640 + x];
          r += px == red, g += px == green;
        }
      if (frames == 9) green_before = g, red_before = r;
      if (frames == 10) fire = zin::key1_of(0x3d) & 0xff, keys.key(fire, true);
      if (frames == 13) keys.key(fire, false);
      if (frames > 10) max_red = std::max(max_red, r), red_frames += r > 0;
      if (frames == 120) red_end = r;
    };
    Passes pump;
    pump.limit = 300;
    app.run(pump);
    std::printf("weapon list (%s): before %d green %d red; after firing red up to %d px over %d frames; at the end "
                "%d red\n",
                d3d ? "Direct3D" : "software", green_before, red_before, max_red, red_frames, red_end);
    mech3::missions().cleanup(false);
    app.stop_game();
    app.exit_instance();
    mech3::set_app(nullptr);
    ztwod::set_host(nullptr);
  }
  zrdr::close_archives(true);
  mech3::reset_states();
  mech3::options() = mech3::Options{};
  // The interp's scroll list, which the world's unload (0x005964c0(1)) would
  // empty: the next run's set-up resets its nodes (0x005964c0(0)).
  zint::scroll_list_init();
}

TEST(sdl_program_weapon_list_shows_the_recharge) {
  if (!have_fonts()) return;
  for (bool d3d : {false, true}) {
    BarRun r;
    weapon_list_run(d3d, r);
    EXPECT(r.green_before > 100 && r.red_before == 0);  // full bars: green
    EXPECT(r.max_red > 20 && r.red_frames >= 5);        // firing: the bars empty to red and refill
    EXPECT(r.red_end == 0);                             // full again
  }
}

// Playtest #23/#24 reopened, through the SDL mouse: c1m1's Mad Cat at full
// throttle (THROTTLE0's key), its medium lasers selected and charged, the
// left button clicked as a player does (down, then up three frames later, as
// SDL events through SdlInput, read by zInput's mouse at each frame's poll).
// Logged per frame: the manager's trigger, the beam's state, how far the
// beam's origin is from the firepoint's place now. The laser fires on the
// press's frame; the beam fires on after the release until its charge is
// spent, and with the fix (bugs) it leaves from the arm all the while; the
// original's path leaves it at the last held frame's muzzle, behind.
struct ClickRun {
  int press_frame = -1, fire_frame = -1, firing_frames = 0;
  float max_stale = 0;  // the beam's origin off the firepoint while the beam shows
  std::string log;
};
static ClickRun click_run(int hold_frames, bool bugs) {
  ClickRun out;
  Mounted m;
  zutl::set_write_dir(std::string(MW3_TEST_TMP) + "/sdl_click_write");
  mech3::reset_states();
  if (!zsnd::state().open) zsnd::state() = zsnd::State{};
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  mech3::damage_set() = mech3::DamageSet{};
  mech3::weapon_set() = mech3::WeaponSet{};
  mech3::missions() = mech3::Missions{};
  mech3::mech_types_forget();
  mech3::player_controls_instance() = nullptr;
  mech3::options() = mech3::Options{};
  zin::state().joy_ready = 0;  // no stick (an earlier test's left zInput's flag set, 0x0063c4a4)
  const zfix::Options keep_fix = zfix::options();
  sdlfe::SdlInput input;
  NoWindow window;
  NoAlert alert;
  StepClock clock;
  KeepScreen screen;
  platform::SilentSoundOutput sound;
  mech3::MountFiles files;
  mech3::Platform p;
  p.window = &window, p.alert = &alert, p.clock = &clock, p.movie_clock = &fast_movie_clock(), p.screen = &screen, p.sound = &sound, p.files = &files;
  p.input = &input;
  auto key = [&](uint8_t dik, bool down) {
    for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
      if (sdlfe::dik_from_scancode(static_cast<SDL_Scancode>(sc)) == dik) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = static_cast<SDL_Scancode>(sc);
        e.key.down = down;
        input.on_event(e);
        return;
      }
  };
  auto button = [&](bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = down;
    input.on_event(e);
  };
  {
    mech3::GameEngine engine(p, "mech3.exe -nofmv");
    engine.quiet = true;
    sdlfe::SdlTwodHost host(&engine, nullptr, &window);
    ztwod::set_host(&host);
    mech3::Mech3App app(&engine);
    mech3::set_app(&app);
    EXPECT(app.init_instance());
    mech3::GameEngine::Launch l;  // c1m1, the Mad Cat
    engine.set_launch(l);
    app.post_intro_ = &mech3::states().load;
    int frames = 0, group = -1;
    void* player = nullptr;
    engine.on_present = [&](int) {
      if (app.current() != &mech3::states().core) return;
      ++frames;
      if (frames == 3) {
        // The original's path for the beam only once the mission is up (its
        // load needs the HUD constructor's fix, hud.md section 3).
        zfix::options().bugs = bugs;
        player = engine.object_of(engine.field32(engine.pilot(), 0x14));
        for (int32_t g = 0; g < 5 && group < 0; ++g)
          if (mech3::mech_group_names(player, g).find("ERLaser_Medium") != std::string::npos) group = g;
        if (group >= 0) mech3::mech_select_group(player, group);
        key(static_cast<uint8_t>(zin::key1_of(0x24) & 0xff), true);  // THROTTLE0: 100%
      }
      if (frames == 5) key(static_cast<uint8_t>(zin::key1_of(0x24) & 0xff), false);
      if (!player || group < 0) return;
      // The click, as the frame's events (seen by the next frame's poll).
      constexpr int kClick = 150;  // at speed
      if (frames == kClick) button(true), out.press_frame = frames + 1;
      if (frames == kClick + hold_frames) button(false);
      if (frames > kClick && frames <= kClick + 40) {
        mech3::BeamProbe b;
        mech3::mech_group_beam(player, group, &b);
        if (b.firing && out.fire_frame < 0) out.fire_frame = frames;
        out.firing_frames += b.firing;
        float off = 0;
        if (b.beam_active)
          off = std::sqrt((b.origin[0] - b.live[0]) * (b.origin[0] - b.live[0]) +
                          (b.origin[1] - b.live[1]) * (b.origin[1] - b.live[1]) +
                          (b.origin[2] - b.live[2]) * (b.origin[2] - b.live[2]));
        if (b.beam_active) out.max_stale = std::max(out.max_stale, off);
        char line[160];
        std::snprintf(line, sizeof line, "    %d: trigger %d/%d state %d, firing %d beam %d, ready %.2f, origin off %.2f m\n",
                      frames, b.trigger_down, b.trigger_was, b.state, b.firing, b.beam_active, b.ready, off);
        if (std::getenv("MW3_CLICK_LOG")) out.log += line;
      }
    };
    Passes pump;
    pump.limit = 600;
    app.run(pump);
    zfix::options() = keep_fix;
    mech3::missions().cleanup(false);
    app.stop_game();
    app.exit_instance();
    mech3::set_app(nullptr);
    ztwod::set_host(nullptr);
  }
  zfix::options() = keep_fix;
  zrdr::close_archives(true);
  mech3::reset_states();
  mech3::options() = mech3::Options{};
  zint::scroll_list_init();  // as the weapon list's run
  return out;
}
TEST(sdl_program_mouse_click_fires_the_laser) {
  if (!have_fonts()) return;
  const ClickRun r = click_run(3, true), o = click_run(3, false);
  for (const ClickRun* x : {&r, &o})
    std::printf("click (3 frames, %s): pressed for frame %d, fired on %d, %d frames firing, the beam's origin up to "
                "%.2f m off the firepoint\n%s",
                x == &r ? "fixed" : "the original", x->press_frame, x->fire_frame, x->firing_frames, x->max_stale,
                x->log.c_str());
  EXPECT(r.fire_frame == r.press_frame && o.fire_frame == o.press_frame);
  EXPECT(r.firing_frames > 3 && o.firing_frames > 3);  // on after the release
  EXPECT(r.max_stale < 0.05f);  // from the arm
  EXPECT(o.max_stale > 0.3f);   // the original's: left behind
}

// Finding the install at start (ours, FIX (A); sdl_install.h): a folder with
// zbd/reader.zbd; a picked Mech3.exe or reader.zbd stands for its install;
// the installer's "Program" value is a folder or the exe's path; mw3.cfg's
// install= replaced with the other lines kept; mw3.cfg's own first, then the
// search, and nothing asked when asking is not allowed.
// An install in any case (FIX (A), zutl::any_case): lower, upper and mixed, each
// part found by its letters. On a case-insensitive disk the exact name answers,
// and only the "none" lines test anything: docs/linux.md, Checking runs it on Linux.
TEST(sdl_install_any_case_finds_the_files) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_any_case";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  auto file = [&](const std::string& rel) {
    std::filesystem::create_directories(std::filesystem::path(dir + "/" + rel).parent_path(), ec);
    std::ofstream(dir + "/" + rel) << "x";
  };
  file("low/zbd/reader.zbd"), file("low/mech3msg.dll");
  file("UP/ZBD/READER.ZBD"), file("UP/VIDEO/INTRO.AVI");
  file("Mix/Zbd/Reader.zbd");
  EXPECT(sdlfe::install_valid(dir + "/low"));
  EXPECT(sdlfe::install_valid(dir + "/UP"));
  EXPECT(sdlfe::install_valid(dir + "/Mix"));
  EXPECT(!zutl::any_case(dir + "/low", "Mech3Msg.dll").empty());
  EXPECT(!zutl::any_case(dir + "/UP", "video/intro.avi").empty());
  EXPECT(!zutl::any_case(dir + "/UP", "video").empty());
  EXPECT(zutl::any_case(dir + "/low", "Mech3.exe").empty());
  EXPECT(zutl::any_case(dir + "/low", "zbd/none/reader.zbd").empty());
  // Through the mounts, each folder keeping its order: an earlier mount's copy in another case wins.
  file("a/ZBD/One.zbd"), file("b/zbd/one.zbd");
  const zutl::Mounts saved = zutl::mounts();
  zutl::mounts() = {};
  zutl::mount(dir + "/a"), zutl::mount(dir + "/b");
  zutl::mount(dir + "/UP/zbd");  // a mount named by the game, in the install's other case
  const bool in_named_mount = !zutl::find("reader.zbd").empty();
  const std::string found = zutl::find("zbd\\one.zbd");
  const bool other_missing = zutl::find("zbd\\two.zbd").empty();
  zutl::mounts() = saved;
  EXPECT(found.find("/a/") != std::string::npos);
  EXPECT(other_missing);
  EXPECT(in_named_mount);
}

TEST(sdl_install_is_found_and_saved) {
  namespace fs = std::filesystem;
  const std::string game = MW3_GAME_DIR, tmp = std::string(MW3_TEST_TMP) + "/sdl_install";
  fs::remove_all(tmp);
  fs::create_directories(tmp + "/half/zbd");
  std::string missing;
  // The search tries this machine's own candidates first, so one with MW3
  // installed (the registry, the default folders) finds that one here.
  std::string installed;
  for (const std::string& d : sdlfe::install_candidates())
    if (installed.empty() && sdlfe::install_valid(d)) installed = d;
  if (!installed.empty()) std::printf("  (MW3 is installed here: the search finds %s)\n", installed.c_str());
  if (fs::exists(game + "/zbd/reader.zbd")) {
    EXPECT(sdlfe::install_valid(game));
    EXPECT(sdlfe::install_of_pick(game + "/Mech3.exe") == game);
    EXPECT(sdlfe::install_of_pick(game + "/zbd/reader.zbd") == game);  // the zbd folder's file: the folder above
    EXPECT(sdlfe::install_of_pick(game) == game);                        // a folder: itself
    bool searched = true;
    EXPECT(sdlfe::find_install(game, {}, false, &searched) == game && !searched);  // mw3.cfg's, still there
    EXPECT(sdlfe::find_install(tmp + "/gone", {game}, false, &searched) == (installed.empty() ? game : installed) &&
           searched);
  }
  EXPECT(!sdlfe::install_valid(tmp, &missing) && missing == "zbd\\ and video\\");
  EXPECT(!sdlfe::install_valid(tmp + "/half", &missing) && missing == "zbd\\reader.zbd and video\\");
  EXPECT(sdlfe::find_install(tmp + "/gone", {tmp + "/half"}, false, nullptr) == installed);  // else none, no dialog
#if defined(_WIN32)
  EXPECT(sdlfe::install_of_program_value("\"C:\\Games\\MW3\\MECH3.EXE\"") == "C:\\Games\\MW3");
  EXPECT(sdlfe::install_of_program_value("C:\\Games\\MW3\\") == "C:\\Games\\MW3");
#else
  EXPECT(sdlfe::install_of_program_value("\"/g/MW3/MECH3.EXE\"") == "/g/MW3");
  EXPECT(sdlfe::install_of_program_value("/g/MW3/") == "/g/MW3");
#endif
  if (std::FILE* f = std::fopen((tmp + "/mw3.cfg").c_str(), "w")) {
    std::fputs("assets=/a\r\ninstall=/old\nwindowed=1\n", f);
    std::fclose(f);
  }
  EXPECT(sdlfe::save_install(tmp, "/new"));
  std::string cfg;
  if (std::FILE* f = std::fopen((tmp + "/mw3.cfg").c_str(), "r")) {
    char buf[256];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) cfg.append(buf, n);
    std::fclose(f);
  }
  EXPECT(cfg == "install=/new\nassets=/a\nwindowed=1\n");
  fs::remove_all(tmp);
}

#if !defined(_WIN32)  // posix_spawn (ours, not on Windows)
extern char** environ;

// --frames through the pause (mission-plan item 22): a launched mission's
// frames counted only those that drew the scene, so Escape's pause screen
// stopped the count and the run never ended. The program itself, Escape at
// frame 900 (the Core state's: the timed fade has taken 99 to 576) and --frames 1200,
// must quit on its own (90 s allowed).
TEST(sdl_program_frames_end_a_paused_mission) {
  if (!have_fonts()) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_frames_pause_write";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::string out = dir + "/mw3.log";
  // --frame-ms: frame 900 is the same game time however fast frames are drawn
  // (a faster build was still in the loading fade at 900 without it).
  std::vector<std::string> args = {MW3_BIN, "--mission", "c1m1", "--tap", "0x01@900", "--frames", "1200", "--frame-ms",
                                   "33", "--verbose", "--nosound", "--hidden", "--write-dir", dir};
  std::vector<char*> argv;
  for (std::string& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_adddup2(&fa, 1, 2);
  pid_t pid = 0;
  const int err = posix_spawn(&pid, MW3_BIN, &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  EXPECT(err == 0);
  if (err) return;
  int status = 0;
  bool quit = false;
  const auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(90)) {
    if (waitpid(pid, &status, WNOHANG) == pid) {
      quit = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  if (!quit) kill(pid, SIGKILL), waitpid(pid, &status, 0);
  std::string log;
  if (FILE* f = std::fopen(out.c_str(), "r")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) log.append(buf, n);
    std::fclose(f);
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("mw3 --mission c1m1 --tap 0x01@900 --frames 1200: %s after %.0f s, exit %d\n", quit ? "quit" : "killed",
              secs, quit && WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  EXPECT(quit && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  EXPECT(log.find("Pause State") != std::string::npos);  // Escape reached the pause
}

// A check run (--hidden --frames) with mw3.cfg's install gone and the search
// finding none (MW3_FIND_INSTALL=1: not the development copy) quits with the
// report, never a dialog; with the development copy it starts.
// A crash says why in the log (crash_report.h), for a player's report. The
// test's run stops at the log line, with no box and no real crash.
TEST(sdl_program_crash_says_why_in_the_log) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_crash_report";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  setenv("MW3_CRASH_TEST", "1", 1);
  std::vector<std::string> args = {MW3_BIN, "--nosound", "--write-dir", dir};
  std::vector<char*> argv;
  for (std::string& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int spawned = posix_spawn(&pid, MW3_BIN, nullptr, nullptr, argv.data(), environ);
  unsetenv("MW3_CRASH_TEST");
  EXPECT(spawned == 0);
  int status = 0;
  EXPECT(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 70);
  std::ifstream in(dir + "/mw3.log");
  const std::string log((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT(log.find("mw3: crashed: segmentation fault") != std::string::npos);
}

TEST(sdl_program_check_runs_never_ask_for_the_install) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_find_install_write";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  if (std::FILE* f = std::fopen((dir + "/mw3.cfg").c_str(), "w")) {
    std::fputs("install=/no/such/mw3\n", f);
    std::fclose(f);
  }
  auto run = [&](bool search_only, std::string* log) {
    const std::string out = dir + "/mw3.log";
    std::vector<std::string> args = {MW3_BIN, "--frames", "5", "--nosound", "--hidden", "--write-dir", dir};
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::vector<std::string> env_s;
    for (char** e = environ; *e; ++e)
      if (std::strncmp(*e, "MW3_FIND_INSTALL=", 17) != 0) env_s.push_back(*e);
    if (search_only) env_s.push_back("MW3_FIND_INSTALL=1");
    std::vector<char*> env;
    for (std::string& e : env_s) env.push_back(e.data());
    env.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    const int err = posix_spawn(&pid, MW3_BIN, &fa, nullptr, argv.data(), env.data());
    posix_spawn_file_actions_destroy(&fa);
    if (err) return -1;
    int status = 0;
    bool quit = false;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
      if (waitpid(pid, &status, WNOHANG) == pid) {
        quit = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!quit) kill(pid, SIGKILL), waitpid(pid, &status, 0);
    log->clear();
    if (FILE* f = std::fopen(out.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) log->append(buf, n);
      std::fclose(f);
    }
    return quit && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  };
  std::string log;
  const int none = run(true, &log);
  std::printf("  search only: exit %d\n", none);
  EXPECT(none == 1 && log.find("No MechWarrior 3 install found") != std::string::npos);
  if (!have_fonts()) return;
  const int dev = run(false, &log);
  std::printf("  with the development copy: exit %d\n", dev);
  EXPECT(dev == 0 && log.find("mw3: install " MW3_GAME_DIR) != std::string::npos);
  EXPECT(log.find("saved to mw3.cfg") == std::string::npos);  // the development copy is not saved
}
#endif

// The window's shape (resolution.md step 2, any_resolution): the frame keeps
// its aspect in the window, pillarboxed where they differ, and the cursor
// and the relative mouse map through that rect, not through 640.
// The 1.2 read-me's "you can now enter IP addresses using the numeric
// keypad" (14c's checks): through the SDL host, zInput's characters for the
// keypad's 1..9, 0 and '.' (DIK 0x4f..0x53). On a Mac (no Num Lock; SDL
// reports it off) the host takes it as on, FIX (A); elsewhere they follow
// the lock, as on Windows.
TEST(sdl_keypad_types_an_address) {
  sdlfe::SdlZinHost host(nullptr);
  zin::set_host(&host);
  static const uint32_t kPad[11] = {0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x52, 0x53};
  std::string typed;
  for (uint32_t k : kPad) {
    const uint32_t c = zin::key_char(k);
    typed += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '#';
  }
  zin::set_host(nullptr);
  std::printf("keypad 1..9, 0, '.': \"%s\"\n", typed.c_str());
#if defined(__APPLE__)
  EXPECT(typed == "1234567890.");
#else
  EXPECT(typed == "1234567890." || (SDL_GetModState() & SDL_KMOD_NUM) == 0);
#endif
}

TEST(sdl_window_shape_maps_the_mouse_through_the_frame) {
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
  float x = 0, y = 0;
  // 4:3 in 4:3: the whole window, as without the fix.
  const sdlfe::FrameShape v640{640, 480, true};
  const sdlfe::FitRect r43 = sdlfe::fit_rect(1280, 960, v640);
  EXPECT(r43.x == 0 && r43.y == 0 && r43.w == 1280 && r43.h == 960);
  // Fixed 16:9 modes still pillarbox on wider screens. Native custom modes
  // instead use their full shape (resolution_ultrawide_hud_and_pointer_share_the_frame).
  const sdlfe::FitRect r219 = sdlfe::fit_rect(3440, 1440, sdlfe::FrameShape{2560, 1440, true, 853, 480});
  EXPECT(r219.x == 440 && r219.y == 0 && r219.w == 2560 && r219.h == 1440);
  const sdlfe::FitRect r329 = sdlfe::fit_rect(3840, 1080, sdlfe::FrameShape{1920, 1080, true, 853, 480});
  EXPECT(r329.x == 960 && r329.y == 0 && r329.w == 1920 && r329.h == 1080);
  sdlfe::window_to_frame(640, 480, 1280, 960, v640, &x, &y);
  EXPECT(near(x, 320) && near(y, 240));
  EXPECT(near(sdlfe::mickey_scale(1280, 960, v640), 0.5f));
  // 16:9 in 16:9 (mode 11, 1280x720, in a 1920x1080 window).
  const sdlfe::FrameShape v720{1280, 720, true};
  const sdlfe::FitRect r169 = sdlfe::fit_rect(1920, 1080, v720);
  EXPECT(r169.x == 0 && r169.y == 0 && r169.w == 1920 && r169.h == 1080);
  sdlfe::window_to_frame(960, 540, 1920, 1080, v720, &x, &y);
  EXPECT(near(x, 640) && near(y, 360));
  EXPECT(near(sdlfe::mickey_scale(1920, 1080, v720), 1280.0f / 1920.0f));
  // 4:3 in 16:9: pillarboxed, 960x720 at x 160; the bars map outside the frame.
  const sdlfe::FitRect rp = sdlfe::fit_rect(1280, 720, v640);
  EXPECT(rp.x == 160 && rp.y == 0 && rp.w == 960 && rp.h == 720);
  sdlfe::window_to_frame(160, 0, 1280, 720, v640, &x, &y);
  EXPECT(near(x, 0) && near(y, 0));
  sdlfe::window_to_frame(1120, 720, 1280, 720, v640, &x, &y);
  EXPECT(near(x, 640) && near(y, 480));
  sdlfe::window_to_frame(100, 360, 1280, 720, v640, &x, &y);
  EXPECT(x < 0);
  sdlfe::frame_to_window(320, 240, 1280, 720, v640, &x, &y);
  EXPECT(near(x, 640) && near(y, 360));
  EXPECT(near(sdlfe::mickey_scale(1280, 720, v640), 640.0f / 960.0f));
  // 16:9 in 4:3: letterboxed.
  const sdlfe::FitRect rl = sdlfe::fit_rect(1280, 960, v720);
  EXPECT(rl.x == 0 && rl.y == 120 && rl.w == 1280 && rl.h == 720);
  // Without the fix: stretched over the window, the old 640 / width.
  const sdlfe::FrameShape off{640, 480, false};
  const sdlfe::FitRect ro = sdlfe::fit_rect(1280, 720, off);
  EXPECT(ro.x == 0 && ro.w == 1280 && ro.h == 720);
  EXPECT(near(sdlfe::mickey_scale(1280, 720, off), 0.5f));
  // The windows a frame opens: 640x480 at 1280x960 (as before), 1280x720 itself.
  int w = 0, h = 0;
  sdlfe::default_window(v640, &w, &h);
  EXPECT(w == 1280 && h == 960);
  sdlfe::default_window(v720, &w, &h);
  EXPECT(w == 1280 && h == 720);

  // Through the program's own pieces, in a hidden 1280x720 window: zInput's
  // client mapping (SdlZinHost) and the relative mouse (SdlInput).
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
  SDL_Window* win = SDL_CreateWindow("mw3 shape test", 1280, 720, SDL_WINDOW_HIDDEN);
  EXPECT(win != nullptr);
  if (!win) return;
  int ww = 0, wh = 0;
  SDL_GetWindowSize(win, &ww, &wh);
  if (ww == 1280 && wh == 720) {
    sdlfe::FrameShape shape{640, 480, true};
    sdlfe::SdlInput input;
    input.window = win;
    input.shape = &shape;
    sdlfe::SdlZinHost host(&input);
    int32_t cx = 1120, cy = 720;
    host.screen_to_client(&cx, &cy);
    EXPECT(cx == 640 && cy == 480);
    cx = 0, cy = 0;
    host.client_to_screen(&cx, &cy);
    EXPECT(cx == 160 && cy == 0);
    int32_t fw = 0, fh = 0;
    host.client_size(&fw, &fh);
    EXPECT(fw == 640 && fh == 480);
    EXPECT(input.open(platform::Input::kMouse));
    input.acquire(platform::Input::kMouse);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = 96, e.motion.yrel = 48;
    input.on_event(e);
    platform::MouseState ms;
    input.mouse_state(&ms);
    std::printf("window shape: 96 x 48 window points of motion are %d x %d mickeys (pillarboxed 960 wide)\n", ms.x, ms.y);
    EXPECT(ms.x == 64 && ms.y == 32);  // 640 / 960 of the motion
    input.unacquire(platform::Input::kMouse);
    input.close(platform::Input::kMouse);
    shape = sdlfe::FrameShape{1280, 720, true, 853, 480};  // a mission past mode 10: the 853x480 canvas
    cx = 1280, cy = 720;
    host.screen_to_client(&cx, &cy);
    EXPECT(cx == 853 && cy == 480);
    cx = 640, cy = 360;
    host.screen_to_client(&cx, &cy);
    EXPECT(std::abs(cx - 426) <= 1 && cy == 240);
    host.client_size(&fw, &fh);
    EXPECT(fw == 853 && fh == 480);
    shape = sdlfe::FrameShape{640, 480, true};
    shape.fit = false;  // the fix off: 640 stretched over the window, the old mapping
    cx = 1280, cy = 720;
    host.screen_to_client(&cx, &cy);
    EXPECT(cx == 640 && cy == 480);
  }
  SDL_DestroyWindow(win);
}


// The front end at a 16:9 or 4K window (resolution.md step 5): the menus run
// at mode 5, a 640x480 frame pillarboxed in the window (step 2's fit), so a
// click on each main-menu button's window point lands on that button's
// 640x480 pixel, as in a 640x480 window.
TEST(sdl_menu_clicks_land_on_the_same_buttons_at_any_window) {
  const sdlfe::FrameShape menus{640, 480, true};
  const int32_t buttons[][2] = {{60, 60}, {132, 135}, {60, 210}, {132, 285}, {60, 360}, {400, 360}, {470, 435}};
  const int32_t windows[][2] = {{640, 480}, {1920, 1080}, {3840, 2160}, {2560, 1080}};
  int hits = 0;
  for (const auto& w : windows)
    for (const auto& b : buttons) {
      float wx = 0, wy = 0, fx = 0, fy = 0;
      sdlfe::frame_to_window(b[0] + 0.5f, b[1] + 0.5f, w[0], w[1], menus, &wx, &wy);
      sdlfe::window_to_frame(wx, wy, w[0], w[1], menus, &fx, &fy);
      if (static_cast<int32_t>(fx) == b[0] && static_cast<int32_t>(fy) == b[1]) ++hits;
    }
  std::printf("menu clicks: %d of %d land on their 640x480 buttons\n", hits, 28);
  EXPECT(hits == 28);
  // At 1920x1080 the frame is 1440x1080 at x 240: CAMPAIGN's 60,60 is the window's 375,135.
  float wx = 0, wy = 0;
  sdlfe::frame_to_window(60, 60, 1920, 1080, menus, &wx, &wy);
  EXPECT(std::fabs(wx - 375) < 0.01f && std::fabs(wy - 135) < 0.01f);
}

#if !defined(_WIN32)  // posix_spawn (ours, not on Windows)
// No screen goes black unseen (after item 21's lab, resolution.md section 9):
// the program itself, one run a screen, all at once, each shot through the
// capture path the shots use (--shot). Each shot's share of lit pixels must be
// over a floor, and where a screen has a known colour (the lab's and the
// debriefing's green), that share too. About 20 s.
namespace {
struct ScreenShot {
  const char* name;
  std::vector<std::string> args;
  bool vmode13 = false;
  double lit_floor = 0.3, green_floor = 0.0;
  int width = 1280, height = 960;
};
// The share of lit pixels (any channel over 24) and of green ones (G 40 over
// R and B) in a 32-bit BMP, every 37th pixel; false when it cannot be read.
bool bmp_shares(const std::string& path, int* w, int* h, double* lit, double* green) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return false;
  auto u32 = [&](size_t at) { return static_cast<uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | d[at + 3] << 24); };
  const uint32_t off = u32(10);
  *w = static_cast<int32_t>(u32(18)), *h = std::abs(static_cast<int32_t>(u32(22)));
  if (d[28] != 32 || off >= d.size()) return false;
  size_t n = 0, l = 0, g = 0;
  for (size_t i = off; i + 3 < d.size(); i += 4 * 37) {
    const int b = d[i], gr = d[i + 1], r = d[i + 2];
    ++n;
    if (std::max({r, gr, b}) > 24) ++l;
    if (gr > r + 40 && gr > b + 40) ++g;
  }
  if (!n) return false;
  *lit = static_cast<double>(l) / n, *green = static_cast<double>(g) / n;
  return true;
}
pid_t spawn_bin(const char* bin, const std::vector<std::string>& args, const std::string& log) {
  std::vector<std::string> a = {bin};
  a.insert(a.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (std::string& s : a) argv.push_back(s.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_adddup2(&fa, 1, 2);
  pid_t pid = 0;
  const int err = posix_spawn(&pid, bin, &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  return err == 0 ? pid : -1;
}
pid_t spawn_mw3(const std::vector<std::string>& args, const std::string& log) { return spawn_bin(MW3_BIN, args, log); }
// mw3launcher, the launcher's own program (docs/launcher.md, The two programs).
pid_t spawn_launcher(const std::vector<std::string>& args, const std::string& log) {
  return spawn_bin(MW3_LAUNCHER_BIN, args, log);
}
}  // namespace

// The program confines its names (zutl::Mounts::confined): a dialog named
// "..\\secret.zrd" is refused though the file sits beside the write dir, and
// the log says so; a dialog the game has still loads.
TEST(sdl_program_names_stay_inside_the_game_folders) {
  if (!have_fonts()) return;
  const std::string root = std::string(MW3_TEST_TMP) + "/sdl_confined";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root + "/wd");
  if (std::FILE* f = std::fopen((root + "/secret.zrd").c_str(), "w")) std::fputs("(MAINMENU ())\n", f), std::fclose(f);
  // The dialog start runs until Escape, so each run is stopped once its
  // answer (the dialog bound, or not found) is in the log, or after 60 s.
  auto run = [&](const char* file, const char* name) {
    const std::string log = root + "/mw3.log";
    const pid_t pid = spawn_mw3({"--dialog", file, name, "--nosound", "--hidden", "--write-dir", root + "/wd"}, log);
    std::string out;
    for (int i = 0; pid > 0 && i < 600; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::ifstream in(log);
      out.assign(std::istreambuf_iterator<char>(in), {});
      if (out.find("not found") != std::string::npos || out.find("objects bound") != std::string::npos) break;
    }
    int status = 0;
    if (pid > 0) kill(pid, SIGKILL), waitpid(pid, &status, 0);
    return out;
  };
  const std::string out = run("..\\secret.zrd", "MAINMENU");
  EXPECT(out.find("../secret.zrd is outside the game's folders, not opened") != std::string::npos);
  EXPECT(out.find("not found") != std::string::npos);
  const std::string ok = run("dialog.zrd", "MAINMENU");
  EXPECT(ok.find("outside the game's folders") == std::string::npos && ok.find("objects bound") != std::string::npos);
}

TEST(sdl_program_screens_are_not_blank) {
  if (!have_fonts()) return;
  const std::vector<std::string> ia = {"--click-at", "60,210@40", "--tap", "0x14@70", "--tap", "0x12@74", "--tap",
                                       "0x1f@78",    "--tap",     "0x14@82", "--click-at", "470,320@100"};
  auto with = [](std::vector<std::string> a, std::initializer_list<const char*> more) {
    for (const char* m : more) a.push_back(m);
    return a;
  };
  std::vector<ScreenShot> shots = {
      {"main menu", {"--shot-frame", "300"}},
      {"pilot", {"--click-at", "60,60@40", "--shot-frame", "200"}},
      {"briefing", {"--briefing", "c1m1", "--shot-frame", "2500"}, false, 0.3, 0.3},
      {"lab chassis", {"--lab-mech", "9", "--shot-frame", "2720"}, false, 0.3, 0.2},
      {"lab criticals", {"--lab-mech", "9", "--click-at", "150,450@2700", "--shot-frame", "2900"}, false, 0.3, 0.2},
      {"lab groups", {"--lab-mech", "9", "--click-at", "265,450@2700", "--shot-frame", "2900"}, false, 0.3, 0.2},
      {"action", with(ia, {"--shot-frame", "300"})},
      // The missions' frames at 33 ms of game time each, whatever the build's speed.
      {"mission", {"--mission", "c1m1", "--frame-ms", "33", "--shot-frame", "700"}},
      {"mission mode 13", {"--mission", "c1m1", "--frame-ms", "33", "--shot-frame", "700"}, true, 0.3, 0.0, 1920, 1080},
      {"pause", {"--mission", "c1m1", "--frame-ms", "33", "--tap", "0x01@750", "--shot-frame", "800"}},
      {"debriefing", {"--briefing", "c1m1", "--frame-ms", "33", "--click-at", "556,435@2600", "--win", "60", "--shot-frame", "4200"},
       false, 0.3, 0.3},
  };
  const std::string root = std::string(MW3_TEST_TMP) + "/sdl_screens";
  std::filesystem::remove_all(root);
  std::vector<pid_t> pids;
  for (size_t i = 0; i < shots.size(); ++i) {
    const std::string dir = root + "/" + std::to_string(i);
    std::filesystem::create_directories(dir);
    if (shots[i].vmode13)  // the mission's screen mode 13 (1920x1080), as the graphics options save it
      if (std::FILE* f = std::fopen((dir + "/settings.reg").c_str(), "w")) {
        std::fputs("Windows Registry Editor Version 5.00\n\n[HKEY_CURRENT_USER\\SOFTWARE\\MicroProse\\MechWarrior 3\\1.0]\n"
                   "\"InGameVMode\"=dword:0000000d\n", f);
        std::fclose(f);
      }
    std::vector<std::string> a = shots[i].args;
    for (const char* x : {"--shot", "", "--nosound", "--hidden", "--write-dir", ""}) a.push_back(x);
    a[a.size() - 5] = dir + "/shot.bmp";
    a.back() = dir;
    pids.push_back(spawn_mw3(a, dir + "/mw3.log"));
  }
  const auto start = std::chrono::steady_clock::now();
  for (pid_t& pid : pids) {
    if (pid <= 0) continue;
    int status = 0;
    while (waitpid(pid, &status, WNOHANG) != pid) {
      if (std::chrono::steady_clock::now() - start > std::chrono::seconds(180)) {
        kill(pid, SIGKILL), waitpid(pid, &status, 0);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  for (size_t i = 0; i < shots.size(); ++i) {
    const ScreenShot& s = shots[i];
    int w = 0, h = 0;
    double lit = 0, green = 0;
    const bool read = pids[i] > 0 && bmp_shares(root + "/" + std::to_string(i) + "/shot.bmp", &w, &h, &lit, &green);
    std::printf("screen %-16s %dx%d lit %.3f green %.3f\n", s.name, w, h, lit, green);
    EXPECT(read && w == s.width && h == s.height);
    EXPECT(lit > s.lit_floor);
    EXPECT(green >= s.green_floor);
  }
  std::printf("screens: %zu shot in %.0f s\n", shots.size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

// The campaign as a player plays it (--campaign-chain, campaign_chain.h;
// MW3_CAMPAIGN=1, about 2 minutes). A new pilot through Op 1 then, after
// the app's quit, the same write dir resumed to the end; and a new pilot
// through all 20 missions in one session. Every briefing the two reach must
// read the same (the store, the salvage, the slot, the lance, the bases);
// the resume starts where the first half ended; each run's census is 0
// outside the known table; both end through the win movie, the credits
// pressed once by the main menu and back to a main menu that works; every
// briefing and debriefing shot is lit, and green. A third run wins Op 1 by
// kills, its salvage followed into the store and the lab (below).
TEST(sdl_program_campaign_chain) {
  if (!have_fonts() || !std::getenv("MW3_CAMPAIGN")) return;
  const std::string root = std::string(MW3_TEST_TMP) + "/sdl_campaign_chain";
  std::filesystem::remove_all(root);
  const std::string split = root + "/split", single = root + "/single", kills = root + "/kills", retry = root + "/retry";
  std::filesystem::create_directories(retry);
  std::filesystem::create_directories(split);
  std::filesystem::create_directories(single);
  std::filesystem::create_directories(kills);
  // Every run on a 33 ms frame clock with a fixed seed (--seed, ours): each
  // mission's start reseeds rand (0x00501ab0, 0x00508fc0), so a mission plays
  // the same in the split and the whole run.
  auto args = [](const std::string& chain, const std::string& dir) {
    return std::vector<std::string>{"--campaign-chain", chain, "--verbose", "--nosound", "--hidden", "--write-dir", dir,
                                    "--frame-ms", "33", "--seed", "1999"};
  };
  auto wait = [](pid_t pid) {
    int status = 0;
    const auto start = std::chrono::steady_clock::now();
    while (pid > 0 && waitpid(pid, &status, WNOHANG) != pid) {
      if (std::chrono::steady_clock::now() - start > std::chrono::seconds(900)) {
        kill(pid, SIGKILL), waitpid(pid, &status, 0);
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  };
  const auto start = std::chrono::steady_clock::now();
  const pid_t whole = spawn_mw3(args("new:20", single), single + "/run.log");
  // c1m3's bases drive about 2.5 km to Charlie; unseeded, their wander made
  // its win frame vary from 7k to over 40k.
  const pid_t by_kills = spawn_mw3(args("kills:4", kills), kills + "/run.log");
  // Op 1 with each mission failed once first (the pause's REPLAY), then won.
  const pid_t by_retry = spawn_mw3(args("retry:4", retry), retry + "/run.log");
  const bool first_ok = wait(spawn_mw3(args("new:4", split), split + "/run1.log"));
  const bool second_ok = wait(spawn_mw3(args("resume:16", split), split + "/run2.log"));
  const bool whole_ok = wait(whole);
  const bool kills_ok = wait(by_kills);
  const bool retry_ok = wait(by_retry);
  EXPECT(first_ok && second_ok && whole_ok && kills_ok && retry_ok);
  auto read = [](const std::string& path) {
    std::string t;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) t.append(buf, n);
      std::fclose(f);
    }
    return t;
  };
  // "mw3: chain briefing <mission>: <state>, wins N": by mission, in order.
  auto briefings = [](const std::string& log) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t at = 0;
    while ((at = log.find("mw3: chain briefing ", at)) != std::string::npos) {
      const size_t end = log.find('\n', at), colon = log.find(": ", at + 20);
      const size_t wins = log.rfind(", wins ", end);
      if (end == std::string::npos || colon == std::string::npos || wins == std::string::npos || colon > end) break;
      out.push_back({log.substr(at + 20, colon - at - 20), log.substr(colon + 2, wins - colon - 2)});
      at = end;
    }
    return out;
  };
  const std::string l1 = read(split + "/run1.log"), l2 = read(split + "/run2.log"), lw = read(single + "/run.log");
  const auto b1 = briefings(l1), b2 = briefings(l2), bw = briefings(lw);
  std::printf("campaign chain: %zu + %zu briefings split, %zu in one session, in %.0f s\n", b1.size(), b2.size(),
              bw.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  EXPECT(b1.size() == 5 && b2.size() == 16 && bw.size() == 20);  // the resume's first is the first run's last
  // The resume starts at the mission and with the game the first run ended on.
  EXPECT(!b1.empty() && !b2.empty() && b1.back() == b2.front());
  if (!b2.empty()) std::printf("  resumed at %s: %s\n", b2.front().first.c_str(), b2.front().second.c_str());
  // Every mission reads the same split and whole, the stores' unit totals
  // included: seeded on the frame clock, a mission spends the same ammunition
  // either way (on the real clock 10 rounds of weapon 17's in c4m2 could go
  // in one run and not the other).
  std::vector<std::pair<std::string, std::string>> joined(b1.begin(), b1.end());
  if (!b2.empty()) joined.insert(joined.end(), b2.begin() + 1, b2.end());
  int same = 0;
  for (size_t i = 0; i < joined.size() && i < bw.size(); ++i) {
    if (joined[i] == bw[i]) ++same;
    else std::printf("  differs at %s:\n    split %s\n    whole %s\n", bw[i].first.c_str(), joined[i].second.c_str(), bw[i].second.c_str());
  }
  EXPECT(same == 20);
  for (const std::string* l : {&l1, &l2, &lw}) EXPECT(l->find("mw3: 0 original functions reached that are not ported") != std::string::npos);
  EXPECT(l2.find("mw3: chain credits after 16 wins (entered 1)") != std::string::npos);
  EXPECT(lw.find("mw3: chain credits after 20 wins (entered 1)") != std::string::npos);
  // The campaign's end, state by state as the app entered them after c4bm6's
  // CONTINUE: the win movie (none is played yet, so it lasts its one
  // update), the main menu, whose enter presses CREDITS by the flag the
  // movie set, the credits once; their CANCEL back to the main menu, whose
  // NEW CAMPAIGN reaches the pilot screen.
  auto end_states = [](const std::string& log) {
    std::string out;
    size_t at = log.rfind("CONTINUE (wins ");
    while (at != std::string::npos && (at = log.find("mw3: chain state ", at)) != std::string::npos) {
      at += 17;
      out += (out.empty() ? "" : ", ") + log.substr(at, log.find(" (frame", at) - at);
    }
    return out;
  };
  const std::string end_seq = "Win Movie State, Main State, Credits State, Main State, Campaign State, Pilot State";
  for (const std::string* l : {&l2, &lw}) {
    std::printf("  the end: %s\n", end_states(*l).c_str());
    EXPECT(end_states(*l) == end_seq);
    EXPECT(l->find("mw3: chain pilot screen again after the credits (credits entered 1)") != std::string::npos);
  }
  // Op 1 won by kills (campaign.md section 14): every enemy shot through
  // zWeapon's impact until it dies, then the places the objectives' valves
  // name, the MFB command (c1m3's bases to Charlie), Exit Mission once won. Each debriefing's salvage is the mission's
  // store, traced to the kills (each mech's and vehicle's its own weapons),
  // the objectives and the win's lance; SALVAGE moves its weapons into the
  // store; the lab fits one, and the next mission's mech carries it.
  const std::string lk = read(kills + "/run.log");
  auto core_frame_of = [](const std::string& log, const char* what) {
    const size_t at = log.find(what), f = at == std::string::npos ? at : log.find("core frame ", at);
    return f == std::string::npos ? std::string("?") : log.substr(f + 11, log.find(')', f) - f - 11);
  };
  auto count = [](const std::string& log, const char* what) {
    int n = 0;
    for (size_t at = 0; (at = log.find(what, at)) != std::string::npos; ++at) ++n;
    return n;
  };
  const int won = count(lk, "mw3: chain won op 1 mission "), by_win = count(lk, "not won by its kills");
  const int matches = count(lk, "the win's lance"), matched = count(lk, "}: matches");
  const int moved = count(lk, "mw3: chain salvage weapons moved"), moved_ok = count(lk, "}: moved\n");
  const int fitted = count(lk, "mw3: chain lab: salvaged weapon"), carried = count(lk, "): fitted\n");
  std::printf("  by kills: %d won by their objectives, %d as --win (c1m3 at core frame %s); %d of %d debriefings' salvage matched, %d of %d "
              "SALVAGE moved, %d fitted, %d carried; %d kills (%d not their weapons)\n",
              won, by_win, core_frame_of(lk, "mw3: chain won op 1 mission 3 by ").c_str(), matched, matches, moved_ok, moved, fitted, carried, count(lk, "mw3: chain killed "),
              count(lk, "NOT its weapons"));
  EXPECT(won == 4 && by_win == 0);
  EXPECT(lk.find("mw3: chain won op 1 mission 3 by ") != std::string::npos);
  EXPECT(matches == 4 && matched == 4);
  EXPECT(moved == 3 && moved_ok == 3 && fitted == 3 && carried == 3);
  EXPECT(count(lk, "NOT its weapons") == 0 && count(lk, "mw3: chain killed mech ") >= 10);
  // The vehicles carry their weapons (0x004b4660, weapons.md): an armed one's
  // salvage is all of them or, on the coin, none; the trucks carry nothing.
  int veh_weapons = 0, veh_coin = 0, veh_unarmed = 0;
  for (size_t at = 0; (at = lk.find("mw3: chain killed vehicle ", at)) != std::string::npos; ++at) {
    const std::string l = lk.substr(at, lk.find('\n', at) - at);
    auto ends = [&](const std::string& e) { return l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0; };
    veh_weapons += ends(", its weapons"), veh_coin += ends(", none (the coin)"), veh_unarmed += ends(", unarmed");
  }
  std::printf("  vehicles killed: %d salvaged their weapons, %d none on the coin, %d unarmed\n", veh_weapons, veh_coin, veh_unarmed);
  EXPECT(veh_weapons >= 5 && veh_weapons + veh_coin > veh_unarmed);
  EXPECT(lk.find("mw3: chain done at op 2 mission 1 after 4 wins") != std::string::npos);
  // The debriefing's rows are the mission's objectives, scored (campaign.md section 3).
  EXPECT(lk.find("rows after op 1 mission 1: Primary Objectives | Take Com. Center Offline | 2500 | ") != std::string::npos);
  EXPECT(lk.find("Secondary Objectives | Destroy Smoke Jaguar Forces | ") != std::string::npos);
  EXPECT(lk.find("mw3: 0 original functions reached that are not ported") != std::string::npos);
  // The retries (campaign.md section 14, The retry): a lost mission is
  // briefed again (0x0047ac20 reads the loss), its animations reset
  // (0x0050ad60), and each is then won by its objectives; c1m3's buildings
  // stood again for it.
  const std::string lr = read(retry + "/run.log");
  const int failed = count(lr, "on purpose (");
  const int retried_won = count(lr, "mw3: chain won op 1 mission "), retried_by_win = count(lr, "not won by its kills");
  std::printf("  retries: %d failed on purpose, then %d won by their objectives, %d as --win (c1m3 at core frame %s)\n",
              failed, retried_won, retried_by_win, core_frame_of(lr, "mw3: chain won op 1 mission 3 by ").c_str());
  EXPECT(failed == 4 && retried_won == 4 && retried_by_win == 0);
  EXPECT(lr.find("mw3: chain done at op 2 mission 1 after 4 wins") != std::string::npos);
  EXPECT(lr.find("mw3: 0 original functions reached that are not ported") != std::string::npos);
  int shots = 0, lit_ok = 0;
  for (const std::string& dir : {split, single, kills})
    for (const auto& f : std::filesystem::directory_iterator(dir)) {
      const std::string name = f.path().filename().string();
      if (name.rfind("chain_", 0) != 0) continue;
      int w = 0, h = 0;
      double lit = 0, green = 0;
      ++shots;
      // A briefing is its map: some are dark (c1m4's water) or grey-green
      // (c3m6's); the debriefing is the green panels.
      const bool debrief = name.find("debriefing") != std::string::npos;
      if (bmp_shares(f.path().string(), &w, &h, &lit, &green) && lit > (debrief ? 0.3 : 0.15) &&
          (!debrief || green > 0.3))
        ++lit_ok;
      else std::printf("  shot %s: lit %.3f green %.3f\n", name.c_str(), lit, green);
    }
  std::printf("  %d of %d briefing and debriefing shots lit\n", lit_ok, shots);
  EXPECT(shots == 90 && lit_ok == shots);  // 81, and the kills run's 5 briefings and 4 debriefings
}
// The whole campaign by its own objectives (MW3_CAMPAIGN_ALL=1, about 5
// minutes): a new pilot's kills chain through all 20 missions, seeded on the
// 33 ms clock. Each is won by its objectives (campaign.md section 14: the
// shooting, the reach_area walks, the valves' regions, the MFB command from
// a valve's net link), never by the chain's --win fallback; the census 0;
// the credits at the end. The frames are printed, not pinned: the check to
// run after a change to the AI, the objectives, the scripts or the nets
// (program.md).
TEST(sdl_program_campaign_by_objectives) {
  if (!have_fonts() || !std::getenv("MW3_CAMPAIGN_ALL")) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_campaign_objectives";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto start = std::chrono::steady_clock::now();
  const pid_t pid = spawn_mw3({"--campaign-chain", "kills:20", "--verbose", "--nosound", "--hidden", "--write-dir", dir,
                               "--frame-ms", "33", "--seed", "1999"},
                              dir + "/run.log");
  int status = 0;
  bool done = pid > 0;
  while (done && waitpid(pid, &status, WNOHANG) != pid) {
    // a mission that misses its objectives runs to the chain's 40000-frame guard, about 5 minutes more each
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3600)) {
      kill(pid, SIGKILL), waitpid(pid, &status, 0);
      done = false;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  EXPECT(done && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  std::string log;
  if (std::FILE* f = std::fopen((dir + "/run.log").c_str(), "r")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) log.append(buf, n);
    std::fclose(f);
  }
  // "mw3: chain won op O mission M by K kills (H hits, core frame F)": by mission, in order.
  int won = 0;
  std::string frames;
  for (size_t at = 0; (at = log.find("mw3: chain won op ", at)) != std::string::npos; ++at) {
    const size_t by = log.find(" by ", at), f = log.find("core frame ", at), end = log.find('\n', at);
    if (by == std::string::npos || f == std::string::npos || f > end) break;
    ++won;
    frames += "  " + log.substr(at + 15, by - at - 15) + " at core frame " + log.substr(f + 11, log.find(')', f) - f - 11) + "\n";
  }
  int by_win = 0;
  for (size_t at = 0; (at = log.find("not won by its kills", at)) != std::string::npos; ++at) {
    const size_t b = log.rfind("mw3: chain ", at);
    std::printf("  NOT by its objectives: %s\n", log.substr(b + 11, at - b - 11).c_str());
    ++by_win;
  }
  std::printf("campaign by objectives: %d of 20 won, %d as --win, in %.0f s\n%s", won, by_win,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), frames.c_str());
  EXPECT(won == 20 && by_win == 0);
  EXPECT(log.find("mw3: chain credits after 20 wins (entered 1)") != std::string::npos);
  EXPECT(log.find("mw3: 0 original functions reached that are not ported") != std::string::npos);
}
// Instant Action played through (MW3_CAMPAIGN=1, about 2 minutes;
// --action-chain, modes_chain.h, campaign.md section 15): a new pilot from
// the main menu's INSTANT ACTION, each of the Action screen's 4 worlds and 5
// scenarios with a line-up, ACCEPT, LAUNCH. A round is won (the enemies shot
// until the objectives end it; Battle Waves and Progression two waves
// cleared, then the pause's QUIT) or lost (the pause's REPLAY); CONTINUE
// plays the next round the other way, ABORT tallies it through the IA Dummy
// State back to the Action screen. Every case tallied as the exe tallies
// (Attack's and Defend's wins and losses), the census 0.
TEST(sdl_program_action_chain) {
  if (!have_fonts() || !std::getenv("MW3_CAMPAIGN")) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_action_chain";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto start = std::chrono::steady_clock::now();
  const pid_t pid = spawn_mw3({"--action-chain", "all", "--verbose", "--nosound", "--hidden", "--write-dir", dir,
                               "--frame-ms", "33", "--seed", "1999"},
                              dir + "/run.log");
  int status = 0;
  bool done = pid > 0;
  while (done && waitpid(pid, &status, WNOHANG) != pid) {
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(900)) {
      kill(pid, SIGKILL), waitpid(pid, &status, 0);
      done = false;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  EXPECT(done && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  std::string log;
  if (std::FILE* f = std::fopen((dir + "/run.log").c_str(), "r")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) log.append(buf, n);
    std::fclose(f);
  }
  auto count = [&](const char* what) {
    int n = 0;
    for (size_t at = 0; (at = log.find(what, at)) != std::string::npos; ++at) ++n;
    return n;
  };
  const int won = count("won by its objectives at core frame"), cleared = count("cleared at core frame");
  const int lost = count(" lost at core frame"), not_won = count("NOT won by its objectives");
  std::printf("action chain: %d rounds won by their objectives, %d by two waves cleared, %d lost, %d not won, in %.0f s\n",
              won, cleared, lost, not_won, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  for (size_t at = 0; (at = log.find("mw3: modes case ", at)) != std::string::npos; ++at)
    if (log.compare(log.find(':', at + 16), 7, ": world") != 0) std::printf("  %s\n", log.substr(at + 11, log.find('\n', at) - at - 11).c_str());
  EXPECT(won == 12 && cleared == 8 && lost == 20 && not_won == 0);  // 12 cases end on their objectives, 8 on waves
  // A round of waves left by QUIT (not ended, so a round less): 1000 a wave
  // and a hundred for each round left at each, by the wave it stopped on.
  int wave_scores = 0, wave_scores_ok = 0;
  for (size_t at = 0; (at = log.find(" cleared at core frame", at)) != std::string::npos; ++at) {
    const size_t start = log.rfind("waves ", at), rows = log.find("debriefing rows", at), eol = log.find('\n', rows);
    int first = 0, defeated = 0, total = -1;
    std::sscanf(log.c_str() + start, "waves %d", &first);
    const std::string line = log.substr(rows, eol - rows);
    if (const size_t m = line.find("Mechs Defeated | "); m != std::string::npos) defeated = std::atoi(line.c_str() + m + 17);
    if (const size_t m = line.find("Mission Total | "); m != std::string::npos) total = std::atoi(line.c_str() + m + 16);
    ++wave_scores;
    wave_scores_ok += total == 2000 + (first + 1) * 100 + first * 100 + defeated;
  }
  std::printf("  %d rounds of waves, %d scored by their waves\n", wave_scores, wave_scores_ok);
  EXPECT(wave_scores == 8 && wave_scores_ok == 8);
  EXPECT(log.find("mw3: modes done: 20 cases, 20 tallied as expected, 0 not") != std::string::npos);
  EXPECT(log.find("mw3: 0 original functions reached that are not ported") != std::string::npos);
}
// Training played through (MW3_CAMPAIGN=1, about a minute; --action-chain
// training, modes_chain.h, campaign.md section 16): a new pilot from the main
// menu's TRAINING, t1's four missions as the pilot screen offers them, each
// lesson's primary objectives met as a player meets them (the commands'
// keys with their modifiers, the throttle held, facing and aiming, the
// weapons, the valves' regions and the AI records' triggers). All four
// passed by their lessons, none stuck, the program's exit clean (m4's
// pocket mode left at the mission's end crashed it), the census 0.
TEST(sdl_program_training_chain) {
  if (!have_fonts() || !std::getenv("MW3_CAMPAIGN")) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_training_chain";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto start = std::chrono::steady_clock::now();
  const pid_t pid = spawn_mw3({"--action-chain", "training", "--verbose", "--nosound", "--hidden", "--write-dir", dir,
                               "--frame-ms", "33", "--seed", "1999"},
                              dir + "/run.log");
  int status = 0;
  bool done = pid > 0;
  while (done && waitpid(pid, &status, WNOHANG) != pid) {
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(900)) {
      kill(pid, SIGKILL), waitpid(pid, &status, 0);
      done = false;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  const bool clean = done && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  std::string log;
  if (std::FILE* f = std::fopen((dir + "/run.log").c_str(), "r")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) log.append(buf, n);
    std::fclose(f);
  }
  auto count = [&](const char* what) {
    int n = 0;
    for (size_t at = 0; (at = log.find(what, at)) != std::string::npos; ++at) ++n;
    return n;
  };
  const int won = count("won by its lessons"), lessons = count(" done at core frame"), stuck = count("NOT met after");
  std::printf("training chain: %d missions won by their lessons, %d lessons met, %d stuck, exit %s, in %.0f s\n", won,
              lessons, stuck, clean ? "clean" : "NOT clean",
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  EXPECT(clean);
  EXPECT(won == 4 && stuck == 0 && lessons >= 40);
  EXPECT(log.find("mw3: modes training done: 4 missions passed by their lessons") != std::string::npos);
  EXPECT(log.find("mw3: 0 original functions reached that are not ported") != std::string::npos);
}
#endif

#if !defined(_WIN32)  // posix_spawn (ours, not on Windows)
// The movies through the Indeo helper's pipe (ours; fmv.md, The Indeo
// helper), its stand-in answering from one imported movie: the client's
// calls give what the imported stream gives in-process, and mw3 plays the
// movie through the pipe as through the files (the same frames drawn, the
// same picture); a helper that does not start falls back to the files.
TEST(sdl_movies_through_the_indeo_helper_pipe) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(MW3_IMPORT_BIN, ec) || !std::filesystem::exists(install + "/video/C1M2.AVI", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_indeo";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  auto run = [&](std::vector<std::string> args, const std::string& name) {
    std::vector<char*> argv;
    for (std::string& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);
    const std::string log = dir + "/" + name + ".log";
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    int status = -1;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) == 0) waitpid(pid, &status, 0);
    posix_spawn_file_actions_destroy(&fa);
    std::string text;
    if (std::FILE* f = std::fopen(log.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  run({MW3_IMPORT_BIN, "--install", install, "--write-dir", dir, "--only", "c1m2"}, "import");
  if (!std::filesystem::exists(dir + "/video/c1m2.fmv", ec)) return;  // no FFmpeg in this importer
  setenv("MW3_FAKE_INDEO_DIR", (dir + "/video").c_str(), 1);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "MW3_FAKE_INDEO_DIR", (dir + "/video").c_str(), true);

  // The client against the stream it stands for.
  {
    sdlfe::SdlVfwHelper h(MW3_FAKE_INDEO, "Ir50_32.dll");
    EXPECT(h.ready());
    std::printf("  helper: %s\n", h.hello().c_str());
    auto local = mech3::MovieStream::open(dir + "/video/c1m2", 0x73646976);
    auto local_a = mech3::MovieStream::open(dir + "/video/c1m2", 0x73647561);
    uint32_t v = 0, s = 0;
    EXPECT(h.open(install + "/video/C1M2.AVI", 0x73646976, &v) == 0 && v != 0);
    EXPECT(h.open(install + "/video/C1M2.AVI", 0x73647561, &s) == 0 && s != 0 && s != v);
    if (local && local_a && v && s) {
      std::vector<uint8_t> f;
      EXPECT(h.format(v, &f) == 0 && f == local->info().video_format);
      zfmv::AviStreamInfo info;
      EXPECT(h.info(v, &info) == 0 && std::memcmp(&info, &local->info().video, sizeof info) == 0);
      EXPECT(h.length(v) == static_cast<int32_t>(local->info().video.length) && h.length(v) == 475);
      EXPECT(h.find_sample(v, 100, 0x14) == local->find_sample(100, 0x14));
      std::vector<uint8_t> sound(22054), sound_local(22054);
      EXPECT(h.read(s, 1000, 22054, sound.data(), 22054) == 0);
      local_a->read(1000, 22054, sound_local.data(), 22054);
      EXPECT(sound == sound_local);
      // Frame 30 as the movie player asks it: 640x480, 16 bits, 565 masks, top down.
      uint8_t bih[52] = {};
      const uint32_t words[] = {40, 640, static_cast<uint32_t>(-480)};
      std::memcpy(bih, words, 12);
      bih[12] = 1, bih[14] = 16, bih[16] = 3;
      const uint32_t masks[3] = {0xf800, 0x07e0, 0x001f};
      std::memcpy(bih + 40, masks, 12);
      std::vector<uint16_t> img(640 * 480), img_local(640 * 480);
      EXPECT(h.read(v, 30, 1, nullptr, 0) == 0);
      EXPECT(h.decompress(v, 0, bih, sizeof bih, img.data(), img.size() * 2) == 0);
      local->read(30, 1, nullptr, 0);
      local->decompress(img_local.data(), 640, 480, 1280, masks[0], masks[1], masks[2]);
      EXPECT(img == img_local);
      size_t lit = 0;
      for (uint16_t p : img) lit += p != 0;
      EXPECT(lit > img.size() / 4);
      h.release(v), h.release(s);
    }
  }
  // mw3 plays the movie through the pipe as through the files.
  auto play = [&](const std::string& helper, const std::string& name) {
    std::vector<std::string> a = {MW3_BIN, "--install", install, "--write-dir", dir, "--movie", "c1m2", "--nosound", "--hidden"};
    if (helper.empty()) a.push_back("--no-vfw-helper");
    else a.push_back("--vfw-helper"), a.push_back(helper);
    std::vector<std::string> b = a;
    for (const std::string& x : {std::string("--shot"), dir + "/" + name + ".bmp", std::string("--shot-frame"), std::string("200")})
      a.push_back(x);
    const std::string shot = run(a, name + "-shot");
    return std::make_pair(shot, run(b, name));
  };
  auto movie_line = [](const std::string& log) {
    const size_t at = log.find("mw3: movie c1m2:");
    return at == std::string::npos ? std::string() : log.substr(at, log.find('\n', at) - at);
  };
  auto bytes = [](const std::string& path) {
    std::vector<uint8_t> d;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
      uint8_t buf[1 << 16];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
      std::fclose(f);
    }
    return d;
  };
  const auto files = play("", "files");
  const auto pipe = play(MW3_FAKE_INDEO, "pipe");
  const auto broken = play(dir + "/no-such-helper", "broken");
  std::printf("  files:  %s\n  pipe:   %s\n  broken: %s\n", movie_line(files.second).c_str(), movie_line(pipe.second).c_str(),
              movie_line(broken.second).c_str());
  EXPECT(pipe.second.find("mw3: movies through the Indeo helper (the test helper on") != std::string::npos);
  EXPECT(broken.second.find("mw3: movies not through the Indeo helper") != std::string::npos);
  EXPECT(!movie_line(files.second).empty() && movie_line(files.second).find("475 drawn") != std::string::npos);
  EXPECT(movie_line(pipe.second) == movie_line(files.second) && movie_line(broken.second) == movie_line(files.second));
  const std::vector<uint8_t> shot_files = bytes(dir + "/files.bmp"), shot_pipe = bytes(dir + "/pipe.bmp");
  EXPECT(shot_files.size() > 100000 && shot_files == shot_pipe);
}

// The intro's picture follows its sound without a beat (fmv.md section 5):
// with the sound's play cursor moving in WASAPI's 10 ms periods (SDL's dummy
// driver at 441 frames: no sound out, the callback on its schedule), the
// exe's way, asking for the sound's frame only when the movie's own 15 fps
// clock changes, repeats or skips a frame every few; with the bug fixes each
// frame shows once (one repeat at the start, the first two steps).
TEST(sdl_intro_follows_its_sound_without_a_beat) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(MW3_IMPORT_BIN, ec) || !std::filesystem::exists(install + "/video/INTRO.AVI", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_intro_beat";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  auto run = [&](std::vector<std::string> args, const std::string& name) {
    std::vector<char*> argv;
    for (std::string& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);
    const std::string log = dir + "/" + name + ".log";
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    int status = -1;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) == 0) waitpid(pid, &status, 0);
    posix_spawn_file_actions_destroy(&fa);
    std::string text;
    if (std::FILE* f = std::fopen(log.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  run({MW3_IMPORT_BIN, "--install", install, "--write-dir", dir, "--only", "intro"}, "import");
  if (!std::filesystem::exists(dir + "/video/intro.fmv", ec)) return;  // no FFmpeg in this importer
  const char* const kEnv[][2] = {{"SDL_AUDIO_DRIVER", "dummy"}, {"SDL_AUDIO_DEVICE_SAMPLE_FRAMES", "441"}, {"MW3_MOVIE_TRACE", "1"}};
  for (const auto& e : kEnv) setenv(e[0], e[1], 1);
  struct Beat {
    int steps = 0, repeats = 0, skipped = 0;
    bool device = false;
  };
  auto play = [&](bool original) {
    std::vector<std::string> a = {MW3_BIN, "--install", install, "--write-dir", dir, "--no-launcher", "--frames", "200", "--hidden"};
    if (original) a.push_back("--original");
    const std::string log = run(a, original ? "original" : "fixed");
    Beat b;
    b.device = log.find("sound device 44100 Hz, 2 channels, 441 sample frames") != std::string::npos;
    int last = -1;
    for (size_t at = 0; (at = log.find("clock frame ", at)) != std::string::npos; ++at) {
      const size_t s = log.find("shown ", at);
      if (s == std::string::npos) break;
      const int shown = std::atoi(log.c_str() + s + 6);
      if (b.steps++ > 0) {
        if (shown == last) ++b.repeats;
        if (shown > last + 1) b.skipped += shown - last - 1;
      }
      last = shown;
    }
    return b;
  };
  const Beat fixed = play(false), original = play(true);
  for (const auto& e : kEnv) unsetenv(e[0]);
  std::printf("  the intro, 10 ms sound periods: fixed %d steps, %d repeated, %d skipped; the exe's %d steps, %d repeated, %d skipped\n",
              fixed.steps, fixed.repeats, fixed.skipped, original.steps, original.repeats, original.skipped);
  EXPECT(fixed.device && original.device);
  EXPECT(fixed.steps >= 150 && fixed.repeats <= 1 && fixed.skipped == 0);
  EXPECT(original.steps >= 150 && original.repeats + original.skipped >= 10);
}

// Two mw3 processes in a session (docs/multiplayer-port.md, piece 4): the
// host on 127.0.0.1 and a port the system picks (parallel runs never meet,
// loopback: no firewall prompt), its port read from its log; a joiner says
// a line. Both see each other, the joiner learns the host from its 0xd, and
// the host's leaving makes the joiner the host.
TEST(sdl_two_processes_host_and_join) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_two";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/host", ec);
  std::filesystem::create_directories(dir + "/join", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<char*> argv;
    for (std::string& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind", "127.0.0.1"};
  std::vector<std::string> h = {MW3_BIN, "--write-dir", dir + "/host", "--frames", "900", "--host", "--net-port", "0", "--net-name", "Ann"};
  h.insert(h.end(), common.begin(), common.end());
  const pid_t host = spawn(h, dir + "/host.log");
  EXPECT(host > 0);
  std::string port;
  for (int i = 0; i < 200 && port.empty(); ++i) {
    const std::string log = read(dir + "/host.log");
    const size_t at = log.find("hosting on port ");
    if (at != std::string::npos) port = log.substr(at + 16, log.find(' ', at + 16) - at - 16);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT(!port.empty());
  std::vector<std::string> j = {MW3_BIN, "--write-dir", dir + "/join", "--frames", "300", "--join", "127.0.0.1:" + port,
                                "--net-name", "Bob", "--net-say", "hello from Bob@60"};
  j.insert(j.end(), common.begin(), common.end());
  const pid_t joiner = spawn(j, dir + "/join.log");
  int status = 0;
  if (joiner > 0) waitpid(joiner, &status, 0);
  if (host > 0) waitpid(host, &status, 0);
  const std::string hl = read(dir + "/host.log"), jl = read(dir + "/join.log");
  auto has = [](const std::string& s, const char* what) { return s.find(what) != std::string::npos; };
  std::printf("  host: %s, joiner: %s\n", has(hl, "player 2 (Bob) joined") ? "saw Bob" : "no Bob",
              has(jl, "as player 2 (Bob)") ? "player 2" : "not joined");
  // The joiner's own line comes as its player is made, before or after the
  // host's 0xd: its "is the host" line says it either way.
  const bool all = has(hl, "as player 1 (Ann), the host 1") && has(jl, "as player 2 (Bob)") &&
                   has(jl, "player 1 (Ann) is the host") && has(hl, "player 2 (Bob) joined") &&
                   has(hl, "player 2 (Bob) says \"hello from Bob\"");
  if (!all)  // what each saw, for the failure
    for (const std::string* l : {&hl, &jl})
      for (size_t at = 0; (at = l->find("mw3: network", at)) != std::string::npos; ++at)
        std::printf("  %s: %s\n", l == &hl ? "host" : "join", l->substr(at, l->find('\n', at) - at).c_str());
  EXPECT(all);
  // Whoever leaves first, the other sees it; the host gone, Bob hosts (the
  // lowest id left, DPSYS_HOST, his 0xd to himself).
  EXPECT(has(hl, "player 2 (Bob) left") || (has(jl, "player 1 (Ann) left") && has(jl, "player 2 (Bob) is the host")));
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// The connection screen (multiplayer-port.md, piece 9b), two mw3 through the
// menus on loopback: each goes MULTIPLAYER, makes a pilot, and lands on the
// connection screen, connected and searching (ours: the LAN searched at
// once). A clicks HOST; B's search finds A's game in its SESSIONS (the LAN's
// probe to A's discovery port: no broadcast on loopback), B chooses it and
// clicks JOIN. Both reach the lobby's page.
TEST(sdl_two_processes_host_and_join_from_the_screens) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_screens";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> v = {MW3_BIN};
    v.insert(v.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : v) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  // Ports of their own (parallel runs and a real game here never meet).
  const int base = 30000 + static_cast<int>(getpid() % 2000) * 3;
  const std::string game = std::to_string(base), lan = std::to_string(base + 1);
  // MULTIPLAYER, a new pilot's name typed, ACCEPT (as the Instant Action check's).
  auto menus = [](std::vector<std::string> v, std::initializer_list<const char*> name) {
    std::vector<std::string> m = {"--click-at", "132,285@40"};
    int at = 70;
    for (const char* k : name) m.insert(m.end(), {"--tap", std::string(k) + "@" + std::to_string(at)}), at += 4;
    m.insert(m.end(), {"--click-at", "470,320@100"});
    v.insert(v.end(), m.begin(), m.end());
    return v;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--frame-ms", "33"};
  std::vector<std::string> va = menus({"--write-dir", dir + "/a", "--net-port", game, "--net-discovery", lan,
                                       "--click-at", "421,406@180", "--frames", "100000"}, {"0x14", "0x12", "0x1f", "0x14"});
  va.insert(va.end(), common.begin(), common.end());
  std::vector<std::string> vb = menus({"--write-dir", dir + "/b", "--net-port", "0", "--net-lan", "127.0.0.1:" + lan,
                                       "--click-at", "100,143@420", "--click-at", "240,406@450", "--frames", "600"},
                                      {"0x30", "0x12", "0x1f", "0x14"});
  vb.insert(vb.end(), common.begin(), common.end());
  const pid_t a = spawn(va, dir + "/a.log");
  for (int i = 0; i < 400 && read(dir + "/a.log").find("multiplayer page 2") == std::string::npos; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t b = spawn(vb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) kill(a, SIGTERM), waitpid(a, &st, 0);  // A hosts until B is done
  const std::string la = read(dir + "/a.log"), lb = read(dir + "/b.log");
  auto has = [](const std::string& t, const char* w) { return t.find(w) != std::string::npos; };
  std::printf("  A: %s; B: %s\n", has(la, "multiplayer page 2") ? "hosting, in the lobby" : "not hosting",
              has(lb, "multiplayer page 2") ? "joined, in the lobby" : "not joined");
  EXPECT(has(la, "multiplayer page 1") && has(la, "multiplayer page 2"));
  EXPECT(has(lb, "multiplayer page 1") && has(lb, "multiplayer page 2"));
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// Two mw3 from the menus through the lobby into one game (the lead's check
// after piece 11c): A hosts, B finds and joins it; once both are in the
// lobby (--click-players: counted from the second player's arrival) A locks
// and readies, B readies, A starts. Both load the lobby's world (c1) and
// reach play; each sees the other's mech where the other says it is.
TEST(sdl_two_processes_from_the_lobby_into_a_game) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_lobby_game";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> v = {MW3_BIN};
    v.insert(v.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : v) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const int base = 34000 + static_cast<int>(getpid() % 2000) * 3;
  const std::string game = std::to_string(base), lan = std::to_string(base + 1);
  auto menus = [](std::vector<std::string> v, std::initializer_list<const char*> name) {
    std::vector<std::string> m = {"--click-at", "132,285@40"};
    int at = 70;
    for (const char* k : name) m.insert(m.end(), {"--tap", std::string(k) + "@" + std::to_string(at)}), at += 4;
    m.insert(m.end(), {"--click-at", "470,320@100"});
    v.insert(v.end(), m.begin(), m.end());
    return v;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--frame-ms", "33", "--net-soak"};
  // A: HOST; with B in: LOCK OPTIONS, its OK, I AM READY, then START.
  std::vector<std::string> va = menus({"--write-dir", dir + "/a", "--net-port", game, "--net-discovery", lan,
                                       "--click-at", "421,406@180", "--click-players", "183,430@60/2",
                                       "--click-players", "246,290@100/2", "--click-players", "515,446@140/2",
                                       "--click-players", "585,418@600/2", "--frames", "2600"},
                                      {"0x14", "0x12", "0x1f", "0x14"});
  va.insert(va.end(), common.begin(), common.end());
  // B: the game found, JOIN; in the lobby I AM READY (after A's lock).
  std::vector<std::string> vb = menus({"--write-dir", dir + "/b", "--net-port", "0", "--net-lan", "127.0.0.1:" + lan,
                                       "--click-at", "100,143@420", "--click-at", "240,406@450",
                                       "--click-players", "515,446@300/2", "--frames", "2400"},
                                      {"0x30", "0x12", "0x1f", "0x14"});
  vb.insert(vb.end(), common.begin(), common.end());
  const pid_t a = spawn(va, dir + "/a.log");
  for (int i = 0; i < 400 && read(dir + "/a.log").find("multiplayer page 2") == std::string::npos; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t b = spawn(vb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) waitpid(a, &st, 0);
  const std::string la = read(dir + "/a.log"), lb = read(dir + "/b.log");
  auto has = [](const std::string& t, const char* w) { return t.find(w) != std::string::npos; };
  // Each "soak menus" line: our own place, then each one we see.
  struct Seen {
    int lines = 0, saw = 0;
    float me[3] = {}, them[3] = {};
  };
  auto scan = [](const std::string& t, uint32_t other) {
    Seen s;
    size_t at = 0;
    while ((at = t.find("mw3: soak menus", at)) != std::string::npos) {
      const size_t end = t.find('\n', at);
      const std::string line = t.substr(at, end - at);
      at = end;
      ++s.lines;
      const size_t me = line.find(" me ");
      unsigned id = 0;
      if (me != std::string::npos) std::sscanf(line.c_str() + me, " me %u %f %f %f", &id, &s.me[0], &s.me[1], &s.me[2]);
      const std::string key = "; sees " + std::to_string(other) + " ";
      const size_t sees = line.find(key);
      if (sees != std::string::npos) {
        ++s.saw;
        std::sscanf(line.c_str() + sees + key.size(), "%f %f %f", &s.them[0], &s.them[1], &s.them[2]);
      }
    }
    return s;
  };
  const Seen sa = scan(la, 2), sb = scan(lb, 1);
  std::printf("  A: %d lines in play, B seen in %d; B: %d lines, A seen in %d\n", sa.lines, sa.saw, sb.lines, sb.saw);
  EXPECT(has(la, "multiplayer page 2") && has(lb, "multiplayer page 2"));
  EXPECT(has(la, "Loading [ c1 ]") && has(lb, "Loading [ c1 ]") && has(la, "Ready To Launch") && has(lb, "Ready To Launch"));
  EXPECT(sa.lines > 3 && sb.lines > 3 && sa.saw > 3 && sb.saw > 3);
  // Standing still, each one's own place is where the other sees it.
  EXPECT(std::fabs(sa.them[0] - sb.me[0]) < 2.0f && std::fabs(sa.them[2] - sb.me[2]) < 2.0f);
  EXPECT(std::fabs(sb.them[0] - sa.me[0]) < 2.0f && std::fabs(sb.them[2] - sa.me[2]) < 2.0f);
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// The 1.2 read-me's "your arms no longer flail" and "no longer run in place
// on other players' screens" (14c's checks), between two mw3 from the menus:
// in a game of 30 s A walks from 5 s to 15 s (--net-walk, ours: the throttle
// up held), then lets go and slows to a stop. On B's screen A stands, walks,
// slows and stands again: never a walking gait for long while A's place holds, and
// A's arms never jerk (with the ping's latency read unsigned, 2^32 ms, its
// aim's extrapolation threw them between their limits as A slowed).
TEST(sdl_two_processes_a_walk_seen_from_the_other) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_walk";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> v = {MW3_BIN};
    v.insert(v.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : v) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const int base = 40000 + static_cast<int>(getpid() % 2000) * 3;
  const std::string game = std::to_string(base), lan = std::to_string(base + 1);
  auto menus = [](std::vector<std::string> v, std::initializer_list<const char*> name) {
    std::vector<std::string> m = {"--click-at", "132,285@40"};
    int at = 70;
    for (const char* k : name) m.insert(m.end(), {"--tap", std::string(k) + "@" + std::to_string(at)}), at += 4;
    m.insert(m.end(), {"--click-at", "470,320@100"});
    v.insert(v.end(), m.begin(), m.end());
    return v;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--frame-ms", "33", "--net-soak", "--net-seconds", "30"};
  std::vector<std::string> va = menus({"--write-dir", dir + "/a", "--net-port", game, "--net-discovery", lan,
                                       "--click-at", "421,406@180", "--click-players", "183,430@60/2",
                                       "--click-players", "246,290@100/2", "--click-players", "515,446@140/2",
                                       "--click-players", "585,418@600/2", "--net-walk", "5,15", "--frames", "2400"},
                                      {"0x14", "0x12", "0x1f", "0x14"});
  va.insert(va.end(), common.begin(), common.end());
  std::vector<std::string> vb = menus({"--write-dir", dir + "/b", "--net-port", "0", "--net-lan", "127.0.0.1:" + lan,
                                       "--click-at", "100,143@420", "--click-at", "240,406@450",
                                       "--click-players", "515,446@300/2", "--frames", "2400"},
                                      {"0x30", "0x12", "0x1f", "0x14"});
  vb.insert(vb.end(), common.begin(), common.end());
  const pid_t a = spawn(va, dir + "/a.log");
  for (int i = 0; i < 400 && read(dir + "/a.log").find("multiplayer page 2") == std::string::npos; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t b = spawn(vb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) waitpid(a, &st, 0);
  // B's lines (each half second of play): A's place, gait and arms' aims.
  struct Seen {
    float z = 0;
    int gait = -1;
    float arms[4] = {};
  };
  std::vector<Seen> seen;
  const std::string lb = read(dir + "/b.log");
  for (size_t at = 0; (at = lb.find("mw3: soak menus clock", at)) != std::string::npos;) {
    const size_t end = lb.find('\n', at);
    const std::string line = lb.substr(at, end - at);
    at = end;
    const size_t p = line.find("; sees 1 ");
    Seen s;
    float x, y, hp;
    int on;
    if (p != std::string::npos && std::sscanf(line.c_str() + p, "; sees 1 %f %f %f %d health %f gait %d arms %f %f %f %f", &x, &y,
                                              &s.z, &on, &hp, &s.gait, &s.arms[0], &s.arms[1], &s.arms[2], &s.arms[3]) == 10)
      seen.push_back(s);
  }
  // Walking in place: a walking gait through three lines (1.5 s) or more
  // while A's place holds. (A's start, on its own screen too, is a step or
  // two before it gets going: one or two lines.)
  int walking = 0, in_place = 0, jerks = 0, run = 0;
  float worst = 0;
  for (size_t i = 7; i < seen.size(); ++i) {  // the first lines: the start's placing and the aim's slerp in
    const bool still = std::fabs(seen[i].z - seen[i - 1].z) < 0.05f;
    walking += seen[i].gait != 2;
    run = still && seen[i].gait != 2 ? run + 1 : 0;
    in_place += run >= 3;
    for (int k = 0; k < 4; ++k) {
      const float d = std::fabs(seen[i].arms[k] - seen[i - 1].arms[k]);
      worst = std::max(worst, d);
      jerks += d > 0.2f;
    }
  }
  std::printf("  B's lines %zu: A walking in %d, walking in place in %d; A's arms' largest change between lines %.3f, %d over 0.2\n",
              seen.size(), walking, in_place, worst, jerks);
  EXPECT(seen.size() > 40 && walking > 10 && seen.back().gait == 2);
  EXPECT(in_place == 0 && jerks == 0);
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// Playtest W45: a network game runs on while its window is inactive (FIX (A);
// the original waits, frozen, and the others fight a statue). B walks and is
// deactivated for 3 s (--blur-ingame, ours: as a lost focus, the focus back
// by a timer); B's frames go on and A sees B walk through it.
TEST(sdl_two_processes_an_inactive_window_plays_on) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_blur";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> v = {MW3_BIN};
    v.insert(v.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : v) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const int base = 42000 + static_cast<int>(getpid() % 2000) * 3;
  const std::string game = std::to_string(base), lan = std::to_string(base + 1);
  auto menus = [](std::vector<std::string> v, std::initializer_list<const char*> name) {
    std::vector<std::string> m = {"--click-at", "132,285@40"};
    int at = 70;
    for (const char* k : name) m.insert(m.end(), {"--tap", std::string(k) + "@" + std::to_string(at)}), at += 4;
    m.insert(m.end(), {"--click-at", "470,320@100"});
    v.insert(v.end(), m.begin(), m.end());
    return v;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--frame-ms", "33", "--net-soak", "--net-seconds", "60"};
  std::vector<std::string> va = menus({"--write-dir", dir + "/a", "--net-port", game, "--net-discovery", lan,
                                       "--click-at", "421,406@180", "--click-players", "183,430@60/2",
                                       "--click-players", "246,290@100/2", "--click-players", "515,446@140/2",
                                       "--click-players", "585,418@600/2", "--frames", "2400"},
                                      {"0x14", "0x12", "0x1f", "0x14"});
  va.insert(va.end(), common.begin(), common.end());
  std::vector<std::string> vb = menus({"--write-dir", dir + "/b", "--net-port", "0", "--net-lan", "127.0.0.1:" + lan,
                                       "--click-at", "100,143@420", "--click-at", "240,406@450",
                                       "--click-players", "515,446@300/2", "--net-walk", "3,60",
                                       "--blur-ingame", "200:3000", "--frames", "2400"},
                                      {"0x30", "0x12", "0x1f", "0x14"});
  vb.insert(vb.end(), common.begin(), common.end());
  const pid_t a = spawn(va, dir + "/a.log");
  for (int i = 0; i < 400 && read(dir + "/a.log").find("multiplayer page 2") == std::string::npos; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t b = spawn(vb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) waitpid(a, &st, 0);
  // B inactive for 3 s from the wall time it logs: its own lines go on (its
  // frames run), and A sees B walk on through it.
  const std::string la = read(dir + "/a.log"), lb = read(dir + "/b.log");
  long long blur = -1;
  if (const size_t p = lb.find("mw3: blur at wall "); p != std::string::npos) blur = std::atoll(lb.c_str() + p + 18);
  auto lines = [](const std::string& t, auto&& each) {
    for (size_t at = 0; (at = t.find("mw3: soak menus clock", at)) != std::string::npos;) {
      const size_t end = t.find('\n', at);
      const std::string line = t.substr(at, end - at);
      at = end;
      long long wall = 0, heard = -1;
      float z = 0;
      if (const size_t w = line.find(" wall "); w != std::string::npos) wall = std::atoll(line.c_str() + w + 6);
      if (const size_t h = line.find(" heard "); h != std::string::npos) heard = std::atoll(line.c_str() + h + 7);
      if (const size_t q = line.find("; sees "); q != std::string::npos) {
        unsigned id;
        float x, y;
        std::sscanf(line.c_str() + q, "; sees %u %f %f %f", &id, &x, &y, &z);
      }
      each(wall, z, heard);
    }
  };
  int b_during = 0;
  lines(lb, [&](long long w, float, long long) { b_during += blur > 0 && w >= blur + 300 && w <= blur + 2700; });
  // A's dead reckoning carries B on either way: what A last heard from B
  // (ms) is what shows the states coming.
  // A's clock is the stepped one (--frame-ms, faster than the wall's), so the
  // ages are against the same span before the blur.
  float first = 0, last = 0;
  long long oldest = -1, before = -1;
  bool seen = false;
  lines(la, [&](long long w, float z, long long heard) {
    if (blur > 0 && w >= blur - 2700 && w <= blur - 300) before = std::max(before, heard);
    if (blur <= 0 || w < blur + 300 || w > blur + 2700) return;
    if (!seen) first = z, seen = true;
    last = z;
    oldest = std::max(oldest, heard);
  });
  std::printf("  B inactive at %lld: its lines meanwhile %d; A saw B move %.1f m, B's last state at most %lld ms old (%lld before)\n",
              blur, b_during, std::fabs(last - first), oldest, before);
  EXPECT(blur > 0 && b_during >= 3);
  EXPECT(seen && std::fabs(last - first) > 5.0f && before >= 0 && oldest >= 0 && oldest <= 3 * before + 300);
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// Playtest W46 (and W44): Escape in a network match shows the quit box over
// the cockpit and the match goes on under it (bug fixes on): moving and the
// mouse's fire as normal; Escape again or N closes it, Y leaves to the main
// menu, and the others drop the player at once (ENet's disconnect sent now).
TEST(sdl_two_processes_leave_box_keeps_play_and_leaves) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_leave";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> v = {MW3_BIN};
    v.insert(v.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : v) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const int base = 46000 + static_cast<int>(getpid() % 2000) * 3;
  const std::string game = std::to_string(base), lan = std::to_string(base + 1);
  auto menus = [](std::vector<std::string> v, std::initializer_list<const char*> name) {
    std::vector<std::string> m = {"--click-at", "132,285@40"};
    int at = 70;
    for (const char* k : name) m.insert(m.end(), {"--tap", std::string(k) + "@" + std::to_string(at)}), at += 4;
    m.insert(m.end(), {"--click-at", "470,320@100"});
    v.insert(v.end(), m.begin(), m.end());
    return v;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--frame-ms", "33", "--net-soak", "--net-seconds", "60"};
  std::vector<std::string> va = menus({"--write-dir", dir + "/a", "--net-port", game, "--net-discovery", lan,
                                       "--click-at", "421,406@180", "--click-players", "183,430@60/2",
                                       "--click-players", "246,290@100/2", "--click-players", "515,446@140/2",
                                       "--click-players", "585,418@600/2", "--frames", "2400"},
                                      {"0x14", "0x12", "0x1f", "0x14"});
  va.insert(va.end(), common.begin(), common.end());
  std::vector<std::string> vb = menus({"--write-dir", dir + "/b", "--net-port", "0", "--net-lan", "127.0.0.1:" + lan,
                                       "--click-at", "100,143@420", "--click-at", "240,406@450",
                                       "--click-players", "515,446@300/2", "--net-walk", "3,60",
                                       "--tap-ingame", "0x01@200:203", "--hold-ingame", "260:290", "--tap-ingame", "0x01@330:333",
                                       "--tap-ingame", "0x01@400:403", "--tap-ingame", "0x31@430:433",
                                       "--tap-ingame", "0x01@500:503", "--tap-ingame", "0x15@530:533", "--fire-probe",
                                       "--verbose", "--frames", "2400"},
                                      {"0x30", "0x12", "0x1f", "0x14"});
  vb.insert(vb.end(), common.begin(), common.end());
  const pid_t a = spawn(va, dir + "/a.log");
  for (int i = 0; i < 400 && read(dir + "/a.log").find("multiplayer page 2") == std::string::npos; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t b = spawn(vb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) waitpid(a, &st, 0);
  const std::string la = read(dir + "/a.log"), lb = read(dir + "/b.log");
  auto count = [](const std::string& t, const char* w) {
    int n = 0;
    for (size_t at = 0; (at = t.find(w, at)) != std::string::npos; ++at) ++n;
    return n;
  };
  // B's box: up at 200 (Escape), down at 330 (Escape again), up at 400, down
  // at 430 (N), up at 500, down at 530 (Y: the match left, the main menu).
  const int ups = count(lb, "mw3: quit box up"), downs = count(lb, "mw3: quit box down");
  const size_t core = lb.find("state Core State"), menu = core == std::string::npos ? core : lb.find("state Main State", core);
  // With the box up (frames 200..330) B's mouse fires (the probe's trigger
  // from 260) and B walks on.
  int first = -1, fired_at = -1;
  float z_up = 0, z_down = 0;
  for (size_t at = 0; (at = lb.find("mw3: fire probe frame ", at)) != std::string::npos;) {
    const size_t end = lb.find('\n', at);
    int f = 0, trig = 0;
    unsigned buttons = 0;
    unsigned long long ms = 0;
    float reload = 0;
    std::sscanf(lb.c_str() + at, "mw3: fire probe frame %d ms %llu buttons %u trigger %d reload %f", &f, &ms, &buttons, &trig, &reload);
    at = end;
    if (first < 0) first = f;
    if (fired_at < 0 && f - first >= 260 && f - first < 330 && trig && reload >= 100.0f) fired_at = f - first;
  }
  int line = 0;
  for (size_t at = 0; (at = lb.find("mw3: soak menus clock", at)) != std::string::npos;) {
    const size_t end = lb.find('\n', at);
    const std::string l = lb.substr(at, end - at);
    at = end;
    float x, y, z;
    unsigned id;
    const size_t me = l.find(" me ");
    if (me == std::string::npos || std::sscanf(l.c_str() + me, " me %u %f %f %f", &id, &x, &y, &z) != 4) continue;
    ++line;  // a line each 15 frames from the game's first
    if (line == 15) z_up = z;    // frame 210: the box up
    if (line == 21) z_down = z;  // frame 300: still up
  }
  // A: B's mech gone from A's lines soon after B's Y (the disconnect at once),
  // and A playing on.
  int with_b = 0, after = 0;
  bool gone = false;
  for (size_t at = 0; (at = la.find("mw3: soak menus clock", at)) != std::string::npos;) {
    const size_t end = la.find('\n', at);
    const bool sees = la.substr(at, end - at).find("; sees 2 ") != std::string::npos;
    at = end;
    if (sees) ++with_b, gone = false;
    else if (with_b > 0) gone = true, ++after;
  }
  std::printf("  B's box up %d, down %d; fired at %d with it up; walked %.1f m under it; on the main menu %d; A saw B in %d lines, "
              "then %d without\n", ups, downs, fired_at, std::fabs(z_down - z_up), menu != std::string::npos, with_b, after);
  EXPECT(ups == 3 && downs == 3 && menu != std::string::npos);
  EXPECT(fired_at >= 260 && fired_at < 300 && std::fabs(z_down - z_up) > 5.0f);
  EXPECT(with_b > 5 && gone && after > 5);
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// The whole menu path twice over (piece 14c, the lead's check): A hosts, B
// joins; each game lasts 20 s (--net-seconds, ours), A is held 50 m from B
// and wrecked 8 s in (B's kill); its end goes through the Multi-Exit State
// and the main menu back to the lobby (page 2), which shows the game's
// scores the same on both machines; there (--click-return, from each
// return) A locks, says OK and readies, B readies, A starts, and they play
// again. Three games, two returns.
TEST(sdl_two_processes_lobby_game_and_back_twice) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_rounds";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> v = {MW3_BIN};
    v.insert(v.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : v) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const int base = 38000 + static_cast<int>(getpid() % 2000) * 3;
  const std::string game = std::to_string(base), lan = std::to_string(base + 1);
  auto menus = [](std::vector<std::string> v, std::initializer_list<const char*> name) {
    std::vector<std::string> m = {"--click-at", "132,285@40"};
    int at = 70;
    for (const char* k : name) m.insert(m.end(), {"--tap", std::string(k) + "@" + std::to_string(at)}), at += 4;
    m.insert(m.end(), {"--click-at", "470,320@100"});
    v.insert(v.end(), m.begin(), m.end());
    return v;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--frame-ms", "33", "--net-soak", "--net-seconds", "20"};
  // A: HOST; with B in, and again after each game: LOCK OPTIONS, its OK, I AM READY, START.
  std::vector<std::string> va = menus({"--write-dir", dir + "/a", "--net-port", game, "--net-discovery", lan,
                                       "--click-at", "421,406@180", "--click-players", "183,430@60/2",
                                       "--click-players", "246,290@100/2", "--click-players", "515,446@140/2",
                                       "--click-players", "585,418@600/2", "--click-return", "183,430@100",
                                       "--click-return", "246,290@160", "--click-return", "515,446@220",
                                       "--click-return", "585,418@650", "--net-ahead", "50", "--net-kill-at", "8",
                                       "--tap-ingame", "0x58@400:402",
                                       "--frames", "4400"},
                                      {"0x14", "0x12", "0x1f", "0x14"});
  va.insert(va.end(), common.begin(), common.end());
  // B: JOIN; I AM READY in the lobby, and again after each game.
  std::vector<std::string> vb = menus({"--write-dir", dir + "/b", "--net-port", "0", "--net-lan", "127.0.0.1:" + lan,
                                       "--click-at", "100,143@420", "--click-at", "240,406@450",
                                       "--click-players", "515,446@300/2", "--click-return", "515,446@400",
                                       "--frames", "4400"},
                                      {"0x30", "0x12", "0x1f", "0x14"});
  vb.insert(vb.end(), common.begin(), common.end());
  const pid_t a = spawn(va, dir + "/a.log");
  for (int i = 0; i < 400 && read(dir + "/a.log").find("multiplayer page 2") == std::string::npos; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const pid_t b = spawn(vb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) waitpid(a, &st, 0);
  const std::string la = read(dir + "/a.log"), lb = read(dir + "/b.log");
  auto count = [](const std::string& t, const char* w) {
    int n = 0;
    for (size_t at = 0; (at = t.find(w, at)) != std::string::npos; ++at) ++n;
    return n;
  };
  // "lobby entered N, scores: <line> | <line>": the scores the return shows.
  auto scores = [](const std::string& t, int n) {
    const std::string key = "lobby entered " + std::to_string(n) + ", scores: ";
    const size_t at = t.find(key);
    if (at == std::string::npos) return std::string("(none)");
    return t.substr(at + key.size(), t.find('\n', at) - at - key.size());
  };
  const std::string a2 = scores(la, 2), b2 = scores(lb, 2), a3 = scores(la, 3), b3 = scores(lb, 3);
  std::printf("  games: A %d, B %d; ended: A %d, B %d\n  after 1: %s\n  after 2: %s\n", count(la, "soak menus game in"),
              count(lb, "soak menus game in"), count(la, "soak menus game over"), count(lb, "soak menus game over"),
              a2.c_str(), a3.c_str());
  EXPECT(count(la, "soak menus game in") == 3 && count(lb, "soak menus game in") == 3);
  EXPECT(count(la, "soak menus game over") >= 2 && count(lb, "soak menus game over") >= 2);
  EXPECT(count(la, "Loading [ c1 ]") == 3 && count(lb, "Loading [ c1 ]") == 3);
  // Each game's scores, the same on both: B's kill of A (score, kills, suicides), A's nothing.
  char n1[32] = {}, n2[32] = {};
  int s1 = -1, k1 = -1, d1 = -1, s2 = -1, k2 = -1, d2 = -1;
  std::sscanf(a2.c_str(), "%31s %d %d %d | %31s %d %d %d", n1, &s1, &k1, &d1, n2, &s2, &k2, &d2);
  EXPECT(a2 == b2 && a3 == b3 && a2 == a3);
  EXPECT(std::string(n1) == "best" && s1 == 1 && k1 == 1 && d1 == 0);
  EXPECT(std::string(n2) == "test" && s2 == 0 && k2 == 0 && d2 == 0);
  // W43: F12 in the game shows the network scores (0xdd5, its columns, a line
  // each player), not the campaign's MISSION COMPLETE.
  const size_t box = la.find("mw3: soak menus box: ");
  const std::string boxed = box == std::string::npos ? "" : la.substr(box + 21, la.find('\n', box) - box - 21);
  std::printf("  A's F12 box: %s\n", boxed.c_str());
  EXPECT(boxed.rfind("Network Game Scores | ", 0) == 0 && boxed.find("best") != std::string::npos &&
         boxed.find("test") != std::string::npos && boxed.find("C O M P L E T E") == std::string::npos);
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// Playtest W42: the mech lab from the lobby listed no mechs (the lab's slots
// had no network game's, 0x004a5270 / 0x00437800). One mw3 hosting through
// the menus: LOCK OPTIONS and its OK, MECHLAB (the Lab State), Avatar from
// the stockpile, ACCEPT; back in the lobby the game's slot holds the Avatar
// (mech 1), not the stock Bushwacker (3).
TEST(sdl_lobby_mech_lab_picks_a_mech) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_lobby_lab";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  const int base = 44000 + static_cast<int>(getpid() % 2000) * 3;
  std::vector<std::string> v = {MW3_BIN, "--install", install, "--no-launcher", "--hidden", "--nosound",
                                "--net-bind", "127.0.0.1", "--frame-ms", "33", "--write-dir", dir, "--verbose",
                                "--net-port", std::to_string(base), "--net-discovery", std::to_string(base + 1),
                                "--click-at", "132,285@40", "--tap", "0x14@70", "--tap", "0x12@74", "--tap", "0x1f@78",
                                "--tap", "0x14@82", "--click-at", "470,320@100", "--click-at", "421,406@180",
                                "--click-at", "183,430@260", "--click-at", "246,290@320", "--click-at", "329,440@380",
                                "--click-at", "470,93@560", "--click-at", "595,460@640", "--frames", "760"};
  std::vector<char*> argv;
  for (std::string& x : v) argv.push_back(x.data());
  argv.push_back(nullptr);
  const std::string log = dir + "/out.log";
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_adddup2(&fa, 1, 2);
  pid_t pid = 0;
  if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
  posix_spawn_file_actions_destroy(&fa);
  int st = 0;
  if (pid > 0) waitpid(pid, &st, 0);
  std::string text;
  if (std::FILE* f = std::fopen(log.c_str(), "r")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
  }
  auto has = [&](const char* w) { return text.find(w) != std::string::npos; };
  std::printf("  the lab %s; back with %s\n", has("state Lab State") ? "opened" : "not opened",
              has("lobby back from the lab, mech 1\n") ? "the Avatar" : "not the Avatar");
  EXPECT(has("state Lab State") && has("lobby back from the lab, mech 1\n"));
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// The lobby's whole way to a game (multiplayer-port.md, piece 11c), one mw3
// hosting alone through the menus: MULTIPLAYER, a pilot, HOST; in the
// lobby LOCK OPTIONS and its OK, I AM READY, START; the Load state loads the
// lobby's world (Beachhead, c1) and is ready to launch.
TEST(sdl_lobby_host_alone_starts_a_game) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_lobby_start";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  const int base = 32000 + static_cast<int>(getpid() % 2000) * 3;
  std::vector<std::string> v = {MW3_BIN, "--install", install, "--no-launcher", "--hidden", "--nosound",
                                "--net-bind", "127.0.0.1", "--frame-ms", "33", "--write-dir", dir,
                                "--net-port", std::to_string(base), "--net-discovery", std::to_string(base + 1),
                                "--click-at", "132,285@40", "--tap", "0x14@70", "--tap", "0x12@74", "--tap", "0x1f@78",
                                "--tap", "0x14@82", "--click-at", "470,320@100", "--click-at", "421,406@180",
                                "--click-at", "183,430@260", "--click-at", "246,290@320", "--click-at", "515,446@380",
                                "--click-at", "585,418@440", "--frames", "1400"};
  std::vector<char*> argv;
  for (std::string& x : v) argv.push_back(x.data());
  argv.push_back(nullptr);
  const std::string log = dir + "/out.log";
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_adddup2(&fa, 1, 2);
  pid_t pid = 0;
  if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
  posix_spawn_file_actions_destroy(&fa);
  int st = 0;
  if (pid > 0) waitpid(pid, &st, 0);
  std::string text;
  if (std::FILE* f = std::fopen(log.c_str(), "r")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
  }
  auto has = [&](const char* w) { return text.find(w) != std::string::npos; };
  std::printf("  lobby %s, world %s, %s\n", has("multiplayer page 2") ? "reached" : "not reached",
              has("Loading [ c1 ]") ? "c1 loading" : "not loading", has("Ready To Launch") ? "ready to launch" : "not ready");
  EXPECT(has("multiplayer page 2") && has("Loading [ c1 ]") && has("Ready To Launch"));
  EXPECT(!has("the Load state of this game"));
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid
// Damage in a network game at a known range (multiplayer-port.md, piece 8d's
// check of the soak's little damage): two mw3 on loopback in c1mp1, B
// standing still, A held 40 m off facing it and firing (2 s in 7). Each
// machine decides hits on its own mech: B's own health falls, and A's view
// of B (its states' damaged parts) agrees with it.
TEST(sdl_network_damage_point_blank) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_net_damage";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/a", ec);
  std::filesystem::create_directories(dir + "/b", ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> a = {"/usr/bin/nice", "-n", "10", MW3_BIN};
    a.insert(a.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : a) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  const std::vector<std::string> common = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind",
                                           "127.0.0.1", "--mission", "c1mp1", "--net-wait", "2", "--net-minutes", "1",
                                           "--net-soak"};
  std::vector<std::string> ha = {"--write-dir", dir + "/a", "--host", "--net-port", "0", "--net-name", "A", "--net-drive",
                                 "1", "--net-staged", "0", "--net-ahead", "40"};
  ha.insert(ha.end(), common.begin(), common.end());
  const pid_t a = spawn(ha, dir + "/a.log");
  std::string port;
  for (int i = 0; i < 400 && port.empty(); ++i) {
    const std::string log = read(dir + "/a.log");
    const size_t at = log.find("hosting on port ");
    if (at != std::string::npos) port = log.substr(at + 16, log.find(' ', at + 16) - at - 16);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT(!port.empty());
  std::vector<std::string> hb = {"--write-dir", dir + "/b", "--join", "127.0.0.1:" + port, "--net-name", "B"};
  hb.insert(hb.end(), common.begin(), common.end());
  const pid_t b = port.empty() ? -1 : spawn(hb, dir + "/b.log");
  int st = 0;
  if (b > 0) waitpid(b, &st, 0);
  if (a > 0) waitpid(a, &st, 0);
  // The last line with 2 s left on each: B's own health, and A's view of it.
  auto last = [&](const std::string& text, float* own, float* seen) {
    *own = *seen = -1;
    for (size_t at = 0; (at = text.find("mw3: soak 1", at)) != std::string::npos;) {
      const size_t end = text.find('\n', at);
      const std::string line = text.substr(at, end - at);
      at = end;
      float clock = 0;
      if (std::sscanf(line.c_str(), "mw3: soak %*lld rss %*zu clock %f", &clock) != 1 || clock < 2.0f) continue;
      const size_t h = line.find(" health ");
      if (h != std::string::npos) *own = std::strtof(line.c_str() + h + 8, nullptr);
      const size_t s2 = line.find("; sees ");
      const size_t h2 = s2 == std::string::npos ? s2 : line.find(" health ", s2);
      if (h2 != std::string::npos) *seen = std::strtof(line.c_str() + h2 + 8, nullptr);
    }
  };
  float a_own, a_sees_b, b_own, b_sees_a;
  last(read(dir + "/a.log"), &a_own, &a_sees_b);
  last(read(dir + "/b.log"), &b_own, &b_sees_a);
  std::printf("  B's health: its own %.3f, as A sees it %.3f; A's own %.3f\n", b_own, a_sees_b, a_own);
  EXPECT(b_own >= 0 && b_own < 0.95f);                 // A's shots hit B on B's machine
  EXPECT(a_sees_b >= 0 && std::fabs(a_sees_b - b_own) < 0.02f);  // and A's view of B carries it
  EXPECT(a_own > 0.999f);                              // B never fired
}
#endif

#if !defined(_WIN32)  // posix_spawn, waitpid, kill
// The soak test (docs/multiplayer-port.md, piece 8b), MW3_SOAK=1 only: four
// hidden mw3 under nice on loopback in a network game on c1mp1, a host and
// three joiners, each driving its mech (walk, turn, fire), under 5% loss and
// 80 ms each way, for MW3_SOAK_MINUTES (30) on the game's clock. Each prints
// where its own mech and every remote one are each half second. Checked: no
// crash and a clean game over on the clock in all four; memory not growing
// (the last tenth's against the second's); every remote mech tracked within
// a few metres of its owner's own track at the same time; the four score
// tables alike, with the staged kills in them.
TEST(sdl_network_soak_four_players) {
  const char* on = std::getenv("MW3_SOAK");
  if (!on || std::strcmp(on, "1") != 0) return;
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(install + "/zbd/mechlib.zbd", ec)) return;
  const int minutes = std::getenv("MW3_SOAK_MINUTES") ? std::atoi(std::getenv("MW3_SOAK_MINUTES")) : 30;
  // MW3_SOAK_DIR keeps the four logs there (the test's own folder goes at its end).
  const std::string dir = std::getenv("MW3_SOAK_DIR") ? std::getenv("MW3_SOAK_DIR") : std::string(MW3_TEST_TMP) + "/sdl_net_soak";
  std::filesystem::remove_all(dir, ec);
  auto spawn = [&](std::vector<std::string> args, const std::string& log) {
    std::vector<std::string> a = {"/usr/bin/nice", "-n", "15", MW3_BIN};
    a.insert(a.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& x : a) argv.push_back(x.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    return pid;
  };
  auto read = [](const std::string& path) {
    std::string text;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text;
  };
  setenv("MW3_NET_LOSS", "0.05", 1);
  setenv("MW3_NET_DELAY", "80", 1);
  auto args = [&](int n, const std::string& net) {
    std::filesystem::create_directories(dir + "/p" + std::to_string(n), ec);
    std::vector<std::string> v = {"--install", install, "--no-launcher", "--hidden", "--nosound", "--net-bind", "127.0.0.1",
                                  "--write-dir", dir + "/p" + std::to_string(n), "--mission", "c1mp1", "--net-wait", "4",
                                  "--net-minutes", std::to_string(minutes), "--net-drive", std::to_string(n), "--net-soak",
                                  "--net-name", "P" + std::to_string(n)};
    if (net.empty()) v.insert(v.end(), {"--host", "--net-port", "0"});
    else v.insert(v.end(), {"--join", net});
    return v;
  };
  std::vector<pid_t> pids = {spawn(args(1, ""), dir + "/p1.log")};
  std::string port;
  for (int i = 0; i < 400 && port.empty(); ++i) {
    const std::string log = read(dir + "/p1.log");
    const size_t at = log.find("hosting on port ");
    if (at != std::string::npos) port = log.substr(at + 16, log.find(' ', at + 16) - at - 16);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT(!port.empty());
  for (int n = 2; n <= 4 && !port.empty(); ++n) pids.push_back(spawn(args(n, "127.0.0.1:" + port), dir + "/p" + std::to_string(n) + ".log"));
  unsetenv("MW3_NET_LOSS");
  unsetenv("MW3_NET_DELAY");
  // All four to their end, the game's clock and five minutes more at most.
  std::vector<int> status(pids.size(), -1);
  const auto until = std::chrono::steady_clock::now() + std::chrono::minutes(minutes + 5);
  for (size_t left = pids.size(); left > 0 && std::chrono::steady_clock::now() < until;) {
    left = 0;
    for (size_t i = 0; i < pids.size(); ++i) {
      if (pids[i] <= 0 || status[i] != -1) continue;
      int st = 0;
      if (waitpid(pids[i], &st, WNOHANG) == pids[i]) status[i] = st;
      else ++left;
    }
    if (left) std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  for (size_t i = 0; i < pids.size(); ++i)
    if (pids[i] > 0 && status[i] == -1) {
      kill(pids[i], SIGKILL);
      waitpid(pids[i], &status[i], 0);
      std::printf("  player %zu still running at the end: killed\n", i + 1);
    }
  // Each one's lines: its own place by time, what it saw of the others, its
  // memory, its scores.
  struct Seen { long long t; uint32_t id; float x, z; };
  struct Own { long long t; float x, z; int alive; };
  struct Log { uint32_t id = 0; std::vector<Own> own; std::vector<Seen> seen; std::vector<size_t> rss; std::string scores; std::set<uint32_t> last_seen; };
  std::vector<Log> logs(pids.size());
  for (size_t i = 0; i < pids.size(); ++i) {
    const std::string text = read(dir + "/p" + std::to_string(i + 1) + ".log");
    Log& L = logs[i];
    for (size_t at = 0; (at = text.find("mw3: soak ", at)) != std::string::npos;) {
      const size_t end = text.find('\n', at);
      const std::string line = text.substr(at, end - at);
      at = end;
      if (line.rfind("mw3: soak game over, scores ", 0) == 0) {
        L.scores = line.substr(28);
        continue;
      }
      long long t = 0;
      size_t kb = 0;
      float clock = 0, x, y, z;
      unsigned id;
      int alive, used = 0;
      if (std::sscanf(line.c_str(), "mw3: soak %lld rss %zu clock %f shots %*d me %u %f %f %f %d%n", &t, &kb, &clock, &id, &x, &y,
                      &z, &alive, &used) < 8)
        continue;
      L.id = id, L.rss.push_back(kb), L.own.push_back({t, x, z, alive});
      // The others seen at the end: the last line with 2 s left (in the last
      // seconds the end's unload takes the mechs).
      const bool counted = clock >= 2.0f;
      if (counted) L.last_seen.clear();
      for (size_t p = line.find("; sees ", used); p != std::string::npos; p = line.find("; sees ", p + 1)) {
        unsigned who;
        int active;
        if (std::sscanf(line.c_str() + p, "; sees %u %f %f %f %d", &who, &x, &y, &z, &active) != 5) continue;
        if (counted) L.last_seen.insert(who);
        if (active) L.seen.push_back({t, who, x, z});
      }
    }
  }
  // The owner's place at time t, between its two lines (null: none, or a
  // death or respawn within a second of it).
  auto owner_at = [&](uint32_t id, long long t, float out[2]) {
    for (const Log& L : logs) {
      if (L.id != id) continue;
      for (size_t k = 1; k < L.own.size(); ++k) {
        const Own &a = L.own[k - 1], &b = L.own[k];
        if (b.t < t) continue;
        if (a.t > t || !a.alive || !b.alive || std::hypot(b.x - a.x, b.z - a.z) > 30.0f) return false;
        if (k < 3 || k + 2 >= L.own.size() || !L.own[k - 2].alive || !L.own[k + 1].alive) return false;
        const float f = b.t == a.t ? 0.0f : static_cast<float>(t - a.t) / static_cast<float>(b.t - a.t);
        out[0] = a.x + (b.x - a.x) * f, out[1] = a.z + (b.z - a.z) * f;
        return true;
      }
    }
    return false;
  };
  bool clean = true, over = true, tracked = true, memory = true;
  for (size_t i = 0; i < pids.size(); ++i) {
    Log& L = logs[i];
    const bool exited = WIFEXITED(status[i]) && WEXITSTATUS(status[i]) == 0;
    clean &= exited;
    over &= !L.scores.empty();
    std::vector<float> err;
    for (const Seen& s : L.seen) {
      float o[2];
      if (owner_at(s.id, s.t, o)) err.push_back(std::hypot(s.x - o[0], s.z - o[1]));
    }
    std::sort(err.begin(), err.end());
    const float p95 = err.empty() ? 1e9f : err[std::min(err.size() - 1, err.size() * 95 / 100)];
    const float median = err.empty() ? 1e9f : err[err.size() / 2];
    // Memory: the median of the last tenth's lines against the second tenth's (the first: the load).
    float growth = 1e9f;
    const size_t n = L.rss.size();
    if (n >= 20) {
      auto med = [&](size_t from, size_t to) {
        std::vector<size_t> v(L.rss.begin() + static_cast<long>(from), L.rss.begin() + static_cast<long>(to));
        std::sort(v.begin(), v.end());
        return static_cast<float>(v[v.size() / 2]);
      };
      growth = med(n * 9 / 10, n) / med(n / 10, n * 2 / 10) - 1.0f;
    }
    std::printf("  player %zu (id %u): %s, %zu lines, sees %zu others at the end, tracking median %.2f m p95 %.2f m over %zu, "
                "memory %+.1f%% (%zu KB at the end), scores \"%s\"\n",
                i + 1, L.id, exited ? "exited" : "did not exit cleanly", n, L.last_seen.size(), median, p95, err.size(),
                growth * 100.0f, n ? L.rss.back() : 0, L.scores.c_str());
    tracked &= L.last_seen.size() == pids.size() - 1 && err.size() >= 20 && p95 < 5.0f;
    memory &= growth < 0.10f;
  }
  EXPECT(clean);
  EXPECT(over);
  EXPECT(tracked);
  EXPECT(memory);
  bool alike = !logs.empty();
  for (const Log& L : logs) alike &= L.scores == logs[0].scores;
  EXPECT(alike);
  // The staged kills (one each two minutes) reached the table: some kill or death in it.
  int tally = 0;
  for (size_t at = 0; !logs.empty() && (at = logs[0].scores.find(':', at)) != std::string::npos; ++at) {
    int k = 0, d = 0;
    if (std::sscanf(logs[0].scores.c_str() + at, ":%d/%d", &k, &d) == 2) tally += k + d;
  }
  EXPECT(tally > 0);
}
#endif

#if !defined(_WIN32)  // posix_spawn (ours, not on Windows)
// The bug fixes switch (ours; fixes.md, The bug fixes switch): --original or
// bugfixes=0 in mw3.cfg turn every play-changing fix off, said at the start;
// bugfixes=1 and no line leave them on.
TEST(sdl_program_bugfixes_switch) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/sdl_bugfixes";
  std::error_code ec;
  auto run = [&](const char* cfg, bool original) {
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    if (cfg)
      if (std::FILE* f = std::fopen((dir + "/mw3.cfg").c_str(), "w")) std::fputs(cfg, f), std::fclose(f);
    std::vector<std::string> a = {"--write-dir", dir, "--nosound", "--hidden", "--frames", "3"};
    if (original) a.push_back("--original");
    const std::string log = dir + "/../sdl_bugfixes.log";
    const pid_t pid = spawn_mw3(a, log);
    int status = 0;
    if (pid > 0) waitpid(pid, &status, 0);
    std::string text;
    if (std::FILE* f = std::fopen(log.c_str(), "r")) {
      char buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
      std::fclose(f);
    }
    return text.find("mw3: bug fixes off") != std::string::npos;
  };
  EXPECT(!run(nullptr, false));
  EXPECT(!run("bugfixes=1\n", false));
  EXPECT(run("bugfixes=0\n", false));
  EXPECT(run(nullptr, true));
  EXPECT(run("bugfixes=1\n", true));  // --original wins
}
#endif

#if !defined(_WIN32)
// The launcher's first-start notes (lead's launcher list, item 5): the
// data's version and the music, in the launcher's words (SETUP's buttons);
// nothing to say for 1.2 data with music. 1.0's data and a mix need no note
// either: they load as 1.2 (docs/patch12.md); only data of an unknown
// version is told about the patch. The music is any CD track's file.
TEST(launcher_first_start_notes) {
  const std::vector<std::string> none = sdlfe::launcher_notes("1.2", true, true, "M");
  const std::vector<std::string> v10 = sdlfe::launcher_notes("1.0", false, true, "M");
  const std::vector<std::string> mix = sdlfe::launcher_notes("1.0 with 1.2 files", true, true, "M");
  const std::vector<std::string> odd = sdlfe::launcher_notes("unknown", true, true, "M");
  const std::vector<std::string> cd = sdlfe::launcher_notes("1.2", true, false, "M");   // movies left on the CD
  const std::vector<std::string> both = sdlfe::launcher_notes("1.2", false, false, "M");
  const bool counts = none.empty() && v10.size() == 1 && mix.empty() && odd.size() == 1 && cd.size() == 1 && both.size() == 1;
  EXPECT(counts);
  if (counts) {
    EXPECT(v10[0].find("No music yet") != std::string::npos && v10[0].find("ADD FROM MY DISC") != std::string::npos &&
           v10[0].find("in M as track02") != std::string::npos && v10[0].find("movies") == std::string::npos);
    EXPECT(odd[0].find("modded") != std::string::npos && odd[0].find("data mods") != std::string::npos &&
           odd[0].find("Mech3.exe") != std::string::npos && odd[0].find("1.2") == std::string::npos &&
           odd[0].find("SETUP") == std::string::npos);  // the 1.2 patch is built in now
    EXPECT(cd[0].find("no movies") != std::string::npos && cd[0].find("ADD FROM MY DISC") != std::string::npos &&
           cd[0].find("music") == std::string::npos);
    EXPECT(both[0].find("no movies") != std::string::npos && both[0].find("no music") != std::string::npos &&
           both[0].find(".cue") != std::string::npos);
  }
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_music";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  EXPECT(!sdlfe::have_music({dir, ""}));
  std::ofstream(dir + "/track01.ogg") << "x";  // the data track's number: not music
  EXPECT(!sdlfe::have_music({dir}));
  std::ofstream(dir + "/track07.flac") << "x";
  EXPECT(sdlfe::have_music({"", dir}));
}

// The launcher's MW3 line with a long install folder (lead, after the PC's
// shot: it ran past the button's sides): shortened from the middle to the
// width, "Ready: version 1.2, " kept whole, the folder's end kept longer.
TEST(launcher_long_path_fits_the_button) {
  auto chars = [](const std::string& t) { return static_cast<int>(t.size()); };  // a width per character
  const std::string ready = "Ready: version 1.2, ";
  const std::string path = "C:\\Users\\SomebodyWithALongName\\Documents\\My Games\\MicroProse\\game\\mw3-1.2";
  const std::string fit = sdlfe::fit_middle(ready + path, ready.size(), 50, chars);
  std::printf("  %s\n", fit.c_str());
  EXPECT(static_cast<int>(fit.size()) <= 50 && fit.rfind(ready, 0) == 0 && fit.find("...") != std::string::npos);
  EXPECT(fit.size() > 44 && fit.compare(fit.size() - 7, 7, "mw3-1.2") == 0);  // its name, and as much as fits
  EXPECT(sdlfe::fit_middle("short", 0, 50, chars) == "short");
  EXPECT(sdlfe::fit_middle(ready + path, ready.size(), 10, chars) == ready + "...");  // too narrow: the words alone
}

// The launcher's one picker (Owen, 2026-11-02: one option): an image or a
// download is itself the source; any other file stands for its folder
// (Mech3.exe or any file of an installed copy, zbd\ and video\ up one, or a
// disc's folder); a .bin with its .cue beside it is the .cue.
TEST(launcher_one_picker_takes_a_file_for_its_folder) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_pick";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/mw3/zbd", ec);
  std::filesystem::create_directories(dir + "/disc/VIDEO", ec);
  for (const char* f : {"/mw3/Mech3.exe", "/mw3/readme.txt", "/mw3/zbd/reader.zbd", "/disc/VIDEO/C1.AVI", "/disc/DATA1.HDR",
                        "/game.zip", "/disc.cue", "/disc.bin", "/lone.bin", "/disc.iso"})
    std::ofstream(dir + f) << "x";
  const std::vector<std::string> kinds = {"iso", "cue", "img", "zip", "7z", "rar"};
  EXPECT(sdlfe::pick_source(dir + "/mw3/Mech3.exe", kinds) == dir + "/mw3");
  EXPECT(sdlfe::pick_source(dir + "/mw3/readme.txt", kinds) == dir + "/mw3");
  EXPECT(sdlfe::pick_source(dir + "/mw3/zbd/reader.zbd", kinds) == dir + "/mw3");  // a valid install: zbd\ is not it
  EXPECT(sdlfe::pick_source(dir + "/disc/VIDEO/C1.AVI", kinds) == dir + "/disc");  // video\ up one
  EXPECT(sdlfe::pick_source(dir + "/disc/DATA1.HDR", kinds) == dir + "/disc");
  EXPECT(sdlfe::pick_source(dir + "/game.zip", kinds) == dir + "/game.zip");
  EXPECT(sdlfe::pick_source(dir + "/disc.iso", kinds) == dir + "/disc.iso");
  EXPECT(sdlfe::pick_source(dir + "/disc.bin", kinds) == dir + "/disc.cue");  // its sheet
  EXPECT(sdlfe::pick_source(dir + "/lone.bin", kinds) == dir);                // no sheet: a file, its folder
  EXPECT(sdlfe::pick_source(dir + "/disc", kinds) == dir + "/disc");          // a folder as it is
}

// The launcher's narrow spaces (lead, setup.png: "No MechWarrior 3was
// found"): DIN Condensed's space is 2 pixels at 13 and its digits draw a
// pixel past their advance, so the space after a digit vanished. A space
// under a fifth of the size is doubled; with the Mac's font, the blank gap
// after "3" is then as wide as the font's own between "a" and "g".
TEST(launcher_narrow_spaces_widen) {
  EXPECT(sdlfe::widen_spaces("MechWarrior 3 was", 2, 13) == "MechWarrior  3  was");
  EXPECT(sdlfe::widen_spaces("MechWarrior 3 was", 3, 13) == "MechWarrior 3 was");  // wide enough: as is
  EXPECT(sdlfe::widen_spaces("none", 1, 13) == "none");
  const char* din = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf";
  if (!std::filesystem::exists(din) || !TTF_Init()) return;
  TTF_Font* f = TTF_OpenFont(din, 13.0f);
  EXPECT(f != nullptr);
  if (!f) return;
  auto gap = [&](const std::string& s) {  // blank columns between the first two inked runs
    SDL_Surface* t = TTF_RenderText_Blended(f, s.c_str(), 0, SDL_Color{255, 255, 255, 255});
    SDL_Surface* c = t ? SDL_ConvertSurface(t, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    int blank = 0, best = -1;
    bool run = false;
    for (int x = 0; c && x < c->w && best < 0; ++x) {
      bool ink = false;
      for (int y = 0; y < c->h; ++y) ink |= (static_cast<const uint32_t*>(c->pixels)[y * (c->pitch / 4) + x] >> 24) > 32;
      if (ink && run && blank) best = blank;
      else if (ink) run = true, blank = 0;
      else if (run) ++blank;
    }
    if (c) SDL_DestroySurface(c);
    if (t) SDL_DestroySurface(t);
    return best;
  };
  int adv = 0;
  TTF_GetGlyphMetrics(f, ' ', nullptr, nullptr, nullptr, nullptr, &adv);
  const int before = gap("3 w"), after = gap(sdlfe::widen_spaces("3 w", adv, 13)), word = gap("a g");
  std::printf("  DIN 13: space %d px; the gap after 3 %d, widened %d, a to g %d\n", adv, before, after, word);
  EXPECT(adv * 5 < 13 && after > before && after >= word - 1);
  TTF_CloseFont(f);
  TTF_Quit();
}

// The launcher (ours, docs/launcher.md), drawn once into a BMP: with the
// development copy found, the games' screen; with none (MW3_FIND_INSTALL=1,
// an empty write dir), the setup screen; and the progress screen. Each a
// lit 640x480 canvas, and the log says what it found.
TEST(sdl_program_launcher_screens) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/wd", ec);
  std::filesystem::create_directories(dir + "/wd2", ec);
  // The first entry hovered (Owen, the PC's fresh install): the ripple only
  // round a round badge; a square plate lights its edge (at edge_x, edge_y,
  // the plate's top) in amber instead.
  struct Shot {
    const char* screen;
    bool none;
    const char* says;
    const char* hover;
    int edge_x, edge_y;
  };
  for (const Shot& s : {Shot{"main", false, "screen main: MECHWARRIOR 3, PIRATE'S MOON, GAME FILES, QUIT\n", "hover MECHWARRIOR 3: ripple\n", -1, -1},
                        Shot{"setup", true, "screen find: FIND THE GAME, BACK\n", "hover FIND THE GAME: edge\n", 320, 222},
                        Shot{"pick", true, "screen find: FIND THE GAME, BACK\n", "hover FIND THE GAME: edge\n", 320, 222},
                        Shot{"work", false, "launcher: Found MechWarrior 3", "hover CANCEL: edge\n", 320, 372},
                        Shot{"notes", false, "screen notes: ADD FROM MY DISC, OK\n", "hover ADD FROM MY DISC: edge\n", 320, 318},
                        Shot{"disc", false, "screen notes: ADD FROM MY DISC, OK\n", "hover ADD FROM MY DISC: edge\n", 320, 318}}) {
    const std::string bmp = dir + "/" + s.screen + ".bmp", log = dir + "/" + s.screen + ".log";
    if (s.none) setenv("MW3_FIND_INSTALL", "1", 1);
    const pid_t pid = spawn_launcher({"--write-dir", dir + (s.none ? "/wd2" : "/wd"), "--launcher-shot", bmp, "--launcher-screen", s.screen}, log);
    if (s.none) unsetenv("MW3_FIND_INSTALL");
    int status = 0;
    EXPECT(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::ifstream f(log);
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    SDL_Surface* img = SDL_LoadBMP(bmp.c_str());
    int lit = 0, n = 0;
    bool edge = s.edge_x < 0;
    if (img) {
      SDL_Surface* c = SDL_ConvertSurface(img, SDL_PIXELFORMAT_ARGB8888);
      if (c && s.edge_x >= 0) {
        const uint32_t p = static_cast<const uint32_t*>(c->pixels)[s.edge_y * (c->pitch / 4) + s.edge_x];
        edge = (p >> 16 & 0xff) > 230 && (p >> 8 & 0xff) > 150 && (p >> 8 & 0xff) < 200 && (p & 0xff) < 100;  // amber
      }
      for (int y = 0; c && y < c->h; y += 7)
        for (int x = 0; x < c->w; x += 7) {
          const uint32_t p = static_cast<const uint32_t*>(c->pixels)[y * (c->pitch / 4) + x];
          ++n, lit += std::max({p >> 16 & 0xff, p >> 8 & 0xff, p & 0xff}) > 40;
        }
      std::printf("launcher %s: %dx%d, %d of %d lit, says \"%s\" %d\n", s.screen, img->w, img->h, lit, n, s.says,
                  text.find(s.says) != std::string::npos);
      if (c) SDL_DestroySurface(c);
    }
    EXPECT(img && img->w == 640 && img->h == 480 && lit > n / 4);
    EXPECT(text.find(s.says) != std::string::npos);
    EXPECT(text.find(s.hover) != std::string::npos && edge);
    if (img) SDL_DestroySurface(img);
  }
}

// A plain mw3 start (docs/games-layout.md): straight into the game when an
// install is found, no launcher; with none, the find screen alone, and once
// POINT TO YOUR GAME's pick is installed, on into the game (MW3_START_ONLY
// stops each where it would go on; MW3_LAUNCHER_PICK stands for the dialog);
// --launcher shows the launcher's main screen.
TEST(sdl_program_plain_start_goes_straight_in_or_to_setup) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/plain_start";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  for (const char* d : {"/wd", "/wd2", "/wd3", "/wd4", "/disc"}) std::filesystem::create_directories(dir + d, ec);
  auto run = [&](const std::vector<std::string>& args, const std::string& log, bool launcher = false) {
    const pid_t pid = launcher ? spawn_launcher(args, log) : spawn_mw3(args, log);
    int status = 0;
    const bool ok = pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    std::ifstream f(log);
    return ok ? std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) : std::string("(failed)");
  };
  setenv("MW3_START_ONLY", "1", 1);
  setenv("MW3_LAUNCHER_FRAMES", "20", 1);
  // An install found: straight in.
  const std::string a = run({"--write-dir", dir + "/wd", "--window", "640x480", "--nosound"}, dir + "/a.log");
  EXPECT(a.find("mw3: start only: install ") != std::string::npos && a.find("(none yet)") == std::string::npos &&
         a.find("launcher:") == std::string::npos);
  // The launcher's main screen is mw3launcher's (quit after 20 frames), and
  // mw3 knows no --launcher any more.
  const std::string b = run({"--write-dir", dir + "/wd2", "--window", "640x480", "--nosound"}, dir + "/b.log", true);
  EXPECT(run({"--launcher", "--write-dir", dir + "/wd2"}, dir + "/b2.log") == "(failed)");
  EXPECT(b.find("launcher: Found MechWarrior 3") != std::string::npos && b.find("start only") == std::string::npos);
  // None: the find screen alone (QUIT after 20 frames: no game).
  setenv("MW3_FIND_INSTALL", "1", 1);
  const std::string c = run({"--write-dir", dir + "/wd3", "--window", "640x480", "--nosound"}, dir + "/c.log");
  EXPECT(c.find("no install found: the find screen") != std::string::npos && c.find("start only") == std::string::npos);
  // None, and a disc's files picked: looked at, installed, on into the game.
  const std::string patch = std::string(MW3_GAME_DIR) + "/../patch-1.2/MechWarrior-3_Patch_Win_EN_Patch-12-US";
  if (std::filesystem::exists(patch + "/data1.cab")) {
    std::filesystem::copy_file(patch + "/data1.hdr", dir + "/disc/DATA1.HDR", ec);
    std::filesystem::copy_file(patch + "/data1.cab", dir + "/disc/DATA1.CAB", ec);
    setenv("MW3_LAUNCHER_FRAMES", "1800", 1);  // about 30 s: a launcher that never goes on quits and fails, not hangs
    setenv("MW3_LAUNCHER_PICK", (dir + "/disc").c_str(), 1);
    const std::string d = run({"--write-dir", dir + "/wd4", "--window", "640x480", "--nosound"}, dir + "/d.log");
    unsetenv("MW3_LAUNCHER_PICK");
    std::printf("plain start, picked: %s\n", d.substr(d.find("is installed in") == std::string::npos ? 0 : d.find("is installed in"), 120).c_str());
    EXPECT(d.find("no install found: the find screen") != std::string::npos && d.find("is installed in") != std::string::npos &&
           d.find("mw3: start only: install " + dir + "/wd4/install") != std::string::npos &&
           std::filesystem::exists(dir + "/wd4/install/mw3-install.txt"));
    std::ifstream cfg(dir + "/wd4/mw3.cfg");
    const std::string text((std::istreambuf_iterator<char>(cfg)), std::istreambuf_iterator<char>());
    EXPECT(text.find("install=" + dir + "/wd4/install") != std::string::npos);
  }
  unsetenv("MW3_FIND_INSTALL");
  unsetenv("MW3_LAUNCHER_FRAMES");
  unsetenv("MW3_START_ONLY");
}

// One window from the find screen into the game: a plain mw3 start with no
// install found (MW3_FIND_INSTALL), an installed copy picked
// (MW3_LAUNCHER_PICK), hands its window over, its renderer freed, and the
// game claims it for its GPU device instead of opening a second window and
// shows a frame in it (--ready-file's line); then stopped.
TEST(sdl_program_launcher_window_goes_on_to_the_game) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_play";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/wd/music", ec);
  std::ofstream(dir + "/wd/music/track02.wav") << "x";  // music there: no first-start notes box (modal) before the window
  const std::string log = dir + "/play.log";
  setenv("MW3_FIND_INSTALL", "1", 1);
  setenv("MW3_LAUNCHER_PICK", MW3_GAME_DIR, 1);
  const pid_t pid = spawn_mw3({"--write-dir", dir + "/wd", "--window", "1280x960", "--nosound", "--no-import", "--ready-file", dir + "/ready"}, log);
  unsetenv("MW3_LAUNCHER_PICK");
  unsetenv("MW3_FIND_INSTALL");
  std::string text;
  for (int i = 0; i < 600 && pid > 0; ++i) {  // up to a minute for the game's first frame
    std::ifstream f(log);
    text.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.find("(the launcher told)") != std::string::npos || waitpid(pid, nullptr, WNOHANG) == pid) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (pid > 0) kill(pid, SIGKILL), waitpid(pid, nullptr, 0);  // SDL makes SIGTERM a quit event a modal box ignores
  const bool handed = text.find("launcher: its window goes on to the game") != std::string::npos;
  const bool took = text.find("the game took the launcher's window") != std::string::npos;
  const bool shown = text.find("(the launcher told)") != std::string::npos && std::filesystem::exists(dir + "/ready");
  std::printf("setup into the game: handed %d, taken %d, a frame shown %d\n", handed, took, shown);
  if (!(handed && took && shown)) std::printf("%s\n", text.substr(text.size() > 1500 ? text.size() - 1500 : 0).c_str());
  EXPECT(handed && took && shown);
  EXPECT(text.find("the menu's sounds") == std::string::npos);  // --nosound: none
}

// mw3launcher starts the game as its own program (docs/launcher.md, The two
// programs): MECHWARRIOR 3 (--launcher-play) runs MW3_GAME_BIN, here a
// stand-in with the install and this start's options, as `script` says
// (it keeps its arguments; READY writes the ready file, DONE its end's
// mark, which it also writes when it reaches the script's end). The launcher ends at
// the game's first frame and leaves it running (Owen, 2026-11-01); a game
// that ends first or is slow leaves it up. Hidden, without sound.
namespace {
struct LauncherRun {
  std::string log;
  std::vector<std::string> args;
  bool exited = false;      // the launcher's process ended, with 0
  bool game_alive = false;  // the stand-in had not finished when it did
  bool game_finished = false;  // and finished on its own after
};
// `env` set for the launcher's run only; `before` gets the test's folder
// (its write dir is <dir>/wd) to lay out files first.
LauncherRun run_launcher_with(const std::string& name, const std::string& script, const char* ready_ms = nullptr,
                              const std::vector<std::pair<std::string, std::string>>& env = {},
                              const std::function<void(const std::string&)>& before = nullptr, bool play = true) {
  LauncherRun out;
  const std::string dir = std::string(MW3_TEST_TMP) + "/" + name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/wd", ec);
  if (before) before(dir);
  const std::string game = dir + "/mw3", done = dir + "/done";
  {
    std::ofstream sh(game);
    std::string body = script;
    for (size_t at; (at = body.find("READY")) != std::string::npos;)
      body.replace(at, 5, "while [ $# -gt 0 ]; do if [ \"$1\" = --ready-file ]; then : > \"$2\"; fi; shift; done");
    for (size_t at; (at = body.find("DONE")) != std::string::npos;) body.replace(at, 4, ": > \"" + done + "\"");
    for (size_t at; (at = body.find("STATUS")) != std::string::npos;)  // the game's status line, beside the ready file
      body.replace(at, 6, "R=; P=; for A in \"$@\"; do [ \"$P\" = --ready-file ] && R=$A; P=$A; done; "
                          "printf 'Importing your movies (first start only): 3 of 19...' > \"$R.status\"");
    sh << "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"" << dir << "/args.txt\"\n" << body << "\n: > \"" << done << "\"\n";
  }
  std::filesystem::permissions(game, std::filesystem::perms::owner_all, ec);
  setenv("MW3_GAME_BIN", game.c_str(), 1);
  if (ready_ms) setenv("MW3_LAUNCHER_READY_MS", ready_ms, 1);
  bool importer = false;
  for (const auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1), importer = importer || k == "MW3_IMPORTER";
  if (!importer) setenv("MW3_NO_IMPORT", "1", 1);  // no movie conversion in the launcher unless a stand-in is given
  std::vector<std::string> args = {"--write-dir", dir + "/wd", "--window", "640x480", "--nosound", "--hidden"};
  if (play) args.insert(args.begin() + 2, "--launcher-play");
  const pid_t pid = spawn_launcher(args, dir + "/launcher.log");
  for (const auto& [k, v] : env) unsetenv(k.c_str());
  unsetenv("MW3_NO_IMPORT");
  unsetenv("MW3_LAUNCHER_READY_MS");
  unsetenv("MW3_GAME_BIN");
  int status = 0;
  out.exited = pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  out.game_alive = !std::filesystem::exists(done, ec);
  for (int i = 0; i < 100 && !std::filesystem::exists(done, ec); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  out.game_finished = std::filesystem::exists(done, ec);
  std::ifstream f(dir + "/launcher.log");
  out.log.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::ifstream af(dir + "/args.txt");
  for (std::string l; std::getline(af, l);) out.args.push_back(l);
  return out;
}
bool has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }
}  // namespace

TEST(sdl_program_launcher_starts_mw3_as_its_own_program) {
  // The first frame at once, then 3 s of play: the launcher has gone by then.
  const LauncherRun r = run_launcher_with("launcher_game", "READY\nsleep 3");
  std::printf("launcher -> game: %zu arguments; the launcher ended %d with the game still running %d, the game ended later %d\n",
              r.args.size(), r.exited, r.game_alive, r.game_finished);
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_game";
  EXPECT(r.args.size() == 9 && r.args[0] == MW3_GAME_DIR && r.args[1] == "--write-dir" && r.args[2] == dir + "/wd" &&
         r.args[3] == "--window" && r.args[4] == "640x480" && r.args[5] == "--nosound" && r.args[6] == "--hidden" &&
         r.args[7] == "--ready-file");
  EXPECT(r.exited && r.game_alive && r.game_finished);
  EXPECT(has(r.log, "the game's first frame") && has(r.log, "the launcher closes") && !has(r.log, "MechWarrior 3 exited"));
}

// In an AppImage the launcher waits for the game: its end would unmount what
// the game is running from (docs/linux.md, Packaging).
TEST(sdl_program_launcher_waits_for_the_game_in_an_appimage) {
  const LauncherRun r = run_launcher_with("launcher_game_image", "READY\nsleep 2", nullptr, {{"APPIMAGE", "/x.AppImage"}});
  EXPECT(r.exited && !r.game_alive && r.game_finished);
  EXPECT(has(r.log, "the game's first frame") && has(r.log, "MechWarrior 3 exited (0)"));
}

// The data folder elsewhere (docs/program.md): the three programs' starts with
// a pointer. The launcher's move, end to end, hidden.
TEST(sdl_program_data_folder_pointer_and_move) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/data_folder_program";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  const std::string usual = dir + "/usual", gone = dir + "/gone/mine", to = dir + "/drive/mine";
  std::filesystem::create_directories(usual, ec);
  setenv("MW3_USUAL_FOLDER", usual.c_str(), 1);
  auto wait = [](pid_t pid) {
    int status = 0;
    return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  };
  auto text = [](const std::string& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  // A pointer to a missing drive: said, no fresh folder, not the usual one in its place.
  std::ofstream(usual + "/" + mech3::data_folder::kPointer) << gone << "\n";
  const std::string log = dir + "/out.log";
  EXPECT(wait(spawn_bin(MW3_BIN, {"--hidden", "--nosound"}, log)) == 1);
  EXPECT(has(text(log), "data folder is not there") && !std::filesystem::exists(dir + "/gone"));
  EXPECT(wait(spawn_launcher({"--hidden", "--nosound", "--window", "640x480"}, log)) == 1);
  EXPECT(!std::filesystem::exists(dir + "/gone") && !std::filesystem::exists(usual + "/mw3launcher.log"));
  // --write-dir wins over it.
  setenv("MW3_START_ONLY", "1", 1);
  const std::string named = dir + "/named";
  EXPECT(wait(spawn_bin(MW3_BIN, {"--hidden", "--nosound", "--write-dir", named}, log)) == 0);
  unsetenv("MW3_START_ONLY");
  EXPECT(std::filesystem::exists(named + "/mw3.log") && !std::filesystem::exists(dir + "/gone"));
  // No pointer, the launcher's move and its remove: the folder switched, the old copy gone.
  std::filesystem::remove(usual + "/" + mech3::data_folder::kPointer, ec);
  std::ofstream(usual + "/mw3.cfg") << "install=" << MW3_GAME_DIR << "\n";
  std::filesystem::create_directories(usual + "/pilots", ec);
  std::ofstream(usual + "/pilots/a.pil") << "pilot";
  setenv("MW3_LAUNCHER_MOVE_TO", to.c_str(), 1);
  setenv("MW3_LAUNCHER_MOVE_OLD", "remove", 1);
  setenv("MW3_LAUNCHER_FRAMES", "90", 1);
  EXPECT(wait(spawn_launcher({"--hidden", "--nosound", "--window", "640x480"}, log)) == 0);
  unsetenv("MW3_LAUNCHER_MOVE_TO");
  unsetenv("MW3_LAUNCHER_MOVE_OLD");
  unsetenv("MW3_LAUNCHER_FRAMES");
  EXPECT(text(usual + "/" + mech3::data_folder::kPointer) == to + "\n");
  EXPECT(text(to + "/pilots/a.pil") == "pilot" && !std::filesystem::exists(usual + "/pilots"));
  // The next launcher reads the pointer and passes the folder to the game.
  const std::string game = dir + "/game.sh", args = dir + "/args.txt";
  std::ofstream(game) << "#!/bin/sh\necho \"$@\" > " << args << "\nexit 0\n";
  std::filesystem::permissions(game, std::filesystem::perms::owner_all, ec);
  setenv("MW3_GAME_BIN", game.c_str(), 1);
  setenv("MW3_NO_IMPORT", "1", 1);
  EXPECT(wait(spawn_launcher({"--hidden", "--nosound", "--window", "640x480", "--launcher-play"}, log)) == 0);
  unsetenv("MW3_GAME_BIN");
  unsetenv("MW3_NO_IMPORT");
  EXPECT(text(args).find("--write-dir " + to) != std::string::npos);
  EXPECT(std::filesystem::exists(to + "/mw3launcher.log"));
  unsetenv("MW3_USUAL_FOLDER");
}

TEST(sdl_program_launcher_stays_when_the_game_fails_or_is_slow) {
  // An error before any frame: the launcher up with its status line, until
  // the game's end (and, with --launcher-play, quits then).
  const LauncherRun bad = run_launcher_with("launcher_game_bad", "sleep 1\nDONE\nexit 3");
  std::printf("a failed start: the launcher saw the exit %d, stayed %d\n", has(bad.log, "MechWarrior 3 exited (3)"),
              !has(bad.log, "the launcher closes"));
  EXPECT(bad.exited && has(bad.log, "MechWarrior 3 exited (3)") && !has(bad.log, "the game's first frame"));
  // No frame within the wait (0.5 s here, 20 s in play): said, still up;
  // the frame after it closes the launcher all the same.
  const LauncherRun slow = run_launcher_with("launcher_game_slow", "sleep 2\nREADY\nsleep 2", "500");
  std::printf("a slow start: said %d, then closed at the first frame %d with the game running %d\n",
              has(slow.log, "no first frame from the game after"), has(slow.log, "the launcher closes"), slow.game_alive);
  EXPECT(slow.exited && slow.game_alive);
  EXPECT(has(slow.log, "no first frame from the game after") && has(slow.log, "the launcher closes"));
}

// The launcher for v1.0 (Owen and the lead, 2026-11-02; docs/launcher.md):
// the games and QUIT only; MECHWARRIOR 3 without its files opens the find
// screen (FIND THE GAME, BACK; no Setup, Search, Folder or Patch); a game
// found there starts at once, through the notes when the install lacks
// movies or music (their ADD FROM MY DISC), the notes' OK the start.
TEST(sdl_program_launcher_finds_the_game_then_plays) {
  auto screen = [](const std::string& log, const char* line) { return log.find(line) != std::string::npos; };
  const auto none = std::vector<std::pair<std::string, std::string>>{{"MW3_FIND_INSTALL", "1"}, {"MW3_LAUNCHER_FRAMES", "20"}};
  // No install: the games, then the find screen; nothing started.
  const LauncherRun a = run_launcher_with("launcher_v1_none", "READY\nsleep 1", nullptr, none);
  std::printf("no install: main %d, find %d, game started %d\n", screen(a.log, "screen main: MECHWARRIOR 3, PIRATE'S MOON, GAME FILES, QUIT\n"),
              screen(a.log, "screen find: FIND THE GAME, BACK\n"), !a.args.empty());
  EXPECT(a.exited && a.args.empty());
  EXPECT(screen(a.log, "screen main: MECHWARRIOR 3, PIRATE'S MOON, GAME FILES, QUIT\n") && screen(a.log, "screen find: FIND THE GAME, BACK\n"));
  for (const char* gone : {"SETUP", "SEARCH AGAIN", "A FOLDER INSTEAD", "APPLY THE 1.2 PATCH", "POINT TO YOUR GAME",
                           "A DISC IMAGE OR DOWNLOAD", ", A FOLDER,", "THE DISC'S FOLDER"})
    EXPECT(!screen(a.log, gone));
  // Found (an installed copy picked), music there: straight into the game.
  const auto pick = std::vector<std::pair<std::string, std::string>>{{"MW3_FIND_INSTALL", "1"}, {"MW3_LAUNCHER_PICK", MW3_GAME_DIR},
                                                                   {"MW3_LAUNCHER_FRAMES", "1800"}};
  const LauncherRun b = run_launcher_with("launcher_v1_music", "READY\nsleep 2", nullptr, pick, [](const std::string& d) {
    std::filesystem::create_directories(d + "/wd/music");
    std::ofstream(d + "/wd/music/track02.wav") << "x";
  });
  EXPECT(b.exited && !b.args.empty() && b.args[0] == MW3_GAME_DIR && screen(b.log, "the game's first frame"));
  EXPECT(!screen(b.log, "screen notes:"));
  // Found, no music: the notes first (ADD FROM MY DISC, OK), their OK the start.
  const LauncherRun c = run_launcher_with("launcher_v1_notes", "READY\nsleep 2", nullptr, pick);
  EXPECT(c.exited && !c.args.empty() && c.args[0] == MW3_GAME_DIR && screen(c.log, "the game's first frame"));
  EXPECT(screen(c.log, "screen notes: ADD FROM MY DISC, OK\n") && screen(c.log, "note: No music yet"));
  EXPECT(c.log.find("screen notes:") < c.log.find("the game's first frame"));
  // A CD-type install (its video folder empty, the movies left on the disc):
  // the notes say both are missing.
  const std::string cd = std::string(MW3_TEST_TMP) + "/launcher_v1_cd_install";
  std::error_code ec;
  std::filesystem::remove_all(cd, ec);
  std::filesystem::create_directories(cd + "/video", ec);
  std::filesystem::create_directory_symlink(std::string(MW3_GAME_DIR) + "/zbd", cd + "/zbd", ec);
  const auto pick_cd = std::vector<std::pair<std::string, std::string>>{{"MW3_FIND_INSTALL", "1"}, {"MW3_LAUNCHER_PICK", cd},
                                                                      {"MW3_LAUNCHER_FRAMES", "1800"}};
  const LauncherRun d = run_launcher_with("launcher_v1_cd", "READY\nsleep 2", nullptr, pick_cd);
  std::printf("a CD-type install: notes %d, both missing %d, started %d\n", screen(d.log, "screen notes: ADD FROM MY DISC, OK\n"),
              screen(d.log, "no movies (they stayed on the CD) and no music"), !d.args.empty());
  EXPECT(d.exited && !d.args.empty() && d.args[0] == cd);
  EXPECT(screen(d.log, "screen notes: ADD FROM MY DISC, OK\n") && screen(d.log, "no movies (they stayed on the CD) and no music"));
  std::filesystem::remove_all(cd, ec);
}

// The movies converted in the launcher (Owen's first start sat 44 s on
// "Starting MechWarrior 3..." while mw3 did it): a found install's AVIs go
// through the importer (here a stand-in, MW3_IMPORTER, as mw3import: its
// "progress K N name" lines, the .fmv files) on the work screen before the
// game starts; with them done, a later start goes straight in.
TEST(sdl_program_launcher_converts_the_movies_before_the_game) {
  auto seen = [](const std::string& log, const std::string& line) { return log.find(line) != std::string::npos; };
  const std::string root = std::string(MW3_TEST_TMP) + "/launcher_convert";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root + "/cd/video", ec);
  std::filesystem::create_directory_symlink(std::string(MW3_GAME_DIR) + "/zbd", root + "/cd/zbd", ec);
  for (const char* m : {"/cd/video/C1.AVI", "/cd/video/INTRO.AVI"}) std::ofstream(root + m) << "x";
  const std::string importer = root + "/mw3import";
  {
    std::ofstream sh(importer);
    sh << "#!/bin/sh\nS=; W=\nwhile [ $# -gt 0 ]; do case \"$1\" in --install) S=$2; shift;; --write-dir) W=$2; shift;; esac; shift; done\n"
          "mkdir -p \"$W/video\"; echo progress 0 2; K=0\nfor F in \"$S\"/video/*.AVI; do N=$(basename \"$F\" .AVI | tr A-Z a-z); "
          ": > \"$W/video/$N.fmv\"; K=$((K+1)); echo progress $K 2 $N; done\n";
  }
  std::filesystem::permissions(importer, std::filesystem::perms::owner_all, ec);
  const auto env = std::vector<std::pair<std::string, std::string>>{{"MW3_FIND_INSTALL", "1"}, {"MW3_LAUNCHER_PICK", root + "/cd"},
                                                                  {"MW3_LAUNCHER_FRAMES", "1800"}, {"MW3_IMPORTER", importer}};
  auto music = [](const std::string& d) {
    std::filesystem::create_directories(d + "/wd/music");
    std::ofstream(d + "/wd/music/track02.wav") << "x";
  };
  const LauncherRun r = run_launcher_with("launcher_convert_run", "READY\nsleep 1", nullptr, env, music);
  const std::string wd = std::string(MW3_TEST_TMP) + "/launcher_convert_run/wd";
  std::printf("convert: work %d, both .fmv %d, before the game %d, started %d\n", seen(r.log, "work: CONVERTING THE MOVIES"),
              std::filesystem::exists(wd + "/video/c1.fmv") && std::filesystem::exists(wd + "/video/intro.fmv"),
              r.log.find("work: CONVERTING THE MOVIES") < r.log.find("the game's first frame"), !r.args.empty());
  EXPECT(r.exited && !r.args.empty() && seen(r.log, "work: CONVERTING THE MOVIES") && seen(r.log, "the game's first frame"));
  EXPECT(r.log.find("work: CONVERTING THE MOVIES") < r.log.find("the game's first frame"));
  EXPECT(std::filesystem::exists(wd + "/video/c1.fmv") && std::filesystem::exists(wd + "/video/intro.fmv"));
  std::filesystem::remove_all(root, ec);
}

// GAME FILES (Owen, 2026-11-02): the find screen at any time, by keys or a
// pad as by the mouse (MW3_LAUNCHER_NAV); with the install in use lacking
// its music, ADD FROM MY DISC there too. A repick to a copy used where it
// is: the launcher's own earlier copy (<write dir>/install, no longer
// pointed at) is removed, mw3.cfg points at the new one, back to the games.
TEST(sdl_program_launcher_game_files_repicks) {
  auto seen = [](const std::string& log, const std::string& line) { return log.find(line) != std::string::npos; };
  const auto keys = std::vector<std::pair<std::string, std::string>>{
      {"MW3_FIND_INSTALL", "1"}, {"MW3_LAUNCHER_FRAMES", "20"}, {"MW3_LAUNCHER_NAV", "next,next,press"}};
  // The launcher's own copy in use: its data linked from the development copy.
  auto own_copy = [](const std::string& d) {
    std::error_code ec;
    std::filesystem::create_directories(d + "/wd/install/video", ec);
    std::filesystem::create_directory_symlink(std::string(MW3_GAME_DIR) + "/zbd", d + "/wd/install/zbd", ec);
    std::ofstream(d + "/wd/install/mw3-install.txt") << "test\n";
    std::ofstream(d + "/wd/install/video/intro.avi") << "x";
    std::ofstream(d + "/wd/mw3.cfg") << "install=" << d << "/wd/install\n";
  };
  const LauncherRun a = run_launcher_with("launcher_files_keys", "READY", nullptr, keys, own_copy, false);
  std::printf("GAME FILES by keys: focus %d, find screen %d\n", seen(a.log, "focus GAME FILES"),
              seen(a.log, "screen find: FIND THE GAME, ADD FROM MY DISC, BACK\n"));
  EXPECT(a.exited && a.args.empty());
  EXPECT(seen(a.log, "screen main: MECHWARRIOR 3, PIRATE'S MOON, GAME FILES, QUIT\n") && seen(a.log, "focus MECHWARRIOR 3\n") &&
         seen(a.log, "focus GAME FILES\n") && seen(a.log, "screen find: FIND THE GAME, ADD FROM MY DISC, BACK\n"));
  // GAME FILES, then the development copy picked (used where it is).
  const auto repick = std::vector<std::pair<std::string, std::string>>{{"MW3_FIND_INSTALL", "1"},
                                                                     {"MW3_LAUNCHER_FRAMES", "600"},
                                                                     {"MW3_LAUNCHER_NAV", "next,next,press"},
                                                                     {"MW3_LAUNCHER_PICK", MW3_GAME_DIR}};
  const LauncherRun b = run_launcher_with("launcher_files_repick", "READY", nullptr, repick, own_copy, false);
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_files_repick";
  std::ifstream cfg(dir + "/wd/mw3.cfg");
  const std::string text((std::istreambuf_iterator<char>(cfg)), std::istreambuf_iterator<char>());
  std::printf("repick: removed %d, cfg %d, back to the games %d\n", seen(b.log, "is no longer used: removed"),
              seen(text, std::string("install=") + MW3_GAME_DIR), seen(b.log.substr(b.log.find("no longer used")), "screen main:"));
  EXPECT(b.exited && b.args.empty() && seen(b.log, "is no longer used: removed") && !std::filesystem::exists(dir + "/wd/install"));
  EXPECT(seen(text, std::string("install=") + MW3_GAME_DIR));
  EXPECT(seen(b.log, "no longer used") && seen(b.log.substr(b.log.find("no longer used")), "screen main:"));
}

// The game's own status before its first frame (its first start's movie
// import, sdl_child.h set_ready_status): on the launcher's status line in
// place of the wait's "not shown a picture yet".
TEST(sdl_program_launcher_shows_the_games_status) {
  const LauncherRun r = run_launcher_with("launcher_game_status", "STATUS\nsleep 2\nREADY\nsleep 1", "500");
  std::printf("a game importing: said %d, waited quietly %d, closed %d\n",
              has(r.log, "the game says: Importing your movies (first start only): 3 of 19..."),
              !has(r.log, "no first frame from the game after"), has(r.log, "the launcher closes"));
  EXPECT(r.exited && has(r.log, "the game says: Importing your movies (first start only): 3 of 19...") &&
         !has(r.log, "no first frame from the game after") && has(r.log, "the launcher closes"));
}

// The launcher's sounds (lead's launcher list, item 4; docs/launcher.md):
// the main menu's own rollover and select (sounds.zrd's MAINMENU set:
// sfx_button2.wav, sfx_button6.wav) read from the install's sound archive,
// none of ours. Skipped where the machine has no audio device.
TEST(sdl_program_launcher_plays_the_menus_sounds) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
  const bool device = SDL_GetNumAudioDrivers() > 0 && std::string(SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "") != "dummy";
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  if (!device) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_sounds";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/wd", ec);
  const std::string log = dir + "/sounds.log";
  setenv("MW3_LAUNCHER_MUTE", "1", 1);  // played at no volume (the machine may be streaming)
  setenv("MW3_LAUNCHER_FRAMES", "5", 1);
  const pid_t pid = spawn_launcher({"--write-dir", dir + "/wd", "--window", "640x480", "--hidden"}, log);
  unsetenv("MW3_LAUNCHER_FRAMES");
  unsetenv("MW3_LAUNCHER_MUTE");
  int status = 0;
  EXPECT(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  std::ifstream f(log);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const size_t at = text.find("the menu's sounds from ");
  std::printf("launcher sounds: %s\n", at == std::string::npos ? "none" : text.substr(at, text.find('\n', at) - at).c_str());
  EXPECT(at != std::string::npos && text.find("(2 of 2)", at) != std::string::npos && text.find("/zbd/sounds", at) != std::string::npos);
}

// A check that loses its window's focus still runs to its end (lead, after
// the launcher hand-over test hung 28 min on a Mac with another app in front,
// and a scheduled --shot run on the PC the same way): the original pauses on
// deactivation (0x00590c00) and waits for the window again, which a run
// that ends by itself never gets. MW3_FOCUS_LOST_AT puts the focus loss in
// at a pass of the pump; --frames must still end the run.
TEST(sdl_program_check_runs_on_without_focus) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/focus_lost";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/wd", ec);
  const std::string log = dir + "/run.log";
  setenv("MW3_FOCUS_LOST_AT", "30", 1);
  const pid_t pid = spawn_mw3({"--no-launcher", "--write-dir", dir + "/wd", "--window", "640x480", "--nosound", "--frames", "150"}, log);
  unsetenv("MW3_FOCUS_LOST_AT");
  int status = 0;
  bool ended = false;
  const auto start = std::chrono::steady_clock::now();
  while (pid > 0) {
    if (waitpid(pid, &status, WNOHANG) == pid) {
      ended = true;
      break;
    }
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(90)) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::ifstream f(log);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const bool lost = text.find("focus lost (MW3_FOCUS_LOST_AT)") != std::string::npos;
  std::printf("focus lost at pass 30: %s; the run %s\n", lost ? "yes" : "no", ended ? "ended by itself" : "hung (killed)");
  EXPECT(lost && ended && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

#if !defined(_WIN32)
// The MechWarrior (1989) entry (lead's launcher list, item 3;
// docs/launcher.md): shown when Elthial's port is found (here a stand-in
// script by MW3_MW1_PORT), started as its own script starts it
// (`mechwarrior <game dir> <capture dir>`, the saves in
// MECHWARRIOR_SAVE_DIRECTORY, both under the write dir's mw1) with the
// folder mw3.cfg's mw1_game= keeps; the launcher waits for it.
TEST(sdl_program_launcher_starts_mechwarrior_1989) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/launcher_mw1";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir + "/wd", ec);
  std::filesystem::create_directories(dir + "/game", ec);
  std::ofstream(dir + "/game/BTECH.EXE") << "x";
  {
    std::ofstream cfg(dir + "/wd/mw3.cfg");
    cfg << "mw1_game=" << dir << "/game\n";
  }
  const std::string port = dir + "/mechwarrior";
  {
    std::ofstream sh(port);
    sh << "#!/bin/sh\necho \"$1|$2|$MECHWARRIOR_SAVE_DIRECTORY\" > \"$2/args.txt\"\nexit 0\n";
  }
  std::filesystem::permissions(port, std::filesystem::perms::owner_all, ec);
  setenv("MW3_MW1_PORT", port.c_str(), 1);
  const std::string log = dir + "/mw1.log";
  const pid_t pid = spawn_launcher({"--write-dir", dir + "/wd", "--launcher-mw1", "--window", "640x480", "--nosound", "--hidden"}, log);
  unsetenv("MW3_MW1_PORT");
  int status = 0;
  EXPECT(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  std::ifstream f(log);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::ifstream af(dir + "/wd/mw1/captures/args.txt");
  std::string args;
  std::getline(af, args);
  std::printf("launcher mw1: found %d, started %d, exited %d; it was given \"%s\"\n",
              text.find("MechWarrior (1989) port at") != std::string::npos, text.find("MechWarrior (1989) started") != std::string::npos,
              text.find("MechWarrior (1989) exited (0)") != std::string::npos, args.c_str());
  EXPECT(text.find("MechWarrior (1989) port at " + port) != std::string::npos);
  EXPECT(text.find("MechWarrior (1989) exited (0)") != std::string::npos);
  EXPECT(args == dir + "/game|" + dir + "/wd/mw1/captures|" + dir + "/wd/mw1/saves");
}
#endif

#if !defined(_WIN32)  // posix_spawn (ours, not on Windows)
// The movies on a wide screen (W1, docs/fmv.md section 8): INTRO, C4WIN and
// a 4:3 briefing imported (mw3import, FFmpeg), their shapes measured from the
// pictures (INTRO letterboxed throughout; C4WIN letterboxed, then its 4:3
// ending; C1M1 4:3), then hidden 1920x1080 shots on a 16:9 screen mode:
// INTRO across the whole width with bars above and below, C1M1 at the
// screen's height with black sides. Skipped without FFmpeg in the importer.
namespace {
// The lit box (any channel over 24) of a 32-bit BMP: left, top, right, bottom.
bool bmp_lit_box(const std::string& path, int* w, int* h, int box[4]) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M' || d[28] != 32) return false;
  auto u32 = [&](size_t at) { return static_cast<uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | d[at + 3] << 24); };
  const uint32_t off = u32(10);
  *w = static_cast<int32_t>(u32(18));
  const int32_t sh = static_cast<int32_t>(u32(22));
  *h = std::abs(sh);
  if (static_cast<size_t>(off) + static_cast<size_t>(*w) * *h * 4 > d.size()) return false;
  box[0] = *w, box[1] = *h, box[2] = -1, box[3] = -1;
  for (int y = 0; y < *h; ++y)
    for (int x = 0; x < *w; ++x) {
      const size_t i = off + (static_cast<size_t>(y) * *w + x) * 4;
      if (std::max({d[i], d[i + 1], d[i + 2]}) <= 24) continue;
      const int row = sh > 0 ? *h - 1 - y : y;  // bottom up unless the height is negative
      box[0] = std::min(box[0], x), box[2] = std::max(box[2], x);
      box[1] = std::min(box[1], row), box[3] = std::max(box[3], row);
    }
  return true;
}
}  // namespace

TEST(sdl_program_movies_keep_their_shape_on_a_wide_screen) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!std::filesystem::exists(MW3_IMPORT_BIN, ec) || !std::filesystem::exists(install + "/video/INTRO.AVI", ec)) return;
  const std::string dir = std::string(MW3_TEST_TMP) + "/wide_movies";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  for (const char* name : {"intro", "c4win", "c1m1", "c1"}) {  // c1m1's block plays c1 first
    const pid_t pid = spawn_bin(MW3_IMPORT_BIN, {"--install", install, "--write-dir", dir, "--only", name},
                                dir + "/import-" + name + ".log");
    int status = 0;
    if (pid > 0) waitpid(pid, &status, 0);
  }
  if (!std::filesystem::exists(dir + "/video/intro.fmv", ec)) return;  // no FFmpeg in this importer
  // The shapes, measured.
  auto runs_of = [&](const char* name) {
    std::vector<mech3::MovieFrameScan> f;
    int32_t w = 0, h = 0;
    if (!mech3::scan_imported_movie(dir + "/video/" + name, &f, &w, &h)) return std::vector<mech3::MovieShapeRun>{};
    return mech3::movie_shape_runs(f, w, h, 15);
  };
  const auto intro = runs_of("intro"), c4win = runs_of("c4win"), c1m1 = runs_of("c1m1");
  std::printf("shapes: %s | %s | %s\n", mech3::movie_shape_line("intro", intro).c_str(),
              mech3::movie_shape_line("c4win", c4win).c_str(), mech3::movie_shape_line("c1m1", c1m1).c_str());
  // INTRO: 640x320 inside its frame (rows 80..400, give or take the
  // pictures' own edges), the whole movie one run.
  EXPECT(intro.size() == 1 && !intro[0].whole && intro[0].top >= 70 && intro[0].top <= 90 && intro[0].bottom >= 395 &&
         intro[0].bottom <= 410);
  // C4WIN: letterboxed (its 640x240 picture and the taller shots beside it),
  // then the 4:3 ending from a cut about 68 s in (frame 1020 at 15 a second).
  EXPECT(c4win.size() == 2 && !c4win[0].whole && c4win[0].top >= 90 && c4win[0].top <= 125 && c4win[0].bottom >= 355 &&
         c4win[0].bottom <= 385 && c4win[1].whole && c4win[1].start >= 990 && c4win[1].start <= 1050);
  // C1M1: a 4:3 briefing, left alone.
  EXPECT(c1m1.size() == 1 && c1m1[0].whole);
  // The shots on a 16:9 mode (13, 1920x1080, as the graphics options save it).
  if (std::FILE* f = std::fopen((dir + "/settings.reg").c_str(), "w")) {
    std::fputs("Windows Registry Editor Version 5.00\n\n[HKEY_CURRENT_USER\\SOFTWARE\\MicroProse\\MechWarrior 3\\1.0]\n"
               "\"InGameVMode\"=dword:0000000d\n", f);
    std::fclose(f);
  }
  auto shot = [&](const char* movie, const char* frame, int box[4]) {
    const std::string bmp = dir + "/" + movie + ".bmp";
    const pid_t pid = spawn_mw3({"--write-dir", dir, "--movie", movie, "--window", "1920x1080", "--nosound", "--hidden",
                                 "--shot", bmp, "--shot-frame", frame},
                                dir + "/" + movie + ".log");
    int status = 0;
    if (pid > 0) waitpid(pid, &status, 0);
    int w = 0, h = 0;
    return bmp_lit_box(bmp, &w, &h, box) && w == 1920 && h == 1080;
  };
  int a[4] = {}, b[4] = {};
  const bool sa = shot("INTRO", "300", a), sb = shot("c1m1", "100", b);
  std::printf("1920x1080: INTRO lit %d..%d x %d..%d, C1M1 lit %d..%d x %d..%d\n", a[0], a[2], a[1], a[3], b[0], b[2], b[1], b[3]);
  // INTRO across the width, black bars above and below (its 322 rows at 3x: 966 of 1080).
  EXPECT(sa && a[0] <= 8 && a[2] >= 1911 && a[1] >= 40 && a[3] <= 1040);
  // C1M1 at the screen's height, 1440 wide in the middle: black sides.
  EXPECT(sb && b[0] >= 230 && b[2] <= 1690 && b[1] <= 30 && b[3] >= 1050);
  // The shipped movies' shapes are the table's: none measured, none written.
  std::ifstream cache(dir + "/movie-shapes.txt");
  const std::string text((std::istreambuf_iterator<char>(cache)), std::istreambuf_iterator<char>());
  EXPECT(text.find("intro/") == std::string::npos && text.find("c1/") == std::string::npos);
}
#endif
#endif

TEST(sdl_render3d_core_critical_defaults_stay_opt_in) {
  const sdlfe::RendererBackend backend = sdlfe::RendererBackend::sdl;
  const sdlfe::CoreCriticalQuality quality{};
  const sdlfe::CoreCriticalAssets assets{};
  const sdlfe::HudOptions hud{};
  EXPECT(backend == sdlfe::RendererBackend::sdl);
  EXPECT(quality.antialiasing == "off" && quality.anisotropy == "1");
  EXPECT(!assets.render_thread && assets.geometry == "legacy" &&
         !assets.geometry_debug_normals);
  EXPECT(assets.textures.empty() && assets.models.empty() &&
         assets.normals.empty());
  EXPECT(assets.lighting == "off" && assets.sun_azimuth == "45" &&
         assets.sun_elevation == "35");
  EXPECT(assets.empty());
  EXPECT(hud.mode == "original" && hud.artwork.empty() && hud.text);
}
