#pragma once

#include "asset_manager.hpp"
#include "gpu.hpp"

#include <SDL3/SDL_video.h>

struct App
{
  App();
  ~App();

  SDL_Window *window_;
  Gpu gpu_;
  AssetManager assetManager{gpu_};
};