#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.hpp"

#include <iostream>

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
  if (!SDL_Init(SDL_INIT_VIDEO))
  {
    std::cerr << "SDL init failed: " << SDL_GetError() << '\n';
  }

  *appstate = new App();

  std::cout << static_cast<App *>(*appstate)->gpu_.getPhysicalDevice().getProperties().deviceName.data() << '\n';
  std::cout << "Graphics queue index: " << static_cast<App *>(*appstate)->gpu_.getGraphicsQueueIndex() << '\n';

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
  if (event->type == SDL_EVENT_QUIT)
  {
    return SDL_APP_SUCCESS;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
  delete static_cast<App *>(appstate);
}