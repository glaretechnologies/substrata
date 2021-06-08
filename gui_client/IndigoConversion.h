/*=====================================================================
IndigoConversion.h
------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


// #define INDIGO_SUPPORT 1


#include "../shared/WorldMaterial.h"
#include "../shared/WorldObject.h"
#include <IndigoMaterial.h>
#include <SceneNodeModel.h>
class ResourceManager;


/*=====================================================================
IndigoConversion
----------------
Conversion from world objects and materials to Indigo API scene graph nodes.
=====================================================================*/
class IndigoConversion
{
public:
#if INDIGO_SUPPORT
	static Indigo::SceneNodeMaterialRef convertMaterialToIndigoMat(const WorldMaterial& mat, ResourceManager& resource_manager);

#if GUI_CLIENT
	static Indigo::SceneNodeMeshRef convertMesh(const WorldObject& object);

	static Indigo::SceneNodeModelRef convertObject(const WorldObject& object, ResourceManager& resource_manager);
#endif

#endif
};
