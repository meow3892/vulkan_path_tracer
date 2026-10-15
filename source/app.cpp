#include "app.hpp"

#include <iostream>

// Gpu calls SDL_Vulkan_GetInstanceExtensions which should be called after creating a window with SDL_WINDOW_VULKAN
// so we put window_ in initializer list to create before gpu_ constructor
App::App() : window_(SDL_CreateWindow("vulkan_path_tracer", 1920, 1080, SDL_WINDOW_VULKAN))
{
  if (!window_)
  {
    std::cerr << "SDL create window failed: " << SDL_GetError() << '\n';
  }
}

App::~App()
{
  SDL_DestroyWindow(window_);
}