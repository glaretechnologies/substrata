/*=====================================================================
MeshManager.h
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "PhysicsObject.h"
#include "../shared/URLString.h"
#include <opengl/GLMemUsage.h>
#include <simpleraytracer/raymesh.h>
#include <utils/ManagerWithCache.h>
#include <map>
class OpenGLMeshRenderData;
class MeshManager;


struct MeshData
{
	MeshData(const URLString& model_URL_, Reference<OpenGLMeshRenderData> gl_meshdata_, MeshManager* mesh_manager_) : model_url(model_URL_), gl_meshdata(gl_meshdata_), voxel_subsample_factor(1), refcount(0), mesh_manager(mesh_manager_) {}

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


	void meshDataBecameUsed() const; // Called when an object starts using this mesh.
	void meshDataBecameUnused() const; // Called by decRefCount()


	URLString model_url;

	Reference<OpenGLMeshRenderData> gl_meshdata;

	int voxel_subsample_factor;

	mutable glare::AtomicInt refcount;

	MeshManager* mesh_manager;
};


struct PhysicsShapeData
{
	PhysicsShapeData(const URLString& model_URL_, PhysicsObject::ShapeType shape_type_, PhysicsShape physics_shape_, MeshManager* mesh_manager_) 
	:	model_url(model_URL_), shape_type(shape_type_), physics_shape(physics_shape_), refcount(0), mesh_manager(mesh_manager_) {}

	//------------------- Custom ReferenceCounted stuff, so we can call shapeDataBecameUnused() ---------------------
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


	void shapeDataBecameUsed() const; // Called when an object starts using this physics shape.
	void shapeDataBecameUnused() const; // Called by decRefCount()


	URLString model_url;
	PhysicsObject::ShapeType shape_type; // Was the shape built as a tri mesh, convex hull, or box>

	PhysicsShape physics_shape;

	mutable glare::AtomicInt refcount;

	MeshManager* mesh_manager;
};


// We build a different physics mesh depending on the shape type, e.g. for dynamic objects.
// NOTE: copied from MainWindow::ModelProcessingKey
struct MeshManagerPhysicsShapeKey
{
	MeshManagerPhysicsShapeKey() {}
	MeshManagerPhysicsShapeKey(const URLString& URL_, PhysicsObject::ShapeType shape_type_) : URL(URL_), shape_type(shape_type_) {}

	URLString URL;
	PhysicsObject::ShapeType shape_type;

	bool operator < (const MeshManagerPhysicsShapeKey& other) const
	{
		if(URL < other.URL)
			return true;
		else if(URL > other.URL)
			return false;
		else
			return shape_type < other.shape_type;
	}
	bool operator == (const MeshManagerPhysicsShapeKey& other) const { return URL == other.URL && shape_type == other.shape_type; }
};
struct MeshManagerPhysicsShapeKeyHasher
{
	size_t operator() (const MeshManagerPhysicsShapeKey& key) const
	{
		std::hash<string_view> h;
		return h(key.URL);
	}
};


/*=====================================================================
MeshManager
-----------
Caches OpenGLMeshRenderData and physics shapes loaded from disk and built.

In general all methods are called on the main thread (in particular meshDataBecameUnused())
=====================================================================*/
class MeshManager
{
public:
	MeshManager();
	~MeshManager();

	void clear();

	Reference<MeshData> insertMesh(const URLString& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata);
	Reference<PhysicsShapeData> insertPhysicsShape(const MeshManagerPhysicsShapeKey& key, PhysicsShape& physics_shape);

	Reference<MeshData> getMeshData(const URLString& model_url) const; // Returns null reference if not found.
	Reference<PhysicsShapeData> getPhysicsShapeData(const MeshManagerPhysicsShapeKey& key); // Returns null reference if not found.

	void meshDataBecameUsed(const MeshData* meshdata);
	void meshDataBecameUnused(const MeshData* meshdata); // Called by decRefCount()
	void physicsShapeDataBecameUsed(const PhysicsShapeData* meshdata);
	void physicsShapeDataBecameUnused(const PhysicsShapeData* meshdata); // Called by decRefCount()

	// The mesh manager keeps a running total of the amount of memory used by inserted meshes.  Therefore it needs to be informed if the size of one of them changes.
	void meshMemoryAllocatedChanged(const GLMemUsage& old_mem_usage, const GLMemUsage& new_mem_usage);

	std::string getDiagnostics() const;

	void trimMeshMemoryUsage();

private:
	void checkRunningOnMainThread() const;

	ManagerWithCache<URLString, Reference<MeshData>, URLStringHasher> model_URL_to_mesh_map;

	ManagerWithCache<MeshManagerPhysicsShapeKey, Reference<PhysicsShapeData>, MeshManagerPhysicsShapeKeyHasher> physics_shape_map;

	uint64 main_thread_id;

	uint64 mesh_CPU_mem_usage; // Running sum of CPU RAM used by inserted meshes.
	uint64 mesh_GPU_mem_usage; // Running sum of GPU RAM used by inserted meshes.

	uint64 shape_mem_usage; // Running sum of CPU RAM used by inserted physics shapes.
};
