#pragma once

#include <memory>
#include <vector>
#include <glm.hpp>
#include <gtx/quaternion.hpp>
#include "renderers/GLUtils.h"
#include "core/CWBVH.h"

typedef std::unique_ptr<TextureBuffer> Attribute;

class IndexTextureBuffer : public TextureBuffer
{
public:	
	IndexTextureBuffer(size_t size, int type_indices);
	~IndexTextureBuffer();
};

typedef std::unique_ptr<IndexTextureBuffer> Index;

struct GeometrySet
{
	Attribute pos_buf;
	Attribute normal_buf;
	Attribute tangent_buf;
	Attribute bitangent_buf;
};


class Primitive
{
public:
	int num_pos = 0;
	std::vector<GeometrySet> geometry;
	Attribute color_buf;
	Attribute uv_buf;
	
	int num_face = 0;
	int type_indices = 2; // 1:uchar; 2: ushort; 4: uint
	Index index_buf;

	int material_idx = -1;

	// keep a cpu copy for ray-cast
	glm::vec3 min_pos = { FLT_MAX, FLT_MAX, FLT_MAX };
	glm::vec3 max_pos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	std::unique_ptr<std::vector<glm::vec4>> cpu_pos;
	std::unique_ptr<std::vector<glm::vec4>> cpu_norm;
	std::unique_ptr<std::vector<glm::vec2>> cpu_uv;
	std::unique_ptr<std::vector<uint8_t>> cpu_indices;

	std::unique_ptr<CWBVH> cwbvh;

	Attribute lightmap_uv_buf;
	Index lightmap_indices;
	std::unique_ptr<std::vector<glm::vec2>> cpu_lightmap_uv;
	std::unique_ptr<std::vector<int>> cpu_lightmap_indices;

};

class Lightmap
{
public:
	Lightmap(int width, int height);
	Lightmap(const std::vector<Primitive*>& primitives, const std::vector<glm::mat4>& trans, int texelsPerUnit = 128);

	int width, height;
	int texels_per_unit = 128;
	std::unique_ptr<GLTexture2D> lightmap;
};

class Node
{
public:
	std::vector<int> children;
	glm::vec3 translation;
	glm::quat rotation;
	glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
	glm::mat4 g_trans;
};

class Mesh
{
public:
	Mesh();
	int node_id = -1;
	int skin_id = -1;
	std::unique_ptr<GLDynBuffer> model_constant;
	std::vector<Primitive> primitives;	
};
