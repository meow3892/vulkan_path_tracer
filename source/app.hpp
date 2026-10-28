#pragma once

#include "asset_manager.hpp"
#include "gpu.hpp"
#include "path_tracer.hpp"

#include <SDL3/SDL_video.h>

struct App
{
  App();
  ~App();

  SDL_Window *window_;
  Gpu gpu_{window_};
  AssetManager assetManager{gpu_};
  PathTracer pathTracer{window_, gpu_, assetManager};
  bool minimized = false;
};