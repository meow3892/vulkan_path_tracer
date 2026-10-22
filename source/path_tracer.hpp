#pragma once

#include "gpu.hpp"

#include <SDL3/SDL_video.h>

class PathTracer
{
public:
  PathTracer(SDL_Window *window, const Gpu &gpu);

private:
  SDL_Window *window_;
  const Gpu &gpu_;
  vma::raii::Buffer cameraBuffer_;
  vk::raii::Sampler hdrTargetSampler_;
  vma::raii::Image hdrTargetImage_{nullptr};
  vk::raii::ImageView hdrTargetImageView_{nullptr};

  void createHdrTarget();
};