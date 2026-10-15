#include "app.hpp"

#include <iostream>

App::App()
{
  window_ = SDL_CreateWindow("vulkan_path_tracer", 1920, 1080, 0);
  if (!window_)
  {
    std::cerr << "SDL create window failed: " << SDL_GetError() << '\n';
  }
}

App::~App()
{
  SDL_DestroyWindow(window_);
}