#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include "core/Object3D.h"
#include "renderers/GLUtils.h"
#include "materials/MeshStandardMaterial.h"
#include "models/ModelComponents.h"

class LightmapRenderTarget;
class GLRenderer;
class Mesh;
class GLTFModel : public Object3D
{
public:
	GLTFModel();
	~GLTFModel();
	glm::vec3 m_min_pos = { FLT_MAX, FLT_MAX, FLT_MAX };
	glm::vec3 m_max_pos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	void calculate_bounding_box();

	std::vector<std::unique_ptr<GLTexture2D>> m_textures;
	std::unordered_map<int, GLTexture2D*> m_repl_textures;
	std::unordered_map<std::string, int> m_tex_dict;
	std::vector<std::unique_ptr<MeshStandardMaterial>> m_materials;
	
	std::vector<Mesh> m_meshs;
	std::unordered_map<std::string, int> m_mesh_dict;
	void updateMeshConstants();

	std::vector<Node> m_nodes;
	std::unordered_map<std::string, int> m_node_dict;	
	std::vector<int> m_roots;			
	void updateNodes();	

	// Batching primitives of the same material 
	std::unique_ptr<Mesh> batched_mesh;
	void batch_primitives();

	std::vector<std::vector<int>> batch_map;

	std::unique_ptr<Lightmap> lightmap;
	std::unique_ptr<LightmapRenderTarget> lightmap_target;
	void init_lightmap(GLRenderer* renderer, int texelsPerUnit = 128);

private:
	void batch_lightmap();
	
};
