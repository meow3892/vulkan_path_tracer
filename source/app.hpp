#pragma once

#include <SDL3/SDL_video.h>

struct App
{
  App();
  ~App();

  SDL_Window *window_;
};