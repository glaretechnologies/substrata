/*=====================================================================
MeshManager.h
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include <opengl/GLMemUsage.h>
#include <simpleraytracer/raymesh.h>
#include <map>
class OpenGLMeshRenderData;
class MeshManager;


struct MeshData
{
	MeshData(const std::string& model_URL_, Reference<OpenGLMeshRenderData> gl_meshdata_, MeshManager* mesh_manager_) : model_url(model_URL_), gl_meshdata(gl_meshdata_), refcount(0), mesh_manager(mesh_manager_) {}

	//------------------- Custom ReferenceCounted stuff, so we can call meshDataBecameUnused() ---------------------
	/// Increment reference count
	inline void incRefCount() const
	{
		assert(refcount >= 0);
		refcount++;
	}

	/// Returns previous reference count
	inline int64 decRefCount() const
	{
		const int64 prev_ref_count = refcount;
		refcount--;
		assert(refcount >= 0);

		if(refcount == 1) // If the only reference is now held by the MeshManager:
			meshDataBecameUnused();

		return prev_ref_count;
	}

	inline int64 getRefCount() const
	{
		assert(refcount >= 0);
		return refcount;
	}
	//----------------------------------------


	void meshDataBecameUnused() const;


	std::string model_url;

	Reference<OpenGLMeshRenderData> gl_meshdata;

	mutable glare::AtomicInt refcount;

	MeshManager* mesh_manager;
};


struct PhysicsShapeData
{
	PhysicsShapeData(const std::string& model_URL_, bool dynamic_, PhysicsShape physics_shape_, MeshManager* mesh_manager_) : model_url(model_URL_), dynamic(dynamic_), physics_shape(physics_shape_), refcount(0), mesh_manager(mesh_manager_) {}

	//------------------- Custom ReferenceCounted stuff, so we can call meshDataBecameUnused() ---------------------
	/// Increment reference count
	inline void incRefCount() const
	{
		assert(refcount >= 0);
		refcount++;
	}

	/// Returns previous reference count
	inline int64 decRefCount() const
	{
		const int64 prev_ref_count = refcount;
		refcount--;
		assert(refcount >= 0);

		if(refcount == 1) // If the only reference is now held by the MeshManager:
			shapeDataBecameUnused();

		return prev_ref_count;
	}

	inline int64 getRefCount() const
	{
		assert(refcount >= 0);
		return refcount;
	}
	//----------------------------------------


	void shapeDataBecameUnused() const;


	std::string model_url;
	bool dynamic; // Is the physics shape built for a dynamic physics object?  If so it will be a convex hull.

	PhysicsShape physics_shape;

	mutable glare::AtomicInt refcount;

	MeshManager* mesh_manager;
};


// We build a different physics mesh for dynamic objects, so we need to keep track of which mesh we are building.
// NOTE: copied from MainWindow::ModelProcessingKey
struct MeshManagerPhysicsShapeKey
{
	MeshManagerPhysicsShapeKey(const std::string& URL_, const bool dynamic_physics_shape_) : URL(URL_), dynamic_physics_shape(dynamic_physics_shape_) {}

	std::string URL;
	bool dynamic_physics_shape;

	bool operator < (const MeshManagerPhysicsShapeKey& other) const
	{
		if(URL < other.URL)
			return true;
		else if(URL > other.URL)
			return false;
		else
			return !dynamic_physics_shape && other.dynamic_physics_shape;
	}
	bool operator == (const MeshManagerPhysicsShapeKey& other) const { return URL == other.URL && dynamic_physics_shape == other.dynamic_physics_shape; }
};


/*=====================================================================
MeshManager
-----------
Caches meshes and OpenGL data loaded from disk and built.
RayMesh and OpenGLMeshRenderData in particular.

NOTE: Do we need to make this class threadsafe?  Or are all methods called on the main thread (in particular meshDataBecameUnused()?)
=====================================================================*/
class MeshManager
{
public:
	MeshManager();
	~MeshManager();

	void clear();

	Reference<MeshData> insertMesh(const std::string& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata);
	Reference<PhysicsShapeData> insertPhysicsShape(const MeshManagerPhysicsShapeKey& key, PhysicsShape& physics_shape);

	Reference<MeshData> getMeshData(const std::string& model_url);
	Reference<PhysicsShapeData> getPhysicsShapeData(const MeshManagerPhysicsShapeKey& key);

	void meshDataBecameUnused(const MeshData* meshdata);
	void physicsShapeDataBecameUnused(const PhysicsShapeData* meshdata);

	GLMemUsage getTotalMemUsage() const;
	size_t getNumGLMeshDataObs() const { return model_URL_to_mesh_map.size(); }
	size_t getNumPhysicsShapeDataObs() const { return physics_shape_map.size(); }

	//Mutex& getMutex() { return mutex; }
private:
	//mutable Mutex mutex;
	std::map<std::string, Reference<MeshData> > model_URL_to_mesh_map;

	std::map<MeshManagerPhysicsShapeKey, Reference<PhysicsShapeData>> physics_shape_map;

	uint64 main_thread_id;
};
