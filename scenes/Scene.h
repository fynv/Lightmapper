#pragma once

#include <unordered_set>

#include "core/Object3D.h"
#include "lights/Lights.h"

class Background;
class SimpleModel;
class GLTFModel;
class DirectionalLight;
class Scene : public Object3D
{
public:
	Background* background = nullptr;	
	Lights lights;

	// pre-render
	std::vector<SimpleModel*> simple_models;
	std::vector<GLTFModel*> gltf_models;
	std::vector<DirectionalLight*> directional_lights;

	void clear_lists()
	{
		simple_models.clear();
		gltf_models.clear();
		directional_lights.clear();
	}

	void get_bounding_box(glm::vec3& min_pos, glm::vec3& max_pos, const glm::mat4& view_matrix = glm::identity<glm::mat4>());
};

