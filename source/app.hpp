#pragma once

#include "gpu.hpp"

#include <SDL3/SDL_video.h>

struct App
{
  App();
  ~App();

  SDL_Window *window_;
  Gpu gpu_;
};