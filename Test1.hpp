#pragma once 

#include "scenes/Scene.h"
#include "cameras/PerspectiveCamera.h"
#include "backgrounds/Background.h"
#include "models/GLTFModel.h"
#include "loaders/GLTFLoader.h"
#include "lights/DirectionalLight.h"
#include "renderers/GLRenderer.h"
#include "renderers/GLRenderTarget.h"
#include "renderers/LightmapRenderTarget.h"


class Test
{
public:
	Scene scene;
	PerspectiveCamera camera;

	ColorBackground background;

	GLTFModel model;

	int idx_texel = 0;	
	int iter = 0;
	int iterations = 6;

	double check_time;

	GLRenderer renderer;
	GLRenderTarget render_target;
	Test(int width, int height);

	void Draw(int width, int height);

	bool mouse_down = false;
	bool recieve_delta = false;
	glm::vec2 delta_acc;
	glm::quat start_rot;
	void set_mouse_down(bool down);
	void rotation_delta(double dx, double dy);
	void move(int dx, int dz);

};

Test::Test(int width, int height)
	: camera(45.0f, (float)width / (float)height, 0.1f, 100.0f)
	, render_target(true, true)
{
	camera.position = { 3.0f, 1.5f, -3.0f };
	camera.rotateY(3.14159f);
	
	background.color = glm::vec3(0.8f, 0.8f, 0.8f);
	scene.background = &background;

	GLTFLoader::LoadModelFromFile(&model, "../assets/models/fireplace_room.glb");
	model.batch_primitives();
	scene.add(&model);

	model.init_lightmap(&renderer, 256);

	check_time = time_sec();
	
}

void Test::set_mouse_down(bool down)
{
	if (down)
	{
		delta_acc = glm::vec2(0.0f);
		start_rot = camera.quaternion;
		mouse_down = true;
		recieve_delta = false;
	}
	else
	{
		mouse_down = false;
		recieve_delta = false;
	}
}

void Test::rotation_delta(double dx, double dy)
{
	delta_acc += glm::vec2(dx, dy);
	float rad = glm::length(delta_acc) * 0.005f;
	glm::vec3 axis = glm::normalize(glm::vec3(-delta_acc.y, -delta_acc.x, 0.0f));
	glm::quat rot_acc = glm::angleAxis(rad, axis);
	camera.set_quaternion(start_rot * rot_acc);
}

void Test::move(int dx, int dz)
{
	glm::vec3 delta = glm::vec3((float)dx, 0, (float)dz) * 0.1f;
	camera.position += camera.quaternion * delta;
}

void Test::Draw(int width, int height)
{
	bool size_changed = render_target.update_framebuffers(width, height);
	if (size_changed)
	{
		camera.aspect = (float)width / (float)height;
		camera.updateProjectionMatrix();
	}

	renderer.render(scene, camera, render_target);

	double start = time_sec();	
	
	while (iter < iterations)
	{
		double t = time_sec();
		if (t - check_time > 0.5)
		{
			printf("iter: %d, texel: %d\n", iter, idx_texel);
			check_time = t;
		}
		if (t - start > 0.010) break;		
		Lightmap& lightmap = *model.lightmap;
		LightmapRenderTarget& source = *model.lightmap_target;
		int num_texels = source.count_valid;
		int count = renderer.updateLightmap(scene, lightmap, source, idx_texel, 8 << iter);
		idx_texel += count;
		if (idx_texel >= num_texels)
		{
			renderer.filterLightmap(lightmap, source);
			idx_texel = 0;
			iter++;			
		}
	}
}
