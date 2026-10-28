#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.hpp"

#include <iostream>

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
  try
  {
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
      throw std::runtime_error(std::string("SDL init failed: ") + SDL_GetError());
    }

    *appstate = new App();
    auto app = static_cast<App *>(*appstate);

    auto cornellBox = app->assetManager.uploadGltf("asset/cornell_box.gltf");
    app->assetManager.buildBlases();

    std::array instances{
        Instance{.mesh = cornellBox, .transform = glm::mat4(1.0f)},
    };
    app->pathTracer.buildTlas(instances);

    app->pathTracer.setCamera({
        .position = glm::vec3(0.0f, 1.0f, 3.5f),
        .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        .fov = glm::radians(45.0f),
        .near = 0.1f,
        .far = 1000.0f,
    });
  }
  catch (const std::exception &exception)
  {
    std::cerr << exception.what() << '\n';
    return SDL_APP_FAILURE;
  }

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
  auto app = static_cast<App *>(appstate);
  if (event->type == SDL_EVENT_QUIT)
  {
    return SDL_APP_SUCCESS;
  }
  if (event->type == SDL_EVENT_WINDOW_MINIMIZED)
  {
    app->minimized = true;
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "waitevent");
  }
  if (event->type == SDL_EVENT_WINDOW_RESTORED)
  {
    app->minimized = false;
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "0");
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
  auto app = static_cast<App *>(appstate);
  if (app->minimized)
  {
    return SDL_APP_CONTINUE;
  }

  try
  {
    app->pathTracer.draw();
  }
  catch (const std::exception &exception)
  {
    std::cerr << exception.what() << '\n';
    return SDL_APP_FAILURE;
  }

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
  delete static_cast<App *>(appstate);
}