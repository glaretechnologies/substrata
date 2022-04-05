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
=====================================================================*/
class MeshManager
{
public:
	MeshManager();
	~MeshManager();
	//bool isMeshDataInserted(const std::string& model_url) const;
	//bool isMeshDataInsertedNoLock(const std::string& model_url) const;

	Reference<MeshData> insertMeshes(const std::string& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata, Reference<RayMesh>& raymesh);

	Reference<MeshData> getMeshData(const std::string& model_url);

	void meshDataBecameUnused(const MeshData* meshdata);

	GLMemUsage getTotalMemUsage() const;

	Mutex& getMutex() { return mutex; }

	//MeshData& operator [] (const std::string& model_url) { return model_URL_to_mesh_map[model_url]; }

	//std::map<std::string, MeshData>::iterator find(const std::string& model_url) { return model_URL_to_mesh_map.find(model_url); }

private:
	mutable Mutex mutex;
	std::map<std::string, Reference<MeshData> > model_URL_to_mesh_map;
};
