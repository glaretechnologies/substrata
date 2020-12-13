/*=====================================================================
GroundPatch.h
-------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#pragma once


//#include "TimeStamp.h"
//#include "UserID.h"
#include <vec3.h>
#include <vec2.h>
#include <physics/jscol_aabbox.h>
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>
struct GLObject;
class PhysicsObject;
class OpenGLEngine;
class RayMesh;
namespace Indigo { class TaskManager; }


struct GroundPatchUID
{
	Vec3<int> coords;
};


inline void writeToStream(const GroundPatchUID& uid, OutStream& stream)
{
	stream.writeData(&uid.coords.x, sizeof(uid.coords));
}


inline GroundPatchUID readGroundPatchUIDFromStream(InStream& stream)
{
	GroundPatchUID uid;
	stream.readData(&uid.coords, sizeof(uid.coords));
	return uid;
}


/*=====================================================================
GroundPatch
-----------

=====================================================================*/
class GroundPatch : public ThreadSafeRefCounted
{
public:
	GroundPatch();
	~GroundPatch();

	GroundPatchUID uid;
	// For client:
	/*enum State
	{
		State_JustCreated,
		State_Alive,
		State_Dead
	};

	State state;*/
	bool from_remote_dirty; // parcel has been changed remotely
	bool from_local_dirty;  // parcel has been changed locally


	std::string lightmap_url;

#if GUI_CLIENT
	Reference<GLObject> opengl_engine_ob;
	//Reference<PhysicsObject> physics_object;

	Reference<GLObject> makeOpenGLObject(Reference<OpenGLEngine>& opengl_engine, bool write_privileges); // Shader program will be set by calling code later.

	//Reference<PhysicsObject> makePhysicsObject(Reference<RayMesh>& unit_cube_raymesh, Indigo::TaskManager& task_manager);
#endif

};


typedef Reference<GroundPatch> GroundPatchRef;



void writeToStream(const GroundPatch& ground_patch, OutStream& stream);
void readFromStream(InStream& stream, GroundPatch& ground_patch);

void writeToNetworkStream(const GroundPatch& ground_patch, OutStream& stream); // write without version
void readFromNetworkStreamGivenID(InStream& stream, GroundPatch& ground_patch);


struct GroundPatchRefHash
{
	size_t operator() (const GroundPatchRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
