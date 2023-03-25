#include <GL/glew.h>
#include <gtx/matrix_decompose.hpp>

#include "GLTFLoader.h"

#include "models/ModelComponents.h"
#include "models/GLTFModel.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

template<typename T>
inline void t_calc_normal(int num_face, int num_pos, const T* p_indices, const glm::vec4* p_pos, glm::vec4* p_norm)
{
	std::vector<float> counts(num_pos, 0.0f);
	for (int j = 0; j < num_face; j++)
	{
		glm::uvec3 ind;
		if (p_indices != nullptr)
		{
			ind.x = (uint32_t)p_indices[j * 3];
			ind.y = (uint32_t)p_indices[j * 3 + 1];
			ind.z = (uint32_t)p_indices[j * 3 + 2];
		}
		else
		{
			ind.x = j * 3;
			ind.y = j * 3 + 1;
			ind.z = j * 3 + 2;
		}

		glm::vec3 v0 = p_pos[ind.x];
		glm::vec3 v1 = p_pos[ind.y];
		glm::vec3 v2 = p_pos[ind.z];
		glm::vec4 face_normals = glm::vec4(glm::cross(v1 - v0, v2 - v0), 0.0f);

		p_norm[ind.x] += face_normals;
		p_norm[ind.y] += face_normals;
		p_norm[ind.z] += face_normals;
		counts[ind.x] += 1.0f;
		counts[ind.y] += 1.0f;
		counts[ind.z] += 1.0f;
	}

	for (int j = 0; j < num_pos; j++)
		p_norm[j] = p_norm[j]/ counts[j];
}

inline void g_calc_normal(int num_face, int num_pos, int type_indices, const void* p_indices, const glm::vec4* p_pos, glm::vec4* p_norm)
{
	if (type_indices == 1)
	{
		t_calc_normal<uint8_t>(num_face, num_pos, (uint8_t*)p_indices, p_pos, p_norm);
	}
	if (type_indices == 2)
	{
		t_calc_normal<uint16_t>(num_face, num_pos, (uint16_t*)p_indices, p_pos, p_norm);
	}
	else if (type_indices == 4)
	{
		t_calc_normal<uint32_t>(num_face, num_pos, (uint32_t*)p_indices, p_pos, p_norm);
	}
}

template<typename T>
inline void t_calc_tangent(int num_face, int num_pos, const T* p_indices, const glm::vec4* p_pos, const glm::vec2* p_uv, glm::vec4* p_tangent, glm::vec4* p_bitangent)
{
	std::vector<float> counts(num_pos, 0.0f);
	for (int j = 0; j < num_face; j++)
	{
		glm::uvec3 ind;
		if (p_indices != nullptr)
		{
			ind.x = (uint32_t)p_indices[j * 3];
			ind.y = (uint32_t)p_indices[j * 3 + 1];
			ind.z = (uint32_t)p_indices[j * 3 + 2];
		}
		else
		{
			ind.x = j * 3;
			ind.y = j * 3 + 1;
			ind.z = j * 3 + 2;
		}

		glm::vec3 v0 = p_pos[ind.x];
		glm::vec3 v1 = p_pos[ind.y];
		glm::vec3 v2 = p_pos[ind.z];
		glm::vec2 texCoord0 = p_uv[ind.x];
		glm::vec2 texCoord1 = p_uv[ind.y];
		glm::vec2 texCoord2 = p_uv[ind.z];

		glm::vec3 edge1 = v1 - v0;
		glm::vec3 edge2 = v2 - v0;
		glm::vec2 delta1 = texCoord1 - texCoord0;
		glm::vec2 delta2 = texCoord2 - texCoord0;

		float f = 1.0f / (delta1[0] * delta2[1] - delta2[0] * delta1[1]);
		glm::vec4 tagent = glm::vec4((f * delta2[1]) * edge1 - (f * delta1[1]) * edge2, 0.0f);
		glm::vec4 bitangent = glm::vec4((-f * delta2[0]) * edge1 + (f * delta1[0]) * edge2, 0.0f);

		p_tangent[ind.x] += tagent;
		p_tangent[ind.y] += tagent;
		p_tangent[ind.z] += tagent;

		p_bitangent[ind.x] += bitangent;
		p_bitangent[ind.y] += bitangent;
		p_bitangent[ind.z] += bitangent;

		counts[ind.x] += 1.0f;
		counts[ind.y] += 1.0f;
		counts[ind.z] += 1.0f;
	}

	for (int j = 0; j < num_pos; j++)
	{
		p_tangent[j] = p_tangent[j] / counts[j];
		p_bitangent[j] = p_bitangent[j] / counts[j];
	}
}

inline void g_calc_tangent(int num_face, int num_pos, int type_indices, const void* p_indices, const glm::vec4* p_pos, const glm::vec2* p_uv, glm::vec4* p_tangent, glm::vec4* p_bitangent)
{
	if (type_indices == 1)
	{
		t_calc_tangent<uint8_t>(num_face, num_pos, (uint8_t*)p_indices, p_pos, p_uv, p_tangent, p_bitangent);
	}
	if (type_indices == 2)
	{
		t_calc_tangent<uint16_t>(num_face, num_pos, (uint16_t*)p_indices, p_pos, p_uv, p_tangent, p_bitangent);
	}
	else if (type_indices == 4)
	{
		t_calc_tangent<uint32_t>(num_face, num_pos, (uint32_t*)p_indices, p_pos, p_uv, p_tangent, p_bitangent);
	}
}

template<typename T>
inline void t_fill_sparse_positions(int count, const T* p_indices, const glm::vec3* p_pos_in, glm::vec4* p_pos_out, int* p_none_zero)
{	
	for (int i = 0; i < count; i++)
	{
		T idx = p_indices[i];
		p_pos_out[idx] = glm::vec4(p_pos_in[i], 0.0f);
		p_none_zero[idx] = 1;
	}
}

inline void g_fill_sparse_positions(int count, int type_idx, const void* p_indices, const glm::vec3* p_pos_in, glm::vec4* p_pos_out, int* p_none_zero)
{
	if (type_idx == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
	{
		t_fill_sparse_positions<uint8_t>(count, (uint8_t*)p_indices, p_pos_in, p_pos_out, p_none_zero);
	}
	else if (type_idx == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
	{
		t_fill_sparse_positions<uint16_t>(count, (uint16_t*)p_indices, p_pos_in, p_pos_out, p_none_zero);
	}
	else if (type_idx == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
	{
		t_fill_sparse_positions<uint32_t>(count, (uint32_t*)p_indices, p_pos_in, p_pos_out, p_none_zero);
	}

}

template<typename T>
inline void t_copy_joints(int num_pos, const T* p_in, glm::uvec4* p_out)
{
	for (int i = 0; i < num_pos; i++)
	{
		const T& j_in = p_in[i];
		glm::uvec4& j_out = p_out[i];
		j_out.x = j_in.x;
		j_out.y = j_in.y;
		j_out.z = j_in.z;
		j_out.w = j_in.w;
	}
}

inline void load_animations(tinygltf::Model& model, std::vector<AnimationClip>& animations);

inline void load_model(tinygltf::Model& model, GLTFModel* model_out)
{
	struct TexLoadOptions
	{
		bool is_srgb = true;
		bool reverse = false;
	};

	size_t num_textures = model.textures.size();
	model_out->m_textures.resize(num_textures);
	std::vector<TexLoadOptions> tex_opts(num_textures);

	size_t num_materials = model.materials.size();
	model_out->m_materials.resize(num_materials+1);
	for (size_t i = 0; i < num_materials; i++)
	{
		tinygltf::Material& material_in = model.materials[i];
		MeshStandardMaterial* material_out = new MeshStandardMaterial();
		model_out->m_materials[i] = std::unique_ptr<MeshStandardMaterial>(material_out);
		if (material_in.alphaMode == "OPAQUE")
		{
			material_out->alphaMode = AlphaMode::Opaque;
		}
		else if (material_in.alphaMode == "MASK")
		{
			material_out->alphaMode = AlphaMode::Mask;
		}
		else if (material_in.alphaMode == "BLEND")
		{
			material_out->alphaMode = AlphaMode::Blend;
		}
		material_out->alphaCutoff = (float)material_in.alphaCutoff;
		material_out->doubleSided = material_in.doubleSided;

		tinygltf::PbrMetallicRoughness& pbr = material_in.pbrMetallicRoughness;
		material_out->color = { pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3] };
		material_out->tex_idx_map = pbr.baseColorTexture.index;

		if (material_in.normalTexture.index >= 0)
		{
			tex_opts[material_in.normalTexture.index].is_srgb = false;
			material_out->tex_idx_normalMap = material_in.normalTexture.index;
			float scale = (float)material_in.normalTexture.scale;
			material_out->normalScale = { scale, scale };
		}

		material_out->emissive = { material_in.emissiveFactor[0], material_in.emissiveFactor[1], material_in.emissiveFactor[2] };

		if (material_in.extensions.find("KHR_materials_emissive_strength") != material_in.extensions.end())
		{
			tinygltf::Value::Object& emissive_stength = material_in.extensions["KHR_materials_emissive_strength"].Get<tinygltf::Value::Object>();
			float strength = (float)emissive_stength["emissiveStrength"].Get<double>();
			material_out->emissive *= strength;
		}

		material_out->tex_idx_emissiveMap = material_in.emissiveTexture.index;

		material_out->metallicFactor = pbr.metallicFactor;
		material_out->roughnessFactor = pbr.roughnessFactor;

		int id_mr = pbr.metallicRoughnessTexture.index;
		if (id_mr >= 0)
		{
			tex_opts[id_mr].is_srgb = false;
			material_out->tex_idx_metalnessMap = id_mr;
			material_out->tex_idx_roughnessMap = id_mr;
		}

		if (material_in.extensions.find("KHR_materials_pbrSpecularGlossiness")!= material_in.extensions.end())
		{			
			material_out->specular_glossiness = true;
			tinygltf::Value::Object& sg = material_in.extensions["KHR_materials_pbrSpecularGlossiness"].Get<tinygltf::Value::Object>();

			if (sg.find("diffuseFactor")!=sg.end())
			{
				tinygltf::Value& color = sg["diffuseFactor"];
				float r = (float)color.Get(0).Get<double>();
				float g = (float)color.Get(1).Get<double>();
				float b = (float)color.Get(2).Get<double>();
				float a = (float)color.Get(3).Get<double>();
				material_out->color = { r,g,b,a };
			}

			if (sg.find("diffuseTexture") != sg.end())
			{
				tinygltf::Value::Object& tex = sg["diffuseTexture"].Get<tinygltf::Value::Object>();
				int idx = tex["index"].Get<int>();
				material_out->tex_idx_map = idx;
			}

			if (sg.find("glossinessFactor") != sg.end())
			{
				float v = (float)sg["glossinessFactor"].Get<double>();
				material_out->glossinessFactor = v;
			}

			if (sg.find("specularFactor") != sg.end())
			{
				tinygltf::Value& color = sg["specularFactor"];
				float r = (float)color.Get(0).Get<double>();
				float g = (float)color.Get(1).Get<double>();
				float b = (float)color.Get(2).Get<double>();
				material_out->specular = { r,g,b };
			}

			if (sg.find("specularGlossinessTexture") != sg.end())
			{
				tinygltf::Value::Object& tex = sg["specularGlossinessTexture"].Get<tinygltf::Value::Object>();
				int idx = tex["index"].Get<int>();
				material_out->tex_idx_specularMap = idx;
				material_out->tex_idx_glossinessMap = idx;
			}
		}

		material_out->update_uniform();
	}

	// default material
	{
		MeshStandardMaterial* material_out = new MeshStandardMaterial();
		model_out->m_materials[num_materials] = std::unique_ptr<MeshStandardMaterial>(material_out);
		material_out->update_uniform();
	}

	for (size_t i = 0; i < num_textures; i++)
	{
		tinygltf::Texture& tex_in = model.textures[i];
		GLTexture2D* tex_out = new GLTexture2D();
		model_out->m_textures[i] = std::unique_ptr<GLTexture2D>(tex_out);

		tinygltf::Image& img_in = model.images[tex_in.source];
		model_out->m_tex_dict[img_in.name] = i;
		const TexLoadOptions& opts = tex_opts[i];

		bool has_alpha = img_in.component > 3;
		if (opts.reverse)
		{
			if (has_alpha)
			{
				tex_out->load_memory_bgra(img_in.width, img_in.height, img_in.image.data(), opts.is_srgb);
			}
			else
			{
				tex_out->load_memory_bgr(img_in.width, img_in.height, img_in.image.data(), opts.is_srgb);
			}
		}
		else
		{
			if (has_alpha)
			{
				tex_out->load_memory_rgba(img_in.width, img_in.height, img_in.image.data(), opts.is_srgb);
			}
			else
			{
				tex_out->load_memory_rgb(img_in.width, img_in.height, img_in.image.data(), opts.is_srgb);
			}
		}
	}

	size_t num_meshes = model.meshes.size();
	size_t num_nodes = model.nodes.size();	
	
	model_out->m_meshs.resize(num_meshes);
	for (size_t i = 0; i < num_meshes; i++)
	{
		tinygltf::Mesh& mesh_in = model.meshes[i];
		Mesh& mesh_out = model_out->m_meshs[i];	

		size_t num_primitives = mesh_in.primitives.size();
		mesh_out.primitives.resize(num_primitives);

		for (size_t j = 0; j < num_primitives; j++)
		{
			tinygltf::Primitive& primitive_in = mesh_in.primitives[j];
			Primitive& primitive_out = mesh_out.primitives[j];
			primitive_out.material_idx = primitive_in.material;
			if (primitive_out.material_idx < 0)
			{
				primitive_out.material_idx = num_materials;
			}

			MeshStandardMaterial* material = model_out->m_materials[primitive_out.material_idx].get();
			bool has_tangent = material->tex_idx_normalMap >= 0;		

			int num_geo_sets = 1;			
			primitive_out.geometry.resize(num_geo_sets);

			int id_pos_in = primitive_in.attributes["POSITION"];
			tinygltf::Accessor& acc_pos_in = model.accessors[id_pos_in];
			primitive_out.num_pos = (int)acc_pos_in.count;
			tinygltf::BufferView& view_pos_in = model.bufferViews[acc_pos_in.bufferView];
			const glm::vec3* p_pos = (const glm::vec3*)(model.buffers[view_pos_in.buffer].data.data() + view_pos_in.byteOffset + acc_pos_in.byteOffset);

			primitive_out.min_pos = { acc_pos_in.minValues[0], acc_pos_in.minValues[1], acc_pos_in.minValues[2] };
			primitive_out.max_pos = { acc_pos_in.maxValues[0], acc_pos_in.maxValues[1], acc_pos_in.maxValues[2] };

			int id_indices_in = primitive_in.indices;
			const void* p_indices = nullptr;
			if (id_indices_in >= 0)
			{
				tinygltf::Accessor& acc_indices_in = model.accessors[id_indices_in];
				primitive_out.num_face = (int)(acc_indices_in.count / 3);
				tinygltf::BufferView& view_indices_in = model.bufferViews[acc_indices_in.bufferView];
				p_indices = model.buffers[view_indices_in.buffer].data.data() + view_indices_in.byteOffset + acc_indices_in.byteOffset;

				if (acc_indices_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
				{
					primitive_out.type_indices = 1;
				}
				else if (acc_indices_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					primitive_out.type_indices = 2;
				}
				else if (acc_indices_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
				{
					primitive_out.type_indices = 4;
				}
				primitive_out.index_buf = Index(new IndexTextureBuffer((size_t)primitive_out.type_indices * (size_t)primitive_out.num_face * 3, primitive_out.type_indices));
				primitive_out.index_buf->upload(p_indices);

				primitive_out.cpu_indices = std::unique_ptr<std::vector<uint8_t>>(new std::vector<uint8_t>(primitive_out.index_buf->m_size));
				memcpy(primitive_out.cpu_indices->data(), p_indices, primitive_out.index_buf->m_size);
			}
			else
			{
				primitive_out.num_face = primitive_out.num_pos / 3;
			}

			GeometrySet& geometry = primitive_out.geometry[0];
			primitive_out.cpu_pos = std::unique_ptr<std::vector<glm::vec4>>(new std::vector<glm::vec4>(primitive_out.num_pos));

			for (int k = 0; k < primitive_out.num_pos; k++)
				(*primitive_out.cpu_pos)[k] = glm::vec4(p_pos[k], 1.0f);
			
			geometry.pos_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * primitive_out.num_pos, GL_RGBA32F));
			geometry.pos_buf->upload(primitive_out.cpu_pos->data());

			primitive_out.cpu_norm = std::unique_ptr<std::vector<glm::vec4>>(new std::vector<glm::vec4>(primitive_out.num_pos, glm::vec4(0.0f)));

			geometry.normal_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * primitive_out.num_pos, GL_RGBA32F));
			if (primitive_in.attributes.find("NORMAL") != primitive_in.attributes.end())
			{
				int id_norm_in = primitive_in.attributes["NORMAL"];
				tinygltf::Accessor& acc_norm_in = model.accessors[id_norm_in];
				tinygltf::BufferView& view_norm_in = model.bufferViews[acc_norm_in.bufferView];
				const glm::vec3* p_norm = (const glm::vec3*)(model.buffers[view_norm_in.buffer].data.data() + view_norm_in.byteOffset + acc_norm_in.byteOffset);

				for (int k = 0; k < primitive_out.num_pos; k++)
					(*primitive_out.cpu_norm)[k] = glm::vec4(p_norm[k], 0.0f);
			}
			else
			{
				g_calc_normal(primitive_out.num_face, primitive_out.num_pos, primitive_out.type_indices, p_indices, primitive_out.cpu_pos->data(), primitive_out.cpu_norm->data());
			}
			geometry.normal_buf->upload(primitive_out.cpu_norm->data());

			if (primitive_in.attributes.find("COLOR_0") != primitive_in.attributes.end())
			{
				int id_color_in = primitive_in.attributes["COLOR_0"];
				tinygltf::Accessor& acc_color_in = model.accessors[id_color_in];
				tinygltf::BufferView& view_color_in = model.bufferViews[acc_color_in.bufferView];

				primitive_out.color_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * primitive_out.num_pos, GL_RGBA32F));
				
				if (acc_color_in.type == TINYGLTF_TYPE_VEC4)
				{					
					if (acc_color_in.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
					{
						primitive_out.color_buf->upload(model.buffers[view_color_in.buffer].data.data() + view_color_in.byteOffset + acc_color_in.byteOffset);
					}
					else if (acc_color_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						glm::u16vec4* p_in = (glm::u16vec4*)(model.buffers[view_color_in.buffer].data.data() + view_color_in.byteOffset + acc_color_in.byteOffset);
						std::vector<glm::vec4> tmp(primitive_out.num_pos);
						for (int k = 0; k < primitive_out.num_pos; k++)
						{
							tmp[k] = glm::vec4(p_in[k]) / 65535.0f;
						}
						primitive_out.color_buf->upload(tmp.data());
					}
					else if (acc_color_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						glm::u8vec4* p_in = (glm::u8vec4*)(model.buffers[view_color_in.buffer].data.data() + view_color_in.byteOffset + acc_color_in.byteOffset);
						std::vector<glm::vec4> tmp(primitive_out.num_pos);
						for (int k = 0; k < primitive_out.num_pos; k++)
						{
							tmp[k] = glm::vec4(p_in[k]) / 255.0f;
						}
						primitive_out.color_buf->upload(tmp.data());
					}
				}
				else if (acc_color_in.type == TINYGLTF_TYPE_VEC3)
				{	
					if (acc_color_in.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
					{
						glm::vec3* p_in = (glm::vec3*)(model.buffers[view_color_in.buffer].data.data() + view_color_in.byteOffset + acc_color_in.byteOffset);
						std::vector<glm::vec4> tmp(primitive_out.num_pos);
						for (int k = 0; k < primitive_out.num_pos; k++)
						{
							tmp[k] = glm::vec4(p_in[k], 1.0f);
						}
						primitive_out.color_buf->upload(tmp.data());
					}
					else if (acc_color_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						glm::u16vec3* p_in = (glm::u16vec3*)(model.buffers[view_color_in.buffer].data.data() + view_color_in.byteOffset + acc_color_in.byteOffset);
						std::vector<glm::vec4> tmp(primitive_out.num_pos);
						for (int k = 0; k < primitive_out.num_pos; k++)
						{
							tmp[k] = glm::vec4(glm::vec3(p_in[k])/65535.0f, 1.0f);
						}
						primitive_out.color_buf->upload(tmp.data());
					}
					else if (acc_color_in.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						glm::u8vec3* p_in = (glm::u8vec3*)(model.buffers[view_color_in.buffer].data.data() + view_color_in.byteOffset + acc_color_in.byteOffset);
						std::vector<glm::vec4> tmp(primitive_out.num_pos);
						for (int k = 0; k < primitive_out.num_pos; k++)
						{
							tmp[k] = glm::vec4(glm::vec3(p_in[k]) / 255.0f, 1.0f);
						}
						primitive_out.color_buf->upload(tmp.data());
					}
				}
			}		

			if (primitive_in.attributes.find("TEXCOORD_0") != primitive_in.attributes.end())
			{
				int id_uv_in = primitive_in.attributes["TEXCOORD_0"];
				tinygltf::Accessor& acc_uv_in = model.accessors[id_uv_in];
				tinygltf::BufferView& view_uv_in = model.bufferViews[acc_uv_in.bufferView];		

				const glm::vec2* p_uv = (const glm::vec2*)(model.buffers[view_uv_in.buffer].data.data() + view_uv_in.byteOffset + acc_uv_in.byteOffset);
				primitive_out.uv_buf = Attribute(new TextureBuffer(sizeof(glm::vec2) * primitive_out.num_pos, GL_RG32F));
				primitive_out.uv_buf->upload(p_uv);

				primitive_out.cpu_uv = std::unique_ptr<std::vector<glm::vec2>>(new std::vector<glm::vec2>(primitive_out.num_pos));
				memcpy(primitive_out.cpu_uv->data(), p_uv, primitive_out.uv_buf->m_size);
			}

			std::vector<glm::vec4> tangent;
			std::vector<glm::vec4> bitangent;
			if (has_tangent)
			{
				tangent.resize(primitive_out.num_pos, glm::vec4(0.0f));
				bitangent.resize(primitive_out.num_pos, glm::vec4(0.0f));
				g_calc_tangent(primitive_out.num_face, primitive_out.num_pos, primitive_out.type_indices, p_indices, primitive_out.cpu_pos->data(), primitive_out.cpu_uv->data(), tangent.data(), bitangent.data());
				geometry.tangent_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * primitive_out.num_pos, GL_RGBA32F));
				geometry.bitangent_buf = Attribute(new TextureBuffer(sizeof(glm::vec4) * primitive_out.num_pos, GL_RGBA32F));
				geometry.tangent_buf->upload(tangent.data());
				geometry.bitangent_buf->upload(bitangent.data());
			}
			
		}		
	}
	
	model_out->m_nodes.resize(num_nodes);
	for (size_t i = 0; i < num_nodes; i++)
	{
		tinygltf::Node& node_in = model.nodes[i];
		Node& node_out = model_out->m_nodes[i];		
		node_out.children = node_in.children;

		if (node_in.matrix.size() > 0)
		{
			glm::mat4 matrix;
			for (int c = 0; c < 16; c++)
			{
				matrix[c/4][c%4] = (float)node_in.matrix[c];
			}
			glm::quat rot;
			glm::vec3 skew;
			glm::vec4 persp;
			glm::decompose(matrix, node_out.scale, node_out.rotation, node_out.translation, skew, persp);
		}
		else
		{
			if (node_in.translation.size() > 0)
			{
				node_out.translation.x = (float)node_in.translation[0];
				node_out.translation.y = (float)node_in.translation[1];
				node_out.translation.z = (float)node_in.translation[2];
			}
			else
			{
				node_out.translation = { 0.0f, 0.0f, 0.0f };
			}

			if (node_in.rotation.size() > 0)
			{
				node_out.rotation.x = (float)node_in.rotation[0];
				node_out.rotation.y = (float)node_in.rotation[1];
				node_out.rotation.z = (float)node_in.rotation[2];
				node_out.rotation.w = (float)node_in.rotation[3];
			}
			else
			{
				node_out.rotation = glm::identity<glm::quat>();
			}

			if (node_in.scale.size() > 0)
			{
				node_out.scale.x = (float)node_in.scale[0];
				node_out.scale.y = (float)node_in.scale[1];
				node_out.scale.z = (float)node_in.scale[2];
			}
			else
			{
				node_out.scale = { 1.0f, 1.0f, 1.0f };
			}
		}

		std::string name = node_in.name;
		if (name == "")
		{
			char node_name[32];
			sprintf(node_name, "node_%d", (int)i);
			name = node_name;
		}

		model_out->m_node_dict[name] = i;
	}
	model_out->m_roots = model.scenes[0].nodes;

	for (size_t i = 0; i < num_nodes; i++)
	{
		tinygltf::Node& node_in = model.nodes[i];
		Node& node_out = model_out->m_nodes[i];
		int j = node_in.mesh;

		if (j >= 0)
		{	
			Mesh& mesh_out = model_out->m_meshs[j];
			mesh_out.node_id = i;
			mesh_out.skin_id = node_in.skin;

			std::string name = node_in.name;
			if (name == "")
			{
				char mesh_name[32];
				sprintf(mesh_name, "mesh_%d", j);
				name = mesh_name;
			}
			model_out->m_mesh_dict[name] = j;
		}
	}	

	model_out->updateNodes();
	model_out->calculate_bounding_box();
}

void GLTFLoader::LoadModelFromFile(GLTFModel* model_out, const char* filename)
{
	std::string err;
	std::string warn;
	tinygltf::TinyGLTF loader;
	tinygltf::Model model;
	loader.LoadBinaryFromFile(&model, &err, &warn, filename);
	load_model(model, model_out);
}

void GLTFLoader::LoadModelFromMemory(GLTFModel* model_out, unsigned char* data, size_t size)
{
	std::string err;
	std::string warn;
	tinygltf::TinyGLTF loader;
	tinygltf::Model model;
	loader.LoadBinaryFromMemory(&model, &err, &warn, data, size);
	load_model(model, model_out);
}
