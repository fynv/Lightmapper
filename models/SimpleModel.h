#pragma once

#include "core/Object3D.h"
#include "models/ModelComponents.h"
#include "materials/MeshStandardMaterial.h"
#include "renderers/GLUtils.h"

class LightmapRenderTarget;
class GLRenderer;
class MeshStandardMaterial;
class SimpleModel : public Object3D
{
public:
	SimpleModel();
	~SimpleModel();
	
	GLDynBuffer m_constant;
	void updateConstant();

	GLTexture2D texture;
	GLTexture2D* repl_texture = nullptr;
	MeshStandardMaterial material;
	Primitive geometry;

	void set_color(const glm::vec3& color);
	void set_metalness(float metalness);
	void set_roughness(float roughness);	

	std::unique_ptr<Lightmap> lightmap;
	std::unique_ptr<LightmapRenderTarget> lightmap_target;
	void init_lightmap(GLRenderer* renderer, int texelsPerUnit = 128);
};


