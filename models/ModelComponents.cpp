#include <GL/glew.h>
#include <gtx/hash.hpp>
#include <unordered_set>
#include "ModelComponents.h"

inline unsigned internalFormat(int type_indices)
{
	if (type_indices == 1)
	{
		return GL_R8UI;
	}
	else if (type_indices == 2)
	{
		return GL_R16UI;
	}
	else if (type_indices == 4)
	{
		return GL_R32UI;
	}

}

IndexTextureBuffer::IndexTextureBuffer(size_t size, int type_indices)
	: TextureBuffer(size, internalFormat(type_indices))
{
	
}

IndexTextureBuffer::~IndexTextureBuffer()
{

}

struct ModelConst
{
	glm::mat4 ModelMat;
	glm::mat4 NormalMat;
};

Mesh::Mesh()
{
	model_constant = std::unique_ptr<GLDynBuffer>(new GLDynBuffer(sizeof(ModelConst)));
}

#include "xatlas.h"

Lightmap::Lightmap(int width, int height)
	:width(width), height(height)
{
	lightmap = std::unique_ptr<GLTexture2D>(new GLTexture2D);
	glBindTexture(GL_TEXTURE_2D, lightmap->tex_id);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, width, height);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}


Lightmap::Lightmap(const std::vector<Primitive*>& primitives, const std::vector<glm::mat4>& trans, int texelsPerUnit)
{
	int num_prims = (int)primitives.size();

	xatlas::Atlas* atlas = xatlas::Create();

	for (int i = 0; i < num_prims; i++)
	{
		glm::mat4 model_mat = trans[i];
		glm::mat4 norm_mat = glm::transpose(glm::inverse(model_mat));

		Primitive* prim = primitives[i];
		int num_pos = prim->num_pos;
				
		std::vector<glm::vec3> pos(num_pos);
		std::vector<glm::vec3> norm(num_pos);		
		
		for (int j = 0; j < num_pos; j++)
		{
			pos[j] = glm::vec3(model_mat * (*prim->cpu_pos)[j]);
			norm[j] = glm::vec3(norm_mat * (*prim->cpu_norm)[j]);
		}

		std::vector<glm::ivec3> faces;
		if (prim->index_buf != nullptr)
		{
			int num_face = prim->num_face;
			faces.resize(num_face);
			if (prim->type_indices == 1)
			{
				const glm::u8vec3* faces_in = (const glm::u8vec3*)prim->cpu_indices->data();
				for (int j = 0; j < num_face; j++)
				{
					faces[j] = glm::ivec3(faces_in[j]);
				}
			}
			else if (prim->type_indices == 2)
			{
				const glm::u16vec3* faces_in = (const glm::u16vec3*)prim->cpu_indices->data();
				for (int j = 0; j < num_face; j++)
				{
					faces[j] = glm::ivec3(faces_in[j]);
				}
			}
			else if (prim->type_indices == 4)
			{
				const glm::u32vec3* faces_in = (const glm::u32vec3*)prim->cpu_indices->data();
				for (int j = 0; j < num_face; j++)
				{
					faces[j] = glm::ivec3(faces_in[j]);
				}
			}
		}
		else
		{
			int num_face = num_pos / 3;
			faces.resize(num_face);
			int* ind = (int*)faces.data();
			for (int i = 0; i < num_face; i++)
			{
				ind[i] = i;
			}
		}

		xatlas::MeshDecl meshDecl;
		meshDecl.vertexCount = (unsigned)pos.size();
		meshDecl.vertexPositionData = pos.data();
		meshDecl.vertexPositionStride = sizeof(float) * 3;
		meshDecl.vertexNormalData = norm.data();
		meshDecl.vertexNormalStride = sizeof(float) * 3;

		if (prim->cpu_uv!=nullptr)
		{
			meshDecl.vertexUvData = prim->cpu_uv->data();
			meshDecl.vertexUvStride = sizeof(float) * 2;
		}

		meshDecl.indexCount = (uint32_t)(faces.size() * 3);
		meshDecl.indexData = faces.data();
		meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

		xatlas::AddMeshError error = xatlas::AddMesh(atlas, meshDecl, 1);
		if (error != xatlas::AddMeshError::Success) {
			xatlas::Destroy(atlas);
			printf("\rError adding mesh: %s\n", xatlas::StringForEnum(error));
		}
	}

	xatlas::ChartOptions chartOptions;
	xatlas::PackOptions packOptions;
	packOptions.padding = 1;
	packOptions.texelsPerUnit = float(texelsPerUnit);

	printf("Generating atlas...\n");
	xatlas::Generate(atlas, chartOptions, packOptions);
	printf("Done.\n");

	width = atlas->width;
	height = atlas->height;
	texels_per_unit = texelsPerUnit;

	glm::vec2 img_size = glm::vec2(width, height);

	for (int i = 0; i < num_prims; i++)
	{
		Primitive* prim = primitives[i];
		const xatlas::Mesh& atlas_mesh = atlas->meshes[i];

		prim->cpu_lightmap_indices = std::unique_ptr<std::vector<int>>(new std::vector<int>(atlas_mesh.indexCount));
		for (int j = 0; j < atlas_mesh.indexCount; j++)
		{
			(*prim->cpu_lightmap_indices)[j] = (int)atlas_mesh.indexArray[j];
		}
		prim->lightmap_indices = Index(new IndexTextureBuffer(sizeof(int) * atlas_mesh.indexCount, 4));
		prim->lightmap_indices->upload(prim->cpu_lightmap_indices->data());
		
		prim->cpu_lightmap_uv = std::unique_ptr<std::vector<glm::vec2>>(new std::vector<glm::vec2>(atlas_mesh.vertexCount));
		for (int j = 0; j < atlas_mesh.vertexCount; j++)
		{
			const float* p_uv = atlas_mesh.vertexArray[j].uv;
			(*prim->cpu_lightmap_uv)[j] = (glm::vec2(p_uv[0], p_uv[1]) + 0.5f)/ img_size;
		}
		prim->lightmap_uv_buf = (Attribute)(new TextureBuffer(sizeof(glm::vec2) * atlas_mesh.vertexCount, GL_RG32F));
		prim->lightmap_uv_buf->upload(prim->cpu_lightmap_uv->data());
	}

	xatlas::Destroy(atlas);
	

	lightmap = std::unique_ptr<GLTexture2D>(new GLTexture2D);
	glBindTexture(GL_TEXTURE_2D, lightmap->tex_id);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, width, height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

}

