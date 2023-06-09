#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "renderers/GLUtils.h"

struct Lights
{
	int num_directional_lights = 0;
	uint64_t hash_directional_lights = 0;
	std::unique_ptr<GLDynBuffer> constant_directional_lights;

	int num_directional_shadows = 0;
	uint64_t hash_directional_shadows = 0;
	std::unique_ptr<GLDynBuffer> constant_directional_shadows;

	std::vector<unsigned> directional_shadow_texs;
	
};

