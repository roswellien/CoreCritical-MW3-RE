cmake_minimum_required(VERSION 3.20)
project(mw3re LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)  # the version resource and icon (tools/windows/version.rc.in)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The install the tests and the tools read (any MW3 data version; assets.md,
# Data versions): -DMW3_GAME_DIR=... for another, e.g. game/mw3-1.0.
set(MW3_GAME_DIR "${CMAKE_SOURCE_DIR}/game/mw3-1.2" CACHE PATH "The MechWarrior 3 install the tests and tools read")

# A build for other players (tools/windows/package.cmd): mw3 and mw3import
# get no development copy to fall back on, only the player's own install.
option(MW3_PACKAGE "Build mw3 and mw3import for players: no MW3_GAME_DIR in them" OFF)
# The version mw3 --version and the Windows version resource give: git's
# describe (a tag, or the commit) and the commit count.
execute_process(COMMAND git describe --tags --match "v[0-9]*" --always --dirty WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE MW3_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE MW3_BUILD_NUMBER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT MW3_VERSION)
  set(MW3_VERSION "unknown")
endif()
if(NOT MW3_BUILD_NUMBER)
  set(MW3_BUILD_NUMBER 0)
endif()

if(MSVC)
  add_compile_options(/W4 "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")  # build.cmd's own flags replace CMake's default
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# The rebuilt game and engine. No platform or engine types in here: the front end
# (SDL3, with Godot as the fallback) talks to it through src/platform.
add_library(cc_geometry_resources STATIC
  src/frontend/core_critical/renderer/geometry/resource.cpp)
target_compile_features(cc_geometry_resources PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(cc_geometry_resources PRIVATE /EHsc)
endif()
add_library(mw3core STATIC
  src/gamez/zmodel/gmod_geometry.cpp
  src/platform/prepared_visuals.cpp
  src/platform/prepared_hud.cpp
  src/crt/msvc_math.cpp
  src/crt/msvc_rand.cpp
  src/crt/msvc_string.cpp
  src/gamez/zerror/zerr.cpp
  src/gamez/zvideo/zvid_dd.cpp
  src/gamez/zvideo/zvid_ddd3d.cpp
  src/gamez/zvideo/zvid_format.cpp
  src/gamez/zutil/ztimer.cpp
  src/gamez/zinput/zin_init.cpp
  src/gamez/zinput/zin_kbd.cpp
  src/gamez/zinput/zin_mouse.cpp
  src/gamez/zinput/zin_cmdmap.cpp
  src/gamez/zinput/zin_joy.cpp
  src/gamez/znetwork/znet_object.cpp
  src/gamez/znetwork/znet_session.cpp
  src/gamez/znetwork/znet_dplay.cpp
  src/gamez/zutil/zutl_msg.cpp
  src/gamez/zutil/zutl_msgdll.cpp
  src/gamez/zutil/zutl_path.cpp
  src/gamez/zutil/zutl_reg.cpp
  src/platform/reg_file.cpp
  src/platform/cue_audio.cpp
  src/mech3/data_folder.cpp
  src/mech3/data_version.cpp
  src/mech3/patch12.cpp
  src/mech3/patch12_edits.cpp
  src/mech3/patch12_force.cpp
  src/mech3/patch12_hud.cpp
  src/mech3/patch12_text.cpp
  src/mech3/patch12_tree.cpp
  src/mech3/patch12_world.cpp
  src/mech3/patch_import.cpp
  src/mech3/disc_install.cpp
  src/mech3/source_archive.cpp
  src/mech3/source_map.cpp
  src/mech3/source_install.cpp
  src/mech3/game_setup.cpp
  third_party/miniz/miniz_tinfl.c
  src/gamez/zutil/zutl_zar.cpp
  src/gamez/zsave/zsave.cpp
  src/gamez/zclass/camera.cpp
  src/gamez/zclass/class.cpp
  src/gamez/zclass/cls_di.cpp
  src/gamez/zclass/cls_mechlib.cpp
  src/gamez/zclass/cls_node.cpp
  src/gamez/zclass/cls_util.cpp
  src/gamez/zclass/cls_world.cpp
  src/gamez/zclass/cls_zbd.cpp
  src/gamez/zclass/display.cpp
  src/gamez/zclass/light.cpp
  src/gamez/zclass/lod.cpp
  src/gamez/zclass/list.cpp
  src/gamez/zclass/seq.cpp
  src/gamez/zclass/object3d.cpp
  src/gamez/zclass/sound.cpp
  src/gamez/zclass/window.cpp
  src/gamez/zdeclient/zdec_init.cpp
  src/gamez/zdeclient/zdec_crater.cpp
  src/gamez/zdeclient/zdec_qsand.cpp
  src/gamez/zgeometry/zgeo_model.cpp
  src/gamez/zgeometry/zgeo_weiler.cpp
  src/gamez/zgeometry/zgeo_convexify.cpp
  src/gamez/zeffect/zeff_anim_init.cpp
  src/gamez/zentity/zent_init.cpp
  src/gamez/zentity/zent_manager.cpp
  src/gamez/zeffect/zeff_anim_puffer.cpp
  src/gamez/zeffect/zeff_anim_events.cpp
  src/gamez/zeffect/zeff_anim_run.cpp
  src/gamez/zeffect/zeff_anim_save.cpp
  src/gamez/zeffect/zeff_init.cpp
  src/gamez/zimage/zimg_library.cpp
  src/gamez/zinterp/zinterp.cpp
  src/gamez/zinterp/zinterp_parse.cpp
  src/gamez/zimage/zimg_texture.cpp
  src/gamez/zmath/zmth_matrix.cpp
  src/gamez/zmath/zmth_vector.cpp
  src/gamez/zmodel/gmod_bake.cpp
  src/gamez/zmodel/gmod_clip2d.cpp
  src/gamez/zmodel/gmod_clip3d.cpp
  src/gamez/zmodel/gmod_const.cpp
  src/gamez/zmodel/gmod_cull.cpp
  src/gamez/zmodel/gmod_draw.cpp
  src/gamez/zmodel/gmod_fogcalc.cpp
  src/gamez/zmodel/gmod_fog.cpp
  src/gamez/zmodel/gmod_hit.cpp
  src/gamez/zmodel/gmod_init.cpp
  src/gamez/zmodel/gmod_light.cpp
  src/gamez/zmodel/gmod_matl.cpp
  src/gamez/zmodel/gmod_puff.cpp
  src/gamez/zmodel/gmod_screen.cpp
  src/gamez/zreader/zreader.cpp
  src/gamez/zsiscript/si_script.cpp
  src/gamez/zgame/zgameapp.cpp
  src/gamez/zsound/zsnd_cd.cpp
  src/gamez/zrender/zrndr_draw.cpp
  src/gamez/zrender/zrndr_flare.cpp
  src/gamez/zsound/zsnd_3d.cpp
  src/gamez/zsound/zsnd_create.cpp
  src/gamez/zsound/zsnd_grp.cpp
  src/gamez/zsound/zsnd_init.cpp
  src/gamez/zsound/zsnd_parm.cpp
  src/gamez/zsound/zsnd_play.cpp
  src/gamez/zweapon/zweapon.cpp
  src/gamez/zweapon/zwep_shot.cpp
  src/gamez/zweapon/zwep_beam.cpp
  src/gamez/zweapon/zwep_init.cpp
  src/gamez/zrender/zrndr_fog.cpp
  src/gamez/zrender/zrndr_span.cpp
  src/gamez/zrender/zrndr_tint.cpp
  src/gamez/zrender/zrndr_ripple.cpp
  src/gamez/zrender/zrndr_blur.cpp
  src/gamez/zfmv/fmv_script.cpp
  src/gamez/zfmv/fmv_stream.cpp
  src/gamez/zrender/zrndr_prim2d.cpp
  src/gamez/zrender/zrndr_blit.cpp
  src/gamez/ztwod/twod_console.cpp
  src/gamez/ztwod/twod_control.cpp
  src/gamez/ztwod/twod_dialog.cpp
  src/gamez/ztwod/twod_editbar.cpp
  src/gamez/ztwod/twod_shapes.cpp
  src/gamez/ztwod/twod_host.cpp
  src/gamez/ztwod/twod_list.cpp
  src/gamez/ztwod/twod_msgbox.cpp
  src/gamez/ztwod/twod_object.cpp
  src/gamez/ztwod/twod_weather.cpp
  src/gamez/ztwod/twod_textbox.cpp
  src/gamez/ztwod/twod_wintext.cpp
  src/mech3/mech.cpp
  src/mech3/mech3_interp.cpp
  src/mech3/controls_cfg.cpp
  src/mech3/mech_config.cpp
  src/mech3/mech_control.cpp
  src/mech3/mech_damage.cpp
  src/mech3/mech_repair.cpp
  src/mech3/mech_repair_store.cpp
  src/mech3/mech_data.cpp
  src/mech3/mech_lib.cpp
  src/mech3/mech_names.cpp
  src/mech3/mech_parts.cpp
  src/mech3/mech_lod.cpp
  src/mech3/mech_dead.cpp
  src/mech3/mech_part_update.cpp
  src/mech3/mech_wake.cpp
  src/mech3/sound_extra.cpp
  src/mech3/mech_pose.cpp
  src/mech3/mech_move.cpp
  src/mech3/mech_skis.cpp
  src/mech3/mech_build.cpp
  src/mech3/mech_part_build.cpp
  src/mech3/mech_update.cpp
  src/mech3/landmines.cpp
  src/mech3/entity_bind.cpp
  src/mech3/mech_collide.cpp
  src/mech3/mech_place.cpp
  src/mech3/mech_command.cpp
  src/mech3/lance_orders.cpp
  src/mech3/mech_pocket.cpp
  src/mech3/pocket_panel.cpp
  src/mech3/cockpit_sound.cpp
  src/mech3/mech_objective.cpp
  src/mech3/mech_shade.cpp
  src/mech3/veh_basic.cpp
  src/mech3/turret.cpp
  src/mech3/veh_basic_object.cpp
  src/mech3/mech_fieldbase.cpp
  src/mech3/mech_fieldbase_dock.cpp
  src/mech3/mech_fieldbase_cut.cpp
  src/mech3/mech_waynet.cpp
  src/mech3/veh_controls.cpp
  src/mech3/veh_move.cpp
  src/mech3/veh_stealth.cpp
  src/mech3/game_editor.cpp
  src/mech3/mech_anim.cpp
  src/mech3/mech_net.cpp
  src/mech3/mech_reader.cpp
  src/mech3/mech_salvage.cpp
  src/mech3/mech_skin.cpp
  src/mech3/mission.cpp
  src/mech3/mission_launch.cpp
  src/mech3/player.cpp
  src/mech3/player_view.cpp
  src/mech3/hud.cpp
  src/mech3/message_panel.cpp
  src/mech3/mech_hud.cpp
  src/mech3/app_cheats.cpp
  src/mech3/app_hud.cpp
  src/mech3/app_keys.cpp
  src/mech3/pilot.cpp
  src/mech3/pilot_games.cpp
  src/mech3/pilot_menu.cpp
  src/mech3/action_menu.cpp
  src/mech3/mp_connect.cpp
  src/mech3/mp_lobby.cpp
  src/mech3/chat_panel.cpp
  src/mech3/cheats.cpp
  src/mech3/briefing.cpp
  src/mech3/debriefing.cpp
  src/mech3/salvage_screen.cpp
  src/mech3/mission_map.cpp
  src/mech3/mission_world.cpp
  src/mech3/options.cpp
  src/mech3/weapon.cpp
  src/mech3/weapon_planner.cpp
  src/mech3/zai_params.cpp
  src/mech3/zai_target.cpp
  src/mech3/zai_anim_owner.cpp
  src/mech3/zai_list.cpp
  src/mech3/entity_classes.cpp
  src/mech3/sensors.cpp
  src/mech3/zai_valve.cpp
  src/mech3/zai_brain.cpp
  src/mech3/zai_steps.cpp
  src/mech3/zai_firing.cpp
  src/mech3/zai_attack.cpp
  src/mech3/zai_nav.cpp
  src/mech3/zai_repair.cpp
  src/mech3/zai_node.cpp
  src/mech3/zai_path.cpp
  src/mech3/zainet.cpp
  src/mech3/zainetedit.cpp
  src/mech3/zainet_inout.cpp
  src/mech3/zainet_region.cpp
  src/mech3/zainet_node.cpp
  src/mech3/zrdrainet.cpp
  src/mech3/zrdraiv.cpp
  src/mech3/zrdrvalve.cpp
  src/mech3/app.cpp
  src/mech3/app_hooks.cpp
  src/mech3/app_states.cpp
  src/mech3/mech_lab.cpp
  src/mech3/lab_view.cpp
  src/mech3/lab_screen.cpp
  src/mech3/app_engine.cpp
  src/mech3/app_force.cpp
  src/mech3/force_feedback.cpp
  src/mech3/app_fmv.cpp
  src/mech3/app_movie.cpp
  src/mech3/movie_shape.cpp
  src/mech3/movie_shape_table.cpp
  src/mech3/mods.cpp
  third_party/pl_mpeg/pl_mpeg.c
  src/mech3/app_mission.cpp
  src/mech3/app_hook_census.cpp
  src/mech3/app_turret.cpp
  src/mech3/app_vehicle.cpp
  src/mech3/app_view.cpp
  src/mech3/app_mech.cpp
  src/mech3/app_pocket.cpp
  src/mech3/app_weapons.cpp
  src/mech3/app_sensors.cpp
  src/mech3/app_menus.cpp
  src/mech3/mainmenu.cpp
  src/mech3/menu_widgets.cpp
  src/mech3/menu_states.cpp
  src/mech3/credits.cpp
  src/mech3/options_menu.cpp
  src/mech3/pause_menu.cpp
  src/mech3/quit_box.cpp
  src/mech3/lab_items.cpp
  src/mech3/controls_menu.cpp
  src/mech3/music.cpp
)
target_include_directories(mw3core PUBLIC src)
target_link_libraries(mw3core PUBLIC cc_geometry_resources)
if(MSVC)
  target_compile_options(mw3core PRIVATE /EHsc)
endif()
target_include_directories(mw3core PRIVATE third_party)
if(MSVC)
  target_compile_options(mw3core PRIVATE /EHsc)
endif()
# pl_mpeg (third_party/pl_mpeg): as written, its warnings not ours.
set_source_files_properties(third_party/pl_mpeg/pl_mpeg.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
# tinfl (third_party/miniz, miniz's inflate) likewise.
set_source_files_properties(third_party/miniz/miniz_tinfl.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
# ENet (third_party/enet, MIT): the multiplayer transport (docs/multiplayer-port.md),
# unmodified, its warnings not ours. unix.c's feature macros as its own build
# finds them on macOS and Linux (no gethostby*_r: it falls back).
add_library(enet STATIC
  third_party/enet/callbacks.c third_party/enet/compress.c third_party/enet/host.c third_party/enet/list.c
  third_party/enet/packet.c third_party/enet/peer.c third_party/enet/protocol.c third_party/enet/unix.c
  third_party/enet/win32.c)
target_include_directories(enet PUBLIC third_party/enet/include)
target_compile_options(enet PRIVATE "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
                             HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
endif()
# miniupnpc 2.2.8 (third_party/miniupnpc, BSD-3-Clause): the host's UDP port
# opened on the router over UPnP (docs/multiplayer-port.md, piece 9c),
# unmodified, its warnings not ours; its definitions as its own build sets
# them, its miniupnpcstrings.h made from its template.
set(_mw3_project_version "${PROJECT_VERSION}")
set(PROJECT_VERSION 2.2.8)
configure_file(third_party/miniupnpc/miniupnpcstrings.h.cmake ${CMAKE_BINARY_DIR}/miniupnpc/miniupnpcstrings.h)
set(PROJECT_VERSION "${_mw3_project_version}")
add_library(miniupnpc STATIC
  third_party/miniupnpc/src/igd_desc_parse.c third_party/miniupnpc/src/miniupnpc.c
  third_party/miniupnpc/src/minixml.c third_party/miniupnpc/src/minisoap.c third_party/miniupnpc/src/minissdpc.c
  third_party/miniupnpc/src/miniwget.c third_party/miniupnpc/src/upnpcommands.c third_party/miniupnpc/src/upnpdev.c
  third_party/miniupnpc/src/upnpreplyparse.c third_party/miniupnpc/src/upnperrors.c
  third_party/miniupnpc/src/connecthostport.c third_party/miniupnpc/src/portlistingparse.c
  third_party/miniupnpc/src/receivedata.c third_party/miniupnpc/src/addr_is_reserved.c)
target_include_directories(miniupnpc PUBLIC third_party/miniupnpc/include
                           PRIVATE third_party/miniupnpc/src ${CMAKE_BINARY_DIR}/miniupnpc)
target_compile_definitions(miniupnpc PUBLIC MINIUPNP_STATICLIB)
target_compile_options(miniupnpc PRIVATE "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
if(WIN32)
  target_compile_definitions(miniupnpc PRIVATE _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_link_libraries(miniupnpc PUBLIC ws2_32 iphlpapi)
else()
  target_compile_definitions(miniupnpc PRIVATE MINIUPNPC_SET_SOCKET_TIMEOUT MINIUPNPC_GET_SRC_ADDR _BSD_SOURCE _DEFAULT_SOURCE)
  if(APPLE)
    target_compile_definitions(miniupnpc PRIVATE _DARWIN_C_SOURCE)
  else()
    target_compile_definitions(miniupnpc PRIVATE _XOPEN_SOURCE=600)
  endif()
endif()
# SETUP's archives (docs/setup-sources.md): libarchive with zlib and liblzma.
# The release's are static, from tools/deps (build_deps.sh, the PC's
# tools/windows/build_deps_archive.cmd into deps\archive); a development
# Mac may use Homebrew's (brew install libarchive). Without it the game
# builds and SETUP takes folders, drives, .iso and .cue only.
find_path(LIBARCHIVE_INCLUDE archive.h
          HINTS "${CMAKE_SOURCE_DIR}/deps/archive/include" /opt/homebrew/opt/libarchive/include /usr/local/opt/libarchive/include)
find_library(LIBARCHIVE_LIB NAMES archive archive_static
             HINTS "${CMAKE_SOURCE_DIR}/deps/archive/lib" /opt/homebrew/opt/libarchive/lib /usr/local/opt/libarchive/lib)
if(LIBARCHIVE_INCLUDE AND LIBARCHIVE_LIB)
  target_include_directories(mw3core PUBLIC "${LIBARCHIVE_INCLUDE}")
  target_link_libraries(mw3core PUBLIC "${LIBARCHIVE_LIB}")
  if(LIBARCHIVE_LIB MATCHES "\\.(a|lib)$")  # static: its own libraries beside it
    target_compile_definitions(mw3core PUBLIC LIBARCHIVE_STATIC)  # the tests write archives with it too
    find_library(MW3_LZMA_LIB NAMES lzma liblzma HINTS "${CMAKE_SOURCE_DIR}/deps/archive/lib")
    find_library(MW3_Z_LIB NAMES z zlibstatic zlib zs HINTS "${CMAKE_SOURCE_DIR}/deps/archive/lib")
    target_link_libraries(mw3core PUBLIC ${MW3_LZMA_LIB} ${MW3_Z_LIB})
    if(WIN32)
      target_link_libraries(mw3core PUBLIC bcrypt)
    endif()
  endif()
  target_compile_definitions(mw3core PUBLIC MW3_HAVE_LIBARCHIVE=1)
  message(STATUS "libarchive: ${LIBARCHIVE_LIB}")
else()
  target_compile_definitions(mw3core PUBLIC MW3_HAVE_LIBARCHIVE=0)
  message(STATUS "libarchive not found: SETUP reads folders, drives, .iso and .cue only")
endif()

# The x87 never fuses a multiply and an add; clang does by default on arm64,
# which rounds differently from the original (CLAUDE.md, FPU precision).
if(NOT MSVC)
  target_compile_options(mw3core PRIVATE -ffp-contract=off)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")  # clang-cl
  target_compile_options(mw3core PRIVATE /clang:-ffp-contract=off)
endif()

# Linked into the shared probe below: position-independent (the Mac's default;
# GCC on Linux needs it asked for, docs/linux.md).
set_target_properties(mw3core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# A C interface over a few ported functions, for the differential tests that run
# the original x86 code in an emulator and compare (tests/differential).
add_library(mw3probe SHARED
  tests/probe.cpp
  tests/probe_clip3d.cpp
  tests/probe_clip2d.cpp
  tests/probe_fogsort.cpp
  tests/probe_lighting.cpp
  tests/probe_drawtable.cpp
  tests/probe_matrix.cpp
  tests/probe_screen.cpp
  tests/probe_ripple.cpp
  tests/probe_cycle.cpp
  tests/probe_cockpit_sound.cpp
  tests/probe_objectives.cpp
  tests/probe_shade.cpp
  tests/probe_skins.cpp
  tests/probe_world.cpp
  tests/probe_modeldraw.cpp
  tests/probe_bake.cpp
  tests/probe_texload.cpp
  tests/probe_scenewalk.cpp
  tests/probe_damage.cpp
  tests/probe_interp.cpp
  tests/probe_app.cpp
  tests/probe_options.cpp
  tests/probe_states.cpp
  tests/probe_shadows.cpp
  tests/probe_shadow_image.cpp
  tests/probe_puffers.cpp
  tests/probe_move.cpp
  tests/probe_skis.cpp
  tests/probe_jets.cpp
  tests/probe_actions.cpp
  tests/probe_sound.cpp
  tests/probe_weapons.cpp
  tests/probe_weapons_run.cpp
  tests/probe_weapons_hit.cpp
  tests/probe_weapons_fly.cpp
  tests/probe_twod.cpp
  tests/probe_mainmenu.cpp
  tests/probe_pocket_panel.cpp
  tests/probe_menus.cpp
  tests/probe_lab_list.cpp
  tests/probe_ctlscreen.cpp
  tests/probe_loadsave.cpp
  tests/probe_pilot.cpp
  tests/probe_zsave.cpp
  tests/probe_pilotscreen.cpp
  tests/probe_actionscreen.cpp
  tests/probe_briefing.cpp
  tests/probe_debriefing.cpp
  tests/probe_salvage_screen.cpp
  tests/probe_lab.cpp
  tests/probe_salvage.cpp
  tests/probe_update.cpp
  tests/probe_di.cpp
  tests/probe_inout.cpp
  tests/probe_place.cpp
  tests/probe_shotq.cpp
  tests/probe_command.cpp
  tests/probe_lance.cpp
  tests/probe_pocket.cpp
  tests/probe_entities.cpp
  tests/probe_collide.cpp
  tests/probe_anim_run.cpp
  tests/probe_action_lists.cpp
  tests/probe_mechanim.cpp
  tests/probe_pose.cpp
  tests/probe_parts.cpp
  tests/probe_controls.cpp
  tests/probe_ctlcfg.cpp
  tests/probe_ai.cpp
  tests/probe_zdec.cpp
  tests/probe_zgeo.cpp
  tests/probe_weiler.cpp
  tests/probe_convexify.cpp
  tests/probe_gmod_add.cpp
  tests/probe_ski.cpp
  tests/probe_crater.cpp
  tests/probe_sound_extra.cpp
  tests/probe_deaths.cpp
  tests/probe_ai_net.cpp
  tests/probe_zinput.cpp
  tests/probe_settings.cpp
  tests/probe_mission.cpp
  tests/probe_player.cpp
  tests/probe_mech_build.cpp
  tests/probe_veh_basic.cpp
  tests/probe_veh_fieldbase.cpp
  tests/probe_veh_stealth.cpp
  tests/probe_veh_dock.cpp
  tests/probe_veh_waynet.cpp
  tests/probe_mech_repair.cpp
  tests/probe_mech_repair_store.cpp
  tests/probe_field_cut.cpp
  tests/probe_veh_controls.cpp
  tests/probe_ai_brain.cpp
  tests/probe_ai_orders.cpp
  tests/probe_ai_valve.cpp
  tests/probe_ai_target.cpp
  tests/probe_turret.cpp
  tests/probe_sensors.cpp
  tests/probe_anim_owner.cpp
  tests/probe_ai_lists.cpp
  tests/probe_glow.cpp
  tests/probe_camera_zone.cpp
  tests/probe_landmines.cpp
  tests/probe_entity_bind.cpp
  tests/probe_flares.cpp
  tests/probe_weather.cpp
  tests/probe_tint.cpp
  tests/probe_decal.cpp
  tests/probe_static.cpp
  tests/probe_entity_classes.cpp
  tests/probe_model_fx.cpp
  tests/probe_mix_fog.cpp
  tests/probe_gfx_flags.cpp
  tests/probe_fade.cpp
  tests/probe_fmv.cpp
  tests/probe_znet.cpp
  tests/probe_mech_net.cpp
  tests/probe_frame.cpp
  tests/probe_view.cpp
  tests/probe_hud.cpp
  tests/probe_mech_data.cpp
)
target_link_libraries(mw3probe PRIVATE mw3core)

enable_testing()
find_package(Threads REQUIRED)
add_executable(mw3render_worker_checks tests/test_render_worker.cpp)
target_include_directories(mw3render_worker_checks PRIVATE src)
target_link_libraries(mw3render_worker_checks PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(mw3render_worker_checks PRIVATE /EHsc)
endif()
add_test(NAME mw3render_worker_checks COMMAND mw3render_worker_checks)
set_tests_properties(mw3render_worker_checks PROPERTIES TIMEOUT 15)
add_executable(mw3prepared_checks tests/test_prepared_visuals.cpp)
target_include_directories(mw3prepared_checks PRIVATE third_party)
target_link_libraries(mw3prepared_checks PRIVATE mw3core)
if(MSVC)
  target_compile_options(mw3prepared_checks PRIVATE /EHsc)
endif()
add_test(NAME mw3prepared_checks COMMAND mw3prepared_checks)
add_executable(mw3tests
  tests/main.cpp
  tests/test_znet.cpp
  tests/test_cue.cpp
  tests/test_mech.cpp
  tests/test_mech_reader.cpp
  tests/test_msvc_rand.cpp
  tests/test_ztimer.cpp
  tests/test_zgameapp.cpp
  tests/test_app.cpp
  tests/test_mainmenu.cpp
  tests/test_menus.cpp
  tests/test_zsnd_cd.cpp
  tests/test_zrndr_span.cpp
  tests/test_fixes.cpp
  tests/test_lance.cpp
  tests/test_nav.cpp
  tests/test_setup_sources.cpp
  tests/test_data_folder.cpp
  tests/test_patch12.cpp
  tests/test_video_device.cpp
  tests/test_resolution.cpp
  tests/test_zreader.cpp
  tests/test_controls_cfg.cpp
  tests/test_pilot_names.cpp
  tests/test_program.cpp
  tests/test_editor.cpp
  tests/test_movie_shape.cpp
  tests/test_fmv_stream.cpp
  tests/test_mods.cpp
  tests/test_bounded_readers.cpp
  tests/test_bounded_media.cpp
  tests/test_hooks.cpp
  tests/place_survey.cpp
)
target_link_libraries(mw3tests PRIVATE mw3core)
# Tests that read the player's install skip themselves when it is not there.
target_compile_definitions(mw3tests PRIVATE MW3_GAME_DIR="${MW3_GAME_DIR}"
                                          MW3_TEST_TMP_ROOT="${CMAKE_BINARY_DIR}"
                                          MW3_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME mw3tests COMMAND mw3tests)
add_executable(mw3geometry_checks tests/test_geometry_handoff.cpp)
target_link_libraries(mw3geometry_checks PRIVATE mw3core)
if(MSVC)
  target_compile_options(mw3geometry_checks PRIVATE /EHsc)
endif()
add_test(NAME mw3geometry_checks COMMAND mw3geometry_checks)

# The SDL3 front end and the programs on it.
option(MW3_CORE_CRITICAL_VULKAN "Build the experimental Core Critical Vulkan/DXGI renderer" OFF)
find_package(SDL3 CONFIG QUIET)
if(MW3_CORE_CRITICAL_VULKAN)
  if(NOT WIN32 OR NOT SDL3_FOUND)
    message(FATAL_ERROR "The Core Critical Vulkan renderer requires Windows x64 and SDL3")
  endif()
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("#if !defined(_M_X64) && !defined(__x86_64__)\n#error x64 required\n#endif\nint main() {}" MW3_CC_TARGET_X64)
  if(NOT MW3_CC_TARGET_X64)
    message(FATAL_ERROR "The Core Critical Vulkan renderer requires an x64 target")
  endif()
endif()
if(SDL3_FOUND)
  # Platform implementations on SDL (docs/program.md): window and message
  # pump, input, render3d, sound, music; the debug world loader.
  add_library(mw3sdl STATIC
    src/frontend/debug_world.cpp
    src/frontend/sdl/sdl_input.cpp
    src/frontend/sdl/sdl_force.cpp
    src/frontend/sdl/sdl_install.cpp
    src/frontend/sdl/sdl_music.cpp
    src/frontend/sdl/sdl_platform.cpp
    src/frontend/sdl/gpu_hud.cpp
    src/frontend/sdl/sdl_render3d.cpp
    src/frontend/sdl/sdl_sound.cpp
    src/frontend/sdl/sdl_vfw.cpp
    src/frontend/sdl/net_enet.cpp
    src/frontend/sdl/net_upnp.cpp
    src/frontend/sdl/sdl_child.cpp
  )
  target_link_libraries(mw3sdl PUBLIC mw3core SDL3::SDL3 enet miniupnpc)
  if(MSVC)
    target_compile_options(mw3sdl PRIVATE /EHsc)
  endif()
  if(MW3_CORE_CRITICAL_VULKAN)
    add_subdirectory(src/frontend/core_critical)
    target_link_libraries(mw3sdl PRIVATE mw3ccvulkan)
    target_compile_definitions(mw3sdl PRIVATE CC_MW3_RE_ADAPTER=1)
  endif()
  # Windows: the Direct3D 12 shaders (sdl_render3d.hlsl), DXIL by the
  # Windows SDK's dxc, as headers the renderer includes.
  if(WIN32)
    find_program(DXC dxc REQUIRED)
    set(hlsl ${CMAKE_SOURCE_DIR}/src/frontend/sdl/sdl_render3d.hlsl)
    set(dxil_dir ${CMAKE_BINARY_DIR}/dxil)
    set(dxil_headers)
    foreach(stage vs:vs_6_0 fs_tex:ps_6_0 fs_flat:ps_6_0)
      string(REPLACE ":" ";" pair ${stage})
      list(GET pair 0 entry)
      list(GET pair 1 profile)
      add_custom_command(OUTPUT ${dxil_dir}/${entry}.h
                         COMMAND ${CMAKE_COMMAND} -E make_directory ${dxil_dir}
                         COMMAND ${DXC} -nologo -T ${profile} -E ${entry} -Fh ${dxil_dir}/${entry}.h -Vn kDxil_${entry} ${hlsl}
                         DEPENDS ${hlsl} VERBATIM)
      list(APPEND dxil_headers ${dxil_dir}/${entry}.h)
    endforeach()
    target_sources(mw3sdl PRIVATE ${dxil_headers})
    target_include_directories(mw3sdl PRIVATE ${dxil_dir})
    target_compile_definitions(mw3sdl PRIVATE MW3_HAVE_DXIL=1)
  endif()
  # Vulkan (Linux; anywhere glslangValidator is found): the shaders'
  # GLSL twin (sdl_render3d.glsl), SPIR-V by Khronos's glslangValidator, as
  # headers of uint32_t words. Linux has no other GPU format, so there it
  # is required.
  if(UNIX AND NOT APPLE)
    find_program(GLSLANG glslangValidator REQUIRED)
  else()
    find_program(GLSLANG glslangValidator)
  endif()
  if(GLSLANG)
    set(glsl ${CMAKE_SOURCE_DIR}/src/frontend/sdl/sdl_render3d.glsl)
    set(spv_dir ${CMAKE_BINARY_DIR}/spirv)
    set(spv_headers)
    foreach(stage vs:vert:VS fs_tex:frag:FS_TEX fs_flat:frag:FS_FLAT)
      string(REPLACE ":" ";" parts ${stage})
      list(GET parts 0 entry)
      list(GET parts 1 kind)
      list(GET parts 2 define)
      add_custom_command(OUTPUT ${spv_dir}/spv_${entry}.h
                         COMMAND ${CMAKE_COMMAND} -E make_directory ${spv_dir}
                         COMMAND ${GLSLANG} -V -S ${kind} -D${define} --quiet --vn kSpv_${entry} -o ${spv_dir}/spv_${entry}.h ${glsl}
                         DEPENDS ${glsl} VERBATIM)
      list(APPEND spv_headers ${spv_dir}/spv_${entry}.h)
    endforeach()
    target_sources(mw3sdl PRIVATE ${spv_headers})
    target_include_directories(mw3sdl PRIVATE ${spv_dir})
    target_compile_definitions(mw3sdl PRIVATE MW3_HAVE_SPIRV=1)
  endif()
  # Music files other than .wav decode with SDL3_sound (brew install sdl3_sound;
  # Windows: its -devel-VC.zip in deps\, found through SDL3_sound_ROOT).
  find_package(SDL3_sound CONFIG QUIET)
  if(SDL3_sound_FOUND)
    target_link_libraries(mw3sdl PUBLIC SDL3_sound::SDL3_sound)
    target_compile_definitions(mw3sdl PRIVATE MW3_HAVE_SDL_SOUND=1)
  else()
    message(STATUS "SDL3_sound not found: music plays .wav tracks only")
  endif()
  # The game, and its front end's tests: the 2D text needs SDL3_ttf.
  find_package(SDL3_ttf CONFIG QUIET)
  if(SDL3_ttf_FOUND)
    add_library(mw3text STATIC src/frontend/sdl/sdl_twod_host.cpp src/frontend/sdl/gdi_font_size.cpp
                src/frontend/sdl/text_fit.cpp src/frontend/sdl/launcher_notes.cpp src/frontend/sdl/movie_import.cpp)
    target_link_libraries(mw3text PUBLIC mw3sdl SDL3_ttf::SDL3_ttf)
    add_executable(mw3
      src/frontend/sdl/mw3_main.cpp
      src/frontend/sdl/run_log.cpp
      src/frontend/sdl/crash_report.cpp
      src/frontend/sdl/launcher.cpp
      src/frontend/sdl/debug_states.cpp
      src/frontend/sdl/campaign_chain.cpp
      src/frontend/sdl/modes_chain.cpp
      src/frontend/sdl/first_run_import.cpp
    )
    target_link_libraries(mw3 PRIVATE mw3text mw3sdl mw3core SDL3::SDL3)
    if(MSVC)
      target_compile_options(mw3text PRIVATE /EHsc)
      target_compile_options(mw3 PRIVATE /EHsc)
    endif()
    if(MW3_CORE_CRITICAL_VULKAN)
      target_compile_definitions(mw3 PRIVATE CC_MW3_RE_ADAPTER=1)
    endif()
    target_compile_definitions(mw3 PRIVATE MW3_VERSION="${MW3_VERSION}")
    # The launcher's art (assets/launcher) in a development build; a package
    # carries it beside the exe (tools/windows/package.cmd) or in the app's
    # Resources (tools/macos/make_app.sh), and keeps no path of this machine.
    if(NOT MW3_PACKAGE)
      target_compile_definitions(mw3 PRIVATE MW3_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
      target_compile_definitions(mw3 PRIVATE MW3_GAME_DIR="${MW3_GAME_DIR}")
    endif()
    # Windows: the version resource and our placeholder icon (tools/windows).
    if(WIN32)
      set(MW3_RC_NAME "MechWarrior 3 rebuild")
      set(MW3_RC_FILE "mw3.exe")
      set(MW3_RC_ICON "ICON_MAIN ICON \"${CMAKE_SOURCE_DIR}/tools/windows/mw3.ico\"")
      configure_file(tools/windows/version.rc.in ${CMAKE_BINARY_DIR}/mw3.rc)
      target_sources(mw3 PRIVATE ${CMAKE_BINARY_DIR}/mw3.rc)
    endif()
    # A package build is a Windows GUI program: no console window beside the
    # game, its output in the write dir's mw3.log (mw3_main.cpp); `main` stays
    # the entry. The development build keeps its console.
    if(WIN32 AND MW3_PACKAGE)
      if(MSVC)
        target_link_options(mw3 PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
      else()
        target_link_options(mw3 PRIVATE -mwindows)
      endif()
    endif()
    # Windows: the DLLs mw3.exe links (SDL3, SDL3_ttf, SDL3_sound) copied beside it.
    if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
      add_custom_command(TARGET mw3 POST_BUILD
                         COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:mw3> $<TARGET_FILE_DIR:mw3>
                         COMMAND_EXPAND_LISTS)
    endif()
    # The launcher (docs/launcher.md, The two programs; games-layout.md): the
    # main icon, its own program; MECHWARRIOR 3 starts mw3 beside it.
    add_executable(mw3launcher
      src/frontend/sdl/launcher_main.cpp
      src/frontend/sdl/launcher.cpp
      src/frontend/sdl/run_log.cpp
      src/frontend/sdl/crash_report.cpp
    )
    target_link_libraries(mw3launcher PRIVATE mw3text mw3sdl mw3core SDL3::SDL3)
    target_compile_definitions(mw3launcher PRIVATE MW3_VERSION="${MW3_VERSION}")
    if(NOT MW3_PACKAGE)
      target_compile_definitions(mw3launcher PRIVATE MW3_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
                                                     MW3_GAME_DIR="${MW3_GAME_DIR}")
    endif()
    add_dependencies(mw3launcher mw3)
    if(WIN32)
      set(MW3_RC_NAME "MW3 Rebuild")
      set(MW3_RC_FILE "mw3launcher.exe")
      set(MW3_RC_ICON "ICON_MAIN ICON \"${CMAKE_SOURCE_DIR}/tools/windows/mw3.ico\"")
      configure_file(tools/windows/version.rc.in ${CMAKE_BINARY_DIR}/mw3launcher.rc)
      target_sources(mw3launcher PRIVATE ${CMAKE_BINARY_DIR}/mw3launcher.rc)
      # No console, in every build: it is only ever a window (its log is
      # mw3launcher.log in the write dir).
      if(MSVC)
        target_link_options(mw3launcher PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
      else()
        target_link_options(mw3launcher PRIVATE -mwindows)
      endif()
      if(NOT CMAKE_VERSION VERSION_LESS 3.21)
        add_custom_command(TARGET mw3launcher POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:mw3launcher> $<TARGET_FILE_DIR:mw3launcher>
                           COMMAND_EXPAND_LISTS)
      endif()
    endif()
    # The Indeo helper's stand-in for the tests (tools/indeo; the real
    # mw3indeo.exe is 32-bit Windows, built by tools/windows/build.cmd).
    add_executable(mw3indeo_fake tools/indeo/mw3indeo_fake.cpp)
    target_link_libraries(mw3indeo_fake PRIVATE mw3core)
    add_executable(mw3sdltests
      tests/main.cpp
      tests/test_sdl_frontend.cpp
      tests/test_net_enet.cpp
      tests/test_net_upnp.cpp
    )
    target_link_libraries(mw3sdltests PRIVATE mw3text mw3sdl mw3core SDL3::SDL3)
    add_executable(mw3hud_checks tests/test_gpu_hud.cpp)
    target_link_libraries(mw3hud_checks PRIVATE mw3text mw3sdl mw3core SDL3::SDL3)
    if(MSVC)
      target_compile_options(mw3hud_checks PRIVATE /EHsc)
    endif()
    add_test(NAME mw3hud_checks COMMAND mw3hud_checks)
    if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
      add_custom_command(TARGET mw3hud_checks POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:mw3hud_checks> $<TARGET_FILE_DIR:mw3hud_checks>
        COMMAND_EXPAND_LISTS)
    endif()
    target_compile_definitions(mw3sdltests PRIVATE MW3_GAME_DIR="${MW3_GAME_DIR}"
                                                 MW3_TEST_TMP_ROOT="${CMAKE_BINARY_DIR}"
                                                 MW3_BIN="$<TARGET_FILE:mw3>"
                                                 MW3_LAUNCHER_BIN="$<TARGET_FILE:mw3launcher>"
                                                 MW3_FAKE_INDEO="$<TARGET_FILE:mw3indeo_fake>"
                                                 MW3_IMPORT_BIN="$<TARGET_FILE_DIR:mw3>/mw3import")
    add_dependencies(mw3sdltests mw3indeo_fake)
    add_dependencies(mw3sdltests mw3)
    add_dependencies(mw3sdltests mw3launcher)
    add_test(NAME mw3sdltests COMMAND mw3sdltests)
  else()
    message(STATUS "SDL3_ttf not found: mw3 is not built (brew install sdl3_ttf)")
  endif()
  # Development tools (not part of the port): the world viewer.
  add_executable(mw3view tools/viewer/viewer.cpp tools/viewer/scene.cpp)
  target_link_libraries(mw3view PRIVATE mw3core mw3sdl SDL3::SDL3)
  target_include_directories(mw3view PRIVATE tools/viewer)
  target_compile_definitions(mw3view PRIVATE MW3_GAME_DIR="${MW3_GAME_DIR}")
  # The importer's movies (docs/fmv.md, The imported movies): only with FFmpeg's
  # libraries (Indeo 5 decoding, the MPEG-1 encoder); not needed to build the game.
  # pkg-config's (brew install ffmpeg), else a prebuilt shared build's include\
  # and lib\ (Windows: deps\ffmpeg-*-shared*, or -DFFMPEG_DIR; docs/windows.md).
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
  endif()
  set(MW3_FFMPEG "")
  if(FFMPEG_FOUND)
    set(MW3_FFMPEG PkgConfig::FFMPEG)
  else()
    if(NOT FFMPEG_DIR)
      file(GLOB _ffmpeg_dirs "${CMAKE_SOURCE_DIR}/deps/ffmpeg-*shared*")
      if(_ffmpeg_dirs)
        list(GET _ffmpeg_dirs 0 FFMPEG_DIR)
      endif()
    endif()
    if(FFMPEG_DIR AND EXISTS "${FFMPEG_DIR}/include/libavformat/avformat.h")
      add_library(mw3_ffmpeg INTERFACE)
      target_include_directories(mw3_ffmpeg INTERFACE "${FFMPEG_DIR}/include")
      foreach(_lib avformat avcodec swscale avutil)
        find_library(_ffmpeg_${_lib} NAMES ${_lib} PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
        target_link_libraries(mw3_ffmpeg INTERFACE ${_ffmpeg_${_lib}})
      endforeach()
      set(MW3_FFMPEG mw3_ffmpeg)
      file(GLOB MW3_FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
    endif()
  endif()
  if(MW3_FFMPEG)
    add_executable(mw3import tools/import/mw3import.cpp)
    target_link_libraries(mw3import PRIVATE mw3core ${MW3_FFMPEG} SDL3::SDL3)
    # FFmpeg's headers by -I, not -isystem: a stray FFmpeg in /usr/local/include
    # (another major version) otherwise wins over them and the structs disagree.
    set_target_properties(mw3import PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
    target_compile_definitions(mw3import PRIVATE MW3_VERSION="${MW3_VERSION}")
    if(NOT MW3_PACKAGE)
      target_compile_definitions(mw3import PRIVATE MW3_GAME_DIR="${MW3_GAME_DIR}")
    endif()
    if(WIN32)
      set(MW3_RC_NAME "MechWarrior 3 rebuild: movie and music importer")
      set(MW3_RC_FILE "mw3import.exe")
      set(MW3_RC_ICON "")
      configure_file(tools/windows/version.rc.in ${CMAKE_BINARY_DIR}/mw3import.rc)
      target_sources(mw3import PRIVATE ${CMAKE_BINARY_DIR}/mw3import.rc)
    endif()
    if(MW3_FFMPEG_DLLS)  # the DLLs beside mw3import.exe, as SDL3.dll is beside mw3.exe
      add_custom_command(TARGET mw3import POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MW3_FFMPEG_DLLS}
                         $<TARGET_FILE_DIR:mw3import>)
    endif()
  else()
    message(STATUS "FFmpeg's libraries not found: mw3import is not built (brew install ffmpeg; Windows: docs/windows.md)")
  endif()
  # Plays one game sound through zSound and SDL audio (docs/sound.md).
  add_executable(mw3soundcheck tools/soundcheck/soundcheck.cpp)
  target_link_libraries(mw3soundcheck PRIVATE mw3core mw3sdl SDL3::SDL3)
  target_compile_definitions(mw3soundcheck PRIVATE MW3_GAME_DIR="${MW3_GAME_DIR}")
endif()
