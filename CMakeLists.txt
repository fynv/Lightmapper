cmake_minimum_required (VERSION 3.0)
project(Lightmapper)

SET(GLFW_BUILD_DOCS false)
SET(GLFW_INSTALL false)

add_subdirectory(thirdparty/glfw)
add_subdirectory(thirdparty/glew-cmake)

set (INCLUDE_DIR
thirdparty/glfw/include
thirdparty/glew-cmake/include
thirdparty/glm/glm
thirdparty/JVB
thirdparty/xatlas
thirdparty
.
)


if (WIN32) 
set (DEFINES  ${DEFINES}
-D"_CRT_SECURE_NO_DEPRECATE"  
-D"_SCL_SECURE_NO_DEPRECATE" 
-D"_CRT_SECURE_NO_WARNINGS"
)
else()
add_compile_options(-fPIC)
endif()

set(CMAKE_CXX_STANDARD 17)

include_directories(${INCLUDE_DIR})
link_directories(${LIB_DIR})

set (SOURCE_JVB
	thirdparty/JVB/Util.h
	thirdparty/JVB/Constructors.h
	thirdparty/JVB/BitArray.h
	thirdparty/JVB/Compare.h
	thirdparty/JVB/Sort.h
	thirdparty/JVB/AABB.cpp
	thirdparty/JVB/AABB.h
	thirdparty/JVB/Triangle.cpp
	thirdparty/JVB/Triangle.h
	thirdparty/JVB/BVH.cpp
	thirdparty/JVB/BVH.h
	thirdparty/JVB/BVH8Converter.cpp
	thirdparty/JVB/BVH8Converter.h
	thirdparty/JVB/BVHPartitions.cpp
	thirdparty/JVB/BVHPartitions.h
	thirdparty/JVB/SAHBuilder.cpp
	thirdparty/JVB/SAHBuilder.h
	thirdparty/JVB/Ray.h
)

set (SOURCE_XATLAS
	thirdparty/xatlas/xatlas.cpp
	thirdparty/xatlas/xatlas.h
)

set (SOURCE_UTILS
	utils/Image.cpp
	utils/Image.h
	utils/Semaphore.h
	utils/Utils.h
	thirdparty/crc64/crc64.cpp
    thirdparty/crc64/crc64.h
)

set(SOURCE_CORE
	core/Object3D.cpp
	core/Object3D.h
	core/CWBVH.cpp
	core/CWBVH.h
)

set(SOURCE_CAMERAS
	cameras/Camera.cpp
	cameras/Camera.h
	cameras/PerspectiveCamera.cpp
	cameras/PerspectiveCamera.h
)

set(SOURCE_SCENES
	scenes/scene.cpp
	scenes/scene.h
)

set (SOURCE_BACKGROUNDS
	backgrounds/Background.cpp
	backgrounds/Background.h	
)

set (SOURCE_MATERIALS
	materials/Material.cpp
	materials/Material.h
	materials/MeshStandardMaterial.cpp
	materials/MeshStandardMaterial.h
)

set (SOURCE_MODELS
	models/ModelComponents.cpp
	models/ModelComponents.h
	models/SimpleModel.cpp
	models/SimpleModel.h
	models/GLTFModel.cpp
	models/GLTFModel.h
	models/GeometryCreator.cpp
	models/GeometryCreator.h
)

set (SOURCE_LIGHTS
	lights/Light.cpp
	lights/Light.h
	lights/DirectionalLight.cpp
	lights/DirectionalLight.h
	lights/DirectionalLightShadow.cpp
	lights/DirectionalLightShadow.h
	lights/lights.h
)

set (SOURCE_LOADERS
	loaders/FileLoader.cpp
	loaders/FileLoader.h
	loaders/ImageLoader.cpp
	loaders/ImageLoader.h
	loaders/GLTFLoader.cpp
	loaders/GLTFLoader.h
)

set (SOURCE_RENDERERS
	renderers/GLUtils.cpp
	renderers/GLUtils.h
	renderers/GLRenderTarget.cpp
	renderers/GLRenderTarget.h
	renderers/GLRenderer.cpp
	renderers/GLRenderer.h
	renderers/BVHRenderTarget.cpp
	renderers/BVHRenderTarget.h
	renderers/BVHRenderer.cpp
	renderers/BVHRenderer.h
	renderers/LightmapRenderTarget.cpp
	renderers/LightmapRenderTarget.h
	renderers/LightmapRayList.cpp
	renderers/LightmapRayList.h
)

set (SOURCE_RENDERER_ROUTINES
	renderers/routines/WeightedOIT.cpp
	renderers/routines/WeightedOIT.h
	renderers/routines/DrawSkyBox.cpp
	renderers/routines/DrawSkyBox.h
	renderers/routines/DrawHemisphere.cpp
	renderers/routines/DrawHemisphere.h
	renderers/routines/DirectionalShadowCast.cpp
	renderers/routines/DirectionalShadowCast.h
	renderers/routines/DepthOnly.cpp
	renderers/routines/DepthOnly.h
	renderers/routines/StandardRoutine.cpp
	renderers/routines/StandardRoutine.h
	renderers/routines/DrawTexture.cpp
	renderers/routines/DrawTexture.h
	renderers/routines/RasterizeAtlas.cpp
	renderers/routines/RasterizeAtlas.h	
)

set (SOURCE_RENDERER_BVH_ROUTINES
	renderers/bvh_routines/PrimitiveBatch.cpp
	renderers/bvh_routines/PrimitiveBatch.h
	renderers/bvh_routines/CompWeightedOIT.cpp
	renderers/bvh_routines/CompWeightedOIT.h
	renderers/bvh_routines/CompSkyBox.cpp
	renderers/bvh_routines/CompSkyBox.h
	renderers/bvh_routines/CompHemisphere.cpp
	renderers/bvh_routines/CompHemisphere.h
	renderers/bvh_routines/BVHDepthOnly.cpp
	renderers/bvh_routines/BVHDepthOnly.h
	renderers/bvh_routines/BVHRoutine.cpp
	renderers/bvh_routines/BVHRoutine.h
	renderers/bvh_routines/LightmapUpdate.cpp
	renderers/bvh_routines/LightmapUpdate.h
	renderers/bvh_routines/LightmapFilter.cpp
	renderers/bvh_routines/LightmapFilter.h
)


add_definitions(${DEFINES})
add_executable(lightmapper 
	${SOURCE_JVB} ${SOURCE_XATLAS} ${SOURCE_UTILS} ${SOURCE_CORE} ${SOURCE_CAMERAS} 
	${SOURCE_SCENES} ${SOURCE_BACKGROUNDS} ${SOURCE_MATERIALS} 
	${SOURCE_MODELS} ${SOURCE_LIGHTS} ${SOURCE_LOADERS} 
	${SOURCE_RENDERERS} ${SOURCE_RENDERER_ROUTINES} ${SOURCE_RENDERER_BVH_ROUTINES} 
	main.cpp test0.hpp test1.hpp)
target_link_libraries(lightmapper libglew_static glfw opengl32)

source_group(JVB FILES ${SOURCE_JVB})
source_group(xatlas FILES ${SOURCE_XATLAS})
source_group(utils FILES ${SOURCE_UTILS})
source_group(core FILES ${SOURCE_CORE})
source_group(cameras FILES ${SOURCE_CAMERAS})
source_group(scenes FILES ${SOURCE_SCENES})
source_group(backgrounds FILES ${SOURCE_BACKGROUNDS})
source_group(materials FILES ${SOURCE_MATERIALS})
source_group(models FILES ${SOURCE_MODELS})
source_group(lights FILES ${SOURCE_LIGHTS})
source_group(loaders FILES ${SOURCE_LOADERS})
source_group(renderers FILES ${SOURCE_RENDERERS})
source_group(renderers/routines FILES ${SOURCE_RENDERER_ROUTINES})
source_group(renderers/bvh_routines FILES ${SOURCE_RENDERER_BVH_ROUTINES})

