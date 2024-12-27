/*=====================================================================
IndigoConversion.h
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <Reference.h>
class WorldMaterial;
class WorldObject;
class ResourceManager;
namespace Indigo { class SceneNodeMaterial; }
namespace Indigo { class SceneNodeMesh; }
namespace Indigo { class SceneNodeModel; }


/*=====================================================================
IndigoConversion
----------------
Conversion from Substrata world objects and materials to Indigo API scene graph nodes.
=====================================================================*/
class IndigoConversion
{
public:
#if INDIGO_SUPPORT
	static Reference<Indigo::SceneNodeMaterial> convertMaterialToIndigoMat(const WorldMaterial& mat, ResourceManager& resource_manager);

#if GUI_CLIENT
	static Reference<Indigo::SceneNodeMesh> convertMesh(const WorldObject& object, ResourceManager& resource_manager);

	static Reference<Indigo::SceneNodeModel> convertObject(const WorldObject& object, ResourceManager& resource_manager);
#endif

#endif // INDIGO_SUPPORT
};
