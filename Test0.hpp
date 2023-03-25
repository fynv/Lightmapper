#pragma once 

#include "scenes/Scene.h"
#include "cameras/PerspectiveCamera.h"
#include "backgrounds/Background.h"
#include "models/SimpleModel.h"
#include "models/GeometryCreator.h"
#include "lights/DirectionalLight.h"
#include "renderers/GLRenderer.h"
#include "renderers/GLRenderTarget.h"
#include "renderers/LightmapRenderTarget.h"

class Test
{
public:
	Scene scene;
	PerspectiveCamera camera;

	CubeBackground background;
	//ColorBackground background;
	//HemisphereBackground background;

	SimpleModel box;
	SimpleModel sphere;
	SimpleModel ground;
	DirectionalLight directional_light;

	struct RefLightMap
	{
		Lightmap* lm;
		LightmapRenderTarget* lm_target;
	};

	std::vector<RefLightMap> lightmaps;
	
	int idx_texel = 0;
	int idx_lightmap = 0;
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
	camera.position = { 0.0f, 0.0f, 7.0f };

	std::string path = "../assets/textures";
	std::string paths[6];
	for (int i = 0; i < 6; i++)
	{
		char rel_path[64];
		sprintf(rel_path, "%s/sky_cube_face%d.jpg", path.c_str(), i);
		paths[i] = rel_path;
	}

	background.cubemap.load_files(paths[0].c_str(), paths[1].c_str(), paths[2].c_str(), paths[3].c_str(), paths[4].c_str(), paths[5].c_str());
	//background.color = glm::vec3(0.0);
	scene.background = &background;

	GeometryCreator::CreateBox(&box.geometry, 2.0f, 2.0f, 2.0f);
	box.name = "box";
	box.translateX(-1.5f);
	box.rotateOnAxis(glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)), 1.0f);
	box.texture.load_file("../assets/textures/uv-test-bw.png", true);
	box.init_lightmap(&renderer, 64);
	lightmaps.push_back({ box.lightmap.get(), box.lightmap_target.get() });
	scene.add(&box);

	GeometryCreator::CreateSphere(&sphere.geometry, 1.0f, 32, 16);
	sphere.name = "sphere";
	sphere.translateX(1.5f);
	sphere.texture.load_file("../assets/textures/uv-test-col.png", true);
	sphere.material.metallicFactor = 0.5f;
	sphere.material.roughnessFactor = 0.5f;
	sphere.material.update_uniform();
	sphere.init_lightmap(&renderer, 64);
	lightmaps.push_back({ sphere.lightmap.get(), sphere.lightmap_target.get() });
	scene.add(&sphere);

	GeometryCreator::CreatePlane(&ground.geometry, 10.0f, 10.0f);
	ground.name = "ground";
	ground.translateY(-1.7f);
	ground.rotateX(-3.14159f * 0.5f);
	ground.init_lightmap(&renderer, 64);
	lightmaps.push_back({ ground.lightmap.get(), ground.lightmap_target.get() });
	scene.add(&ground);

	directional_light.intensity = 4.0;
	directional_light.position = { 5.0, 10.0, 5.0 };
	directional_light.setShadow(true, 4096, 4096);
	directional_light.setShadowProjection(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f, 50.0f);
	directional_light.SetShadowRadius(0.02f);
	scene.add(&directional_light);

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
			printf("iter: %d, lightmap: %d, texel: %d\n", iter, idx_lightmap, idx_texel);
			check_time = t;
		}
		if (t - start > 0.010) break;	
		
		RefLightMap ref_lm = lightmaps[idx_lightmap];
		Lightmap& lightmap = *ref_lm.lm;
		LightmapRenderTarget& source = *ref_lm.lm_target;
		int num_texels = source.count_valid;
		int count = renderer.updateLightmap(scene, lightmap, source, idx_texel, 8<<iter);
		idx_texel += count;
		if (idx_texel >= num_texels)
		{
			renderer.filterLightmap(lightmap, source);
			idx_texel = 0;
			idx_lightmap++;			
			if (idx_lightmap >= (int)lightmaps.size())
			{
				idx_lightmap = 0;
				iter++;
			}
		}
	}
}
