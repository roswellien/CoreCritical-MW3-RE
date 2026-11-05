#pragma once

#include <memory>

#include "frontend/sdl/sdl_render3d.h"

namespace cc_mw3_re {

std::unique_ptr<sdlfe::FrameRenderer> make_renderer(SDL_Window *window);

} // namespace cc_mw3_re
