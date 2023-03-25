#include <GL/glew.h>
#include <list>
#include <gtx/hash.hpp>

#include "GLTFModel.h"
#include "utils/Utils.h"
#include "renderers/bvh_routines/PrimitiveBatch.h"
#include "renderers/LightmapRenderTarget.h"
#include "renderers/GLRenderer.h"

GLTFModel::GLTFModel()
{
}

GLTFModel::~GLTFModel()
{

}

void GLTFModel::calculate_bounding_box()
{
	m_min_pos = { FLT_MAX, FLT_MAX, FLT_MAX };
	m_max_pos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	size_t num_meshes = m_meshs.size();
	for (size_t i = 0; i < num_meshes; i++)
	{
		Mesh& mesh = m_meshs[i];

		glm::vec3 mesh_min_pos = { FLT_MAX, FLT_MAX, FLT_MAX };
		glm::vec3 mesh_max_pos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		size_t num_prims = mesh.primitives.size();
		for (size_t j = 0; j < num_prims; j++)
		{
			Primitive& prim = mesh.primitives[j];
			mesh_min_pos = glm::min(mesh_min_pos, prim.min_pos);
			mesh_max_pos = glm::max(mesh_max_pos, prim.max_pos);
		}
		
		if (mesh.node_id >= 0 && mesh.skin_id < 0)
		{
			Node& node = m_nodes[mesh.node_id];
			glm::mat4 mesh_mat = node.g_trans;

			glm::vec4 model_pos[8];
			model_pos[0] = mesh_mat * glm::vec4(mesh_min_pos.x, mesh_min_pos.y, mesh_min_pos.z, 1.0f);
			model_pos[1] = mesh_mat * glm::vec4(mesh_max_pos.x, mesh_min_pos.y, mesh_min_pos.z, 1.0f);
			model_pos[2] = mesh_mat * glm::vec4(mesh_min_pos.x, mesh_max_pos.y, mesh_min_pos.z, 1.0f);
			model_pos[3] = mesh_mat * glm::vec4(mesh_max_pos.x, mesh_max_pos.y, mesh_min_pos.z, 1.0f);
			model_pos[4] = mesh_mat * glm::vec4(mesh_min_pos.x, mesh_min_pos.y, mesh_max_pos.z, 1.0f);
			model_pos[5] = mesh_mat * glm::vec4(mesh_max_pos.x, mesh_min_pos.y, mesh_max_pos.z, 1.0f);
			model_pos[6] = mesh_mat * glm::vec4(mesh_min_pos.x, mesh_max_pos.y, mesh_max_pos.z, 1.0f);
			model_pos[7] = mesh_mat * glm::vec4(mesh_max_pos.x, mesh_max_pos.y, mesh_max_pos.z, 1.0f);

			for (int k = 0; k < 8; k++)
			{
				glm::vec4 pos = model_pos[k];
				if (pos.x < m_min_pos.x) m_min_pos.x = pos.x;
				if (pos.x > m_max_pos.x) m_max_pos.x = pos.x;
				if (pos.y < m_min_pos.y) m_min_pos.y = pos.y;
				if (pos.y > m_max_pos.y) m_max_pos.y = pos.y;
				if (pos.z < m_min_pos.z) m_min_pos.z = pos.z;
				if (pos.z > m_max_pos.z) m_max_pos.z = pos.z;
			}
		}
		else
		{
			m_min_pos = glm::min(m_min_pos, mesh_min_pos);
			m_max_pos = glm::max(m_max_pos, mesh_max_pos);
		}

		
	}
}

struct ModelConst
{
	glm::mat4 ModelMat;
	glm::mat4 NormalMat;
};

void GLTFModel::updateMeshConstants()
{
	size_t num_mesh = m_meshs.size();
	for (size_t i = 0; i < num_mesh; i++)
	{
		Mesh& mesh = m_meshs[i];
		glm::mat4 matrix = matrixWorld;
		if (mesh.node_id >= 0 && mesh.skin_id < 0)
		{
			Node& node = m_nodes[mesh.node_id];
			matrix *= node.g_trans;
		}
		ModelConst c;
		c.ModelMat = matrix;
		c.NormalMat = glm::transpose(glm::inverse(matrix));
		mesh.model_constant->upload(&c);
	}

	if (batched_mesh != nullptr)
	{
		ModelConst c;
		c.ModelMat = matrixWorld;
		c.NormalMat = glm::transpose(glm::inverse(matrixWorld));
		batched_mesh->model_constant->upload(&c);
	}
}

void GLTFModel::updateNodes()
{
	std::list<int> node_queue;
	size_t num_roots = m_roots.size();
	for (size_t i = 0; i < num_roots; i++)
	{
		int idx_root = m_roots[i];		
		Node& node = m_nodes[idx_root];
		node.g_trans = glm::identity<glm::mat4>();
		node_queue.push_back(idx_root);
	}

	while (!node_queue.empty())
	{
		int id_node = node_queue.front();
		node_queue.pop_front();
		Node& node = m_nodes[id_node];

		glm::mat4 local = glm::identity<glm::mat4>();
		local = glm::translate(local, node.translation);
		local *= glm::toMat4(node.rotation);
		local = glm::scale(local, node.scale);
		node.g_trans *= local;

		for (size_t i = 0; i < node.children.size(); i++)
		{
			int id_child = node.children[i];
			Node& child = m_nodes[id_child];
			child.g_trans = node.g_trans;
			node_queue.push_back(id_child);
		}
	}
}

template<typename T>
inline void t_copy_indices(int num_face, int pos_offset, int face_offset, const uint8_t* p_indices_in, uint8_t* p_indices_out)
{
	const T* p_in = (const T*)p_indices_in;
	int* p_out = (int*)p_indices_out;
	for (int i = 0; i < num_face * 3; i++)
	{
		p_out[face_offset * 3 + i] = (int)(p_in[i]) + pos_offset;
	}
}

inline void g_copy_indices(int num_face, int pos_offset, int face_offset, int type_indices, const uint8_t* p_indices_in, uint8_t* p_indices_out)
{
	if (type_indices == 1)
	{
		t_copy_indices<uint8_t>(num_face, pos_offset, face_offset, p_indices_in, p_indices_out);
	}
	if (type_indices == 2)
	{
		t_copy_indices<uint16_t>(num_face, pos_offset, face_offset, p_indices_in, p_indices_out);
	}
	else if (type_indices == 4)
	{
		t_copy_indices<uint32_t>(num_face, pos_offset, face_offset, p_indices_in, p_indices_out);
	}

}

void GLTFModel::batch_primitives()
{
	struct BatchInfo
	{
		int num_pos = 0;
		int num_face = 0;
		bool has_color = false;
		bool has_uv = false;
		bool has_tangent = false;
		std::vector<glm::ivec2> indices;
	};

	std::unordered_map<int, BatchInfo> primitive_map;

	size_t num_meshes = m_meshs.size();
	batch_map.resize(num_meshes);
	for (size_t i = 0; i < num_meshes; i++)
	{
		Mesh& mesh = m_meshs[i];		
		size_t num_prims = mesh.primitives.size();
		batch_map[i].resize(num_prims);
		for (size_t j = 0; j < num_prims; j++)
		{
			Primitive& prim = mesh.primitives[j];
			BatchInfo& info = primitive_map[prim.material_idx];
			info.num_pos += prim.num_pos;
			info.num_face += prim.num_face;
			info.has_color = info.has_color || prim.color_buf != nullptr;
			info.has_uv = info.has_uv || prim.uv_buf != nullptr;
			info.has_tangent = info.has_tangent || prim.geometry[0].tangent_buf != nullptr;
			info.indices.push_back({ i,j });
		}
	}	

	std::unique_ptr<PrimitiveBatch> batchers[8];
	GLDynBuffer subModel(sizeof(ModelConst), GL_UNIFORM_BUFFER);

	batched_mesh = std::unique_ptr<Mesh>(new Mesh);
	{
		ModelConst c;
		c.ModelMat = matrixWorld;
		c.NormalMat = glm::transpose(glm::inverse(matrixWorld));
		batched_mesh->model_constant->upload(&c);
	}


	batched_mesh->primitives.resize(primitive_map.size());
	int idx_prim = 0;

	auto iter = primitive_map.begin();
	while (iter != primitive_map.end())
	{
		BatchInfo info = iter->second;				
		
		Primitive& prim_batch = batched_mesh->primitives[idx_prim];
		prim_batch.material_idx = iter->first;
		prim_batch.type_indices = 4;
		prim_batch.num_pos = info.num_pos;
		prim_batch.num_face = info.num_face;		

		bool has_color = info.has_color;
		bool has_uv = info.has_uv;		
		bool has_tangent = info.has_tangent;

		int batcher_idx = (has_color ? 1 : 0) + (has_uv ? 2 : 0) + (has_tangent ? 4 : 0);
		auto& batcher = batchers[batcher_idx];
		if (batcher == nullptr)
		{
			PrimitiveBatch::Options options;
			options.has_color = has_color;
			options.has_uv = has_uv;
			options.has_tangent = has_tangent;
			batcher = std::unique_ptr<PrimitiveBatch>(new PrimitiveBatch(options));
		}

		prim_batch.geometry.resize(1);
		GeometrySet& geometry = prim_batch.geometry[0];

		geometry.pos_buf = Attribute(new TextureBuffer(sizeof(glm::vec4)* prim_batch.num_pos, GL_RGBA32F));
		geometry.normal_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * prim_batch.num_pos, GL_RGBA32F));

		if (has_color)
		{
			prim_batch.color_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * prim_batch.num_pos, GL_RGBA32F));
		}

		if (has_uv)
		{
			prim_batch.uv_buf = Attribute(new TextureBuffer(sizeof(glm::vec2) * prim_batch.num_pos, GL_RG32F));
		}

		if (has_tangent)
		{
			geometry.tangent_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * prim_batch.num_pos, GL_RGBA32F));
			geometry.bitangent_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * prim_batch.num_pos, GL_RGBA32F));
		}		

		prim_batch.cpu_pos = std::unique_ptr<std::vector<glm::vec4>>(new std::vector<glm::vec4>(prim_batch.num_pos));
		prim_batch.cpu_indices = std::unique_ptr<std::vector<uint8_t>>(new std::vector<uint8_t>(prim_batch.num_face * 3 * 4));

		int pos_offset = 0;
		int face_offset = 0;

		const std::vector<glm::ivec2>& indices = info.indices;
		for (size_t i = 0; i < indices.size(); i++)
		{
			int idx_mesh = indices[i].x;
			int idx_prim = indices[i].y;
			Mesh& mesh = m_meshs[idx_mesh];
			Primitive& prim = mesh.primitives[idx_prim];

			std::vector<int>& batch_map_mesh = batch_map[idx_mesh];
			batch_map_mesh[idx_prim] = face_offset * 3;

			prim_batch.min_pos = glm::min(prim_batch.min_pos, prim.min_pos);
			prim_batch.max_pos = glm::max(prim_batch.max_pos, prim.max_pos);

			int num_pos = prim.num_pos;
			for (int j = 0; j < num_pos; j++)
			{
				glm::vec4 pos = (*prim.cpu_pos)[j];
				if (mesh.node_id >= 0 && mesh.skin_id < 0)
				{
					Node& node = m_nodes[mesh.node_id];
					pos = node.g_trans * pos;
				}
				(*prim_batch.cpu_pos)[pos_offset + j] = pos;
			}						

			int num_face = prim.num_face;
			if (prim.index_buf != nullptr)
			{
				g_copy_indices(num_face, pos_offset, face_offset, prim.type_indices, prim.cpu_indices->data(), prim_batch.cpu_indices->data());
			}
			else
			{
				int* p_out = (int*)prim_batch.cpu_indices->data();
				for (int i = 0; i < num_face * 3; i++)
				{
					p_out[face_offset * 3 + i] = i  + pos_offset;
				}
			}

			{
				glm::mat4 matrix = glm::identity<glm::mat4>();
				if (mesh.node_id >= 0 && mesh.skin_id < 0)
				{
					Node& node = m_nodes[mesh.node_id];
					matrix = node.g_trans;
				}

				ModelConst c;
				c.ModelMat = matrix;
				c.NormalMat = glm::transpose(glm::inverse(matrix));
				subModel.upload(&c);
			}

			PrimitiveBatch::Params params;
			params.offset = pos_offset;
			params.constant_model = &subModel;
			params.primitive_in = &prim;
			params.primitive_out = &prim_batch;
			batcher->update(params);

			pos_offset += num_pos;
			face_offset += num_face;
		}		

		prim_batch.index_buf = Index(new IndexTextureBuffer(prim_batch.cpu_indices->size(), 4));
		prim_batch.index_buf->upload(prim_batch.cpu_indices->data());		

		iter++;
		idx_prim++;
	}

	if (lightmap != nullptr)
	{		
		batch_lightmap();	
	}
}

void GLTFModel::batch_lightmap()
{
	struct BatchInfo
	{
		int num_light_uv = 0;		
		std::vector<glm::ivec2> indices;
	};

	std::unordered_map<int, BatchInfo> primitive_map;

	size_t num_meshes = m_meshs.size();
	for (size_t i = 0; i < num_meshes; i++)
	{
		Mesh& mesh = m_meshs[i];
		size_t num_prims = mesh.primitives.size();
		for (size_t j = 0; j < num_prims; j++)
		{
			Primitive& prim = mesh.primitives[j];
			BatchInfo& info = primitive_map[prim.material_idx];
			info.num_light_uv += (int)prim.cpu_lightmap_uv->size();			
			info.indices.push_back({ i,j });
		}
	}

	int num_batched_prims = (int)batched_mesh->primitives.size();
	for (int i = 0; i < num_batched_prims; i++)
	{
		Primitive& prim_batch = batched_mesh->primitives[i];
		int material_idx = prim_batch.material_idx;
		BatchInfo info = primitive_map[material_idx];
		
		int num_light_uv_batch = info.num_light_uv;

		prim_batch.cpu_lightmap_uv = std::unique_ptr<std::vector<glm::vec2>>(new std::vector<glm::vec2>(num_light_uv_batch));
		prim_batch.cpu_lightmap_indices = std::unique_ptr<std::vector<int>>(new std::vector<int>(prim_batch.num_face * 3));

		int uv_offset = 0;
		int face_offset = 0;

		const std::vector<glm::ivec2>& indices = info.indices;
		for (size_t i = 0; i < indices.size(); i++)
		{
			int idx_mesh = indices[i].x;
			int idx_prim = indices[i].y;

			Mesh& mesh = m_meshs[idx_mesh];
			Primitive& prim = mesh.primitives[idx_prim];

			int num_light_uv = (int)prim.cpu_lightmap_uv->size();
			for (int j = 0; j < num_light_uv; j++)
			{
				glm::vec2 uv = (*prim.cpu_lightmap_uv)[j];				
				(*prim_batch.cpu_lightmap_uv)[uv_offset + j] = uv;
			}

			int num_face = prim.num_face;
			g_copy_indices(num_face, uv_offset, face_offset, 4, (const uint8_t*)prim.cpu_lightmap_indices->data(), (uint8_t*)prim_batch.cpu_lightmap_indices->data());

			uv_offset += num_light_uv;
			face_offset += num_face;
		}

		prim_batch.lightmap_indices = Index(new IndexTextureBuffer(sizeof(int)*prim_batch.cpu_lightmap_indices->size(), 4));
		prim_batch.lightmap_indices->upload(prim_batch.cpu_lightmap_indices->data());

		prim_batch.lightmap_uv_buf = Attribute(new TextureBuffer(sizeof(glm::vec2) * num_light_uv_batch, GL_RG32F));
		prim_batch.lightmap_uv_buf->upload(prim_batch.cpu_lightmap_uv->data());
	}
}

void GLTFModel::init_lightmap(GLRenderer* renderer, int texelsPerUnit)
{
	std::vector<Primitive*> primitives;
	std::vector<glm::mat4> trans;

	size_t num_meshes = m_meshs.size();	
	for (size_t i = 0; i < num_meshes; i++)
	{
		Mesh& mesh = m_meshs[i];
		size_t num_prims = mesh.primitives.size();		
		for (size_t j = 0; j < num_prims; j++)
		{
			Primitive& prim = mesh.primitives[j];
			glm::mat4 model_mat = glm::identity<glm::mat4>();
			if (mesh.node_id >= 0 && mesh.skin_id < 0)
			{
				Node& node = m_nodes[mesh.node_id];
				model_mat = node.g_trans;
			}
			primitives.push_back(&prim);
			trans.push_back(model_mat);
		}
	}

	lightmap = std::unique_ptr<Lightmap>(new Lightmap(primitives, trans, texelsPerUnit));
	if (batched_mesh != nullptr)
	{		
		batch_lightmap();		
	}

	lightmap_target = std::unique_ptr<LightmapRenderTarget>(new LightmapRenderTarget);
	lightmap_target->update_framebuffer(lightmap->width, lightmap->height);
	renderer->rasterize_atlas(this);
	
	glm::vec4 zero = { 0.0f, 0.0f, 0.0f, 0.0f};
	glClearTexImage(lightmap->lightmap->tex_id, 0, GL_RGBA, GL_FLOAT, &zero);

	std::vector<float> alpha_mask(lightmap->width * lightmap->height);

	glBindTexture(GL_TEXTURE_2D, lightmap_target->m_tex_position->tex_id);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_FLOAT, alpha_mask.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	std::vector<glm::u16vec2> lst_valid;

	for (int i = 0; i < lightmap->width * lightmap->height; i++)
	{
		float f = alpha_mask[i];
		if (f > 0.5f)
		{			
			uint16_t x = (uint16_t)(i % lightmap->width);
			uint16_t y = (uint16_t)(i / lightmap->width);
			lst_valid.push_back({ x,y });
		}	
	}	

	lightmap_target->count_valid = (int)lst_valid.size();
	lightmap_target->valid_list = std::unique_ptr<TextureBuffer>(new TextureBuffer(sizeof(glm::u16vec2) * lightmap_target->count_valid, GL_RG16UI));
	lightmap_target->valid_list->upload(lst_valid.data());
}

