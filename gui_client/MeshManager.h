/*=====================================================================
MeshManager.h
-------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <opengl/OpenGLEngine.h>
#include <simpleraytracer/raymesh.h>


class MeshManager;


struct MeshData
{
	MeshData() : refcount(0), mesh_manager(NULL) {}

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

	Reference<RayMesh> raymesh;

	mutable glare::AtomicInt refcount;

	MeshManager* mesh_manager;
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

	Reference<MeshData> insertMeshes(const std::string& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata, Reference<RayMesh>& raymesh);

	Reference<MeshData> getMeshData(const std::string& model_url);

	void meshDataBecameUnused(const MeshData* meshdata);

	GLMemUsage getTotalMemUsage() const;

	//Mutex& getMutex() { return mutex; }
private:
	//mutable Mutex mutex;
	std::map<std::string, Reference<MeshData> > model_URL_to_mesh_map;
	uint64 main_thread_id;
};
