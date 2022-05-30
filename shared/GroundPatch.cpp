/*=====================================================================
GroundPatch.cpp
---------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#include "GroundPatch.h"


#include <Exception.h>
#include <StringUtils.h>
#include <ContainerUtils.h>
#include <ConPrint.h>
#include <ShouldCancelCallback.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#include "opengl/OpenGLMeshRenderData.h"
#endif
#include "../gui_client/PhysicsObject.h"
#include <StandardPrintOutput.h>


GroundPatch::GroundPatch()
:	//state(State_JustCreated),
	from_remote_dirty(false),
	from_local_dirty(false)
{
}


GroundPatch::~GroundPatch()
{

}


#if GUI_CLIENT


Reference<GLObject> GroundPatch::makeOpenGLObject(Reference<OpenGLEngine>& opengl_engine, bool write_privileges)
{
	Reference<OpenGLMeshRenderData> unit_quad_mesh_data = opengl_engine->getUnitQuadMeshData();

	Reference<GLObject> ob = new GLObject();

	const float PATCH_W = 25.0f;

	ob->ob_to_world_matrix = Matrix4f::translationMatrix(this->uid.coords.x * PATCH_W, this->uid.coords.y * PATCH_W, 0) * Matrix4f::scaleMatrix(PATCH_W, PATCH_W, 1);


	ob->mesh_data = unit_quad_mesh_data;
	ob->materials.resize(1);
	ob->materials[0].albedo_rgb = Colour3f(0.9f);
	ob->materials[0].alpha = 0.5f;
	ob->materials[0].transparent = true;

	ob->materials[0].tex_matrix = Matrix2f(PATCH_W, 0, 0, PATCH_W);

	try
	{
		ob->materials[0].albedo_texture = opengl_engine->getTexture("resources/obstacle.png");
	}
	catch(glare::Exception& e)
	{
		assert(0);
		conPrint("ERROR: " + e.what());
	}

	ob->materials[0].roughness = 0.8f;
	ob->materials[0].fresnel_scale = 0.5f;

	return ob;
}


#endif // GUI_CLIENT


static const uint32 GROUNDPATCH_SERIALISATION_VERSION = 1;
/*
Version 1: created.
*/


static void writeToStreamCommon(const GroundPatch& ground_patch, OutStream& stream)
{
	stream.writeStringLengthFirst(ground_patch.lightmap_url);
}


static void readFromStreamCommon(InStream& stream, uint32 version, GroundPatch& ground_patch) // UID will have been read already
{
	ground_patch.lightmap_url = stream.readStringLengthFirst(10000);
}


void writeToStream(const GroundPatch& ground_patch, OutStream& stream)
{
	// Write version
	stream.writeUInt32(GROUNDPATCH_SERIALISATION_VERSION);

	writeToStreamCommon(ground_patch, stream);
}


void readFromStream(InStream& stream, GroundPatch& ground_patch)
{
	// Read version
	const uint32 version = stream.readUInt32();
	if(version > GROUNDPATCH_SERIALISATION_VERSION)
		throw glare::Exception("GroundPatch readFromStream: Unsupported version " + toString(version) + ", expected " + toString(GROUNDPATCH_SERIALISATION_VERSION) + ".");

	ground_patch.uid = readGroundPatchUIDFromStream(stream);
	
	readFromStreamCommon(stream, version, ground_patch);
}


void writeToNetworkStream(const GroundPatch& ground_patch, OutStream& stream)
{
	writeToStreamCommon(ground_patch, stream);
}


void readFromNetworkStreamGivenID(InStream& stream, GroundPatch& ground_patch) // UID will have been read already
{
	readFromStreamCommon(stream, /*version=*/GROUNDPATCH_SERIALISATION_VERSION, ground_patch);
}
