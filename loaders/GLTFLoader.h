#pragma once

#include <vector>

class GLTFModel;
class AnimationClip;
class GLTFLoader
{
public:
	static void LoadModelFromFile(GLTFModel* model, const char* filename);
	static void LoadModelFromMemory(GLTFModel* model, unsigned char* data, size_t size);
	
};