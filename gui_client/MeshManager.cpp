/*=====================================================================
MeshManager.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshManager.h"


#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <utils/PlatformUtils.h>


void MeshData::meshDataBecameUsed() const
{
	if(mesh_manager)
		mesh_manager->meshDataBecameUsed(this);
}


void MeshData::meshDataBecameUnused() const
{
	if(mesh_manager)
		mesh_manager->meshDataBecameUnused(this);
}



void PhysicsShapeData::shapeDataBecameUsed() const
{
	if(mesh_manager)
		mesh_manager->physicsShapeDataBecameUsed(this);
}


void PhysicsShapeData::shapeDataBecameUnused() const
{
	if(mesh_manager)
		mesh_manager->physicsShapeDataBecameUnused(this);
}


inline void MeshManager::checkRunningOnMainThread() const
{
#if !defined(EMSCRIPTEN)
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
#endif
}



MeshManager::MeshManager()
{
#if !defined(EMSCRIPTEN)
	main_thread_id = PlatformUtils::getCurrentThreadID();
#endif
	mesh_CPU_mem_usage = 0;
	mesh_GPU_mem_usage = 0;
	shape_mem_usage = 0;
}


MeshManager::~MeshManager()
{
	checkRunningOnMainThread();

	clear();
}


void MeshManager::clear()
{
	checkRunningOnMainThread();

	// Before we clear model_URL_to_mesh_map, NULL out references to mesh_manager so meshDataBecameUnused() doesn't trigger.
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
		it->second.value->mesh_manager = NULL;

	for(auto it = physics_shape_map.begin(); it != physics_shape_map.end(); ++it)
		it->second.value->mesh_manager = NULL;

	model_URL_to_mesh_map.clear();
	physics_shape_map.clear();

	mesh_CPU_mem_usage = 0;
	mesh_GPU_mem_usage = 0;
	shape_mem_usage = 0;
}


Reference<MeshData> MeshManager::insertMesh(const URLString& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata)
{
	checkRunningOnMainThread();

	// conPrint("Inserting mesh '" + model_url + "' into mesh manager.");

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res == model_URL_to_mesh_map.end())
	{
		Reference<MeshData> mesh_data = new MeshData(model_url, gl_meshdata, /*mesh_manager=*/this);

		model_URL_to_mesh_map.insert(std::make_pair(model_url, mesh_data));

		// Add to running total of memory used
		const GLMemUsage mesh_mem_usage = gl_meshdata->getTotalMemUsage();
		mesh_CPU_mem_usage += mesh_mem_usage.geom_cpu_usage;
		mesh_GPU_mem_usage += mesh_mem_usage.geom_gpu_usage;

		return mesh_data;
	}
	else
	{
		return res->second.value;
	}
}


Reference<PhysicsShapeData> MeshManager::insertPhysicsShape(const MeshManagerPhysicsShapeKey& key, PhysicsShape& physics_shape)
{
	checkRunningOnMainThread();

	auto res = physics_shape_map.find(key);
	if(res == physics_shape_map.end())
	{
		Reference<PhysicsShapeData> shape_data = new PhysicsShapeData(key.URL, key.dynamic_physics_shape, physics_shape, /*mesh_manager=*/this);

		physics_shape_map.insert(std::make_pair(key, shape_data));

		// Add to running total of memory used
		shape_mem_usage += shape_data->physics_shape.size_B;

		return shape_data;
	}
	else
	{
		return res->second.value;
	}
}


Reference<MeshData> MeshManager::getMeshData(const URLString& model_url) const
{
	checkRunningOnMainThread();

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res != model_URL_to_mesh_map.end())
		return res->second.value;
	else
		return NULL;
}


Reference<PhysicsShapeData> MeshManager::getPhysicsShapeData(const MeshManagerPhysicsShapeKey& key)
{
	checkRunningOnMainThread();

	auto res = physics_shape_map.find(key);
	if(res != physics_shape_map.end())
		return res->second.value;
	else
		return NULL;
}


void MeshManager::meshDataBecameUsed(const MeshData* meshdata)
{
	checkRunningOnMainThread();

	//conPrint("meshDataBecameUsed(): '" + meshdata->model_url + "'");

	model_URL_to_mesh_map.itemBecameUsed(meshdata->model_url);
}


void MeshManager::meshDataBecameUnused(const MeshData* meshdata)
{
	checkRunningOnMainThread();

	//conPrint("meshDataBecameUnused():'" + meshdata->model_url + "'");

	model_URL_to_mesh_map.itemBecameUnused(meshdata->model_url);
}


void MeshManager::physicsShapeDataBecameUsed(const PhysicsShapeData* shape_data)
{
	checkRunningOnMainThread();

	//conPrint("physicsShapeDataBecameUsed(): '" + shape_data->model_url + "'");

	physics_shape_map.itemBecameUsed(MeshManagerPhysicsShapeKey(shape_data->model_url, shape_data->dynamic));
}


void MeshManager::physicsShapeDataBecameUnused(const PhysicsShapeData* shape_data)
{
	checkRunningOnMainThread();

	//conPrint("physicsShapeDataBecameUnused(): '" + shape_data->model_url + "'");

	physics_shape_map.itemBecameUnused(MeshManagerPhysicsShapeKey(shape_data->model_url, shape_data->dynamic));
}


// The mesh manager keeps a running total of the amount of memory used by inserted meshes.  Therefore it needs to be informed if the size of one of them changes.
void MeshManager::meshMemoryAllocatedChanged(const GLMemUsage& old_mem_usage, const GLMemUsage& new_mem_usage)
{
	mesh_CPU_mem_usage += (int64)new_mem_usage.geom_cpu_usage - (int64)old_mem_usage.geom_cpu_usage;
	mesh_GPU_mem_usage += (int64)new_mem_usage.geom_gpu_usage - (int64)old_mem_usage.geom_gpu_usage;
}


void MeshManager::trimMeshMemoryUsage()
{
	// Max sizes for used + cached objects for determining cache size.  Objects will be removed from cache if the total size exceeds these thresholds.
	// Use smaller sizes for Emscripten due to hard memory usage limits in the browser.
#if EMSCRIPTEN
	const size_t max_mesh_CPU_mem_usage = 512ull * 1024ull * 1024ull;
#else
	const size_t max_mesh_CPU_mem_usage = 1024ull * 1024ull * 1024ull;
#endif

	const size_t max_mesh_GPU_mem_usage = 1024ull * 1024ull * 1024ull;

#if EMSCRIPTEN
	const size_t max_shape_mem_usage    = 512ull * 1024ull * 1024ull;
#else
	const size_t max_shape_mem_usage    = 1024ull * 1024ull * 1024ull;
#endif

	// Remove textures from unused texture list until we are using <= max_tex_mem_usage
	while(((mesh_CPU_mem_usage > max_mesh_CPU_mem_usage) || (mesh_GPU_mem_usage > max_mesh_GPU_mem_usage)) && (model_URL_to_mesh_map.numUnusedItems() > 0))
	{
		URLString removed_key;
		Reference<MeshData> removed_meshdata;
		const bool removed = model_URL_to_mesh_map.removeLRUUnusedItem(removed_key, removed_meshdata);
		assert(removed);
		if(removed)
		{
			const GLMemUsage mesh_mem_usage = removed_meshdata->gl_meshdata->getTotalMemUsage();

			assert(this->mesh_CPU_mem_usage >= mesh_mem_usage.geom_cpu_usage);
			assert(this->mesh_GPU_mem_usage >= mesh_mem_usage.geom_gpu_usage);

			this->mesh_CPU_mem_usage -= mesh_mem_usage.geom_cpu_usage;
			this->mesh_GPU_mem_usage -= mesh_mem_usage.geom_gpu_usage;
		}
	}

	// Trim physics shapes from unused shape list, until we are using <= max_shape_mem_usage
	while((shape_mem_usage > max_shape_mem_usage) && (physics_shape_map.numUnusedItems() > 0))
	{
		MeshManagerPhysicsShapeKey removed_key;
		Reference<PhysicsShapeData> removed_meshdata;
		const bool removed = physics_shape_map.removeLRUUnusedItem(removed_key, removed_meshdata);
		assert(removed);
		if(removed)
		{
			const size_t the_shape_mem_usage = removed_meshdata->physics_shape.size_B;

			assert(this->shape_mem_usage >= the_shape_mem_usage);

			this->shape_mem_usage -= the_shape_mem_usage;
		}
	}
}


std::string MeshManager::getDiagnostics() const
{
	//Timer timer;

	// Get total size of used (active) gl meshes.
	GLMemUsage active_gl_mesh_usage;
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
		if(model_URL_to_mesh_map.isItemUsed(it->second))
			active_gl_mesh_usage += it->second.value->gl_meshdata->getTotalMemUsage();

	// Get total size of used (active) physics shapes
	size_t active_shape_mem = 0;
	for(auto it = physics_shape_map.begin(); it != physics_shape_map.end(); ++it)
		if(physics_shape_map.isItemUsed(it->second))
			active_shape_mem += it->second.value->physics_shape.size_B;

	const size_t mesh_usage_unused_CPU = this->mesh_CPU_mem_usage - active_gl_mesh_usage.geom_cpu_usage; // CPU Mem usage for unused textures = total mem usage - used mem usage
	const size_t mesh_usage_unused_GPU = this->mesh_GPU_mem_usage - active_gl_mesh_usage.geom_gpu_usage; // GPU Mem usage for unused textures = total mem usage - used mem usage
	
	const size_t shape_usage_unused = this->shape_mem_usage - active_shape_mem; // GPU Mem usage for unused textures = total mem usage - used/active mem usage

	std::string msg;
	msg += "---Mesh Manager---\n";
	msg += "gl meshes:              " + toString(model_URL_to_mesh_map.numUsedItems()) + " / " + toString(model_URL_to_mesh_map.numUnusedItems()) + " / " + toString(model_URL_to_mesh_map.size()) + "    (active/cached/total)\n";

	msg += "gl meshes CPU mem:      " + getMBSizeString(active_gl_mesh_usage.geom_cpu_usage) + " / " + getMBSizeString(mesh_usage_unused_CPU) + " / " + getMBSizeString(this->mesh_CPU_mem_usage) + "    (active/cached/total)\n";

	msg += "gl meshes GPU mem:      " + getMBSizeString(active_gl_mesh_usage.geom_gpu_usage) +  + " / " + getMBSizeString(mesh_usage_unused_GPU) + " / " + getMBSizeString(this->mesh_GPU_mem_usage) + "    (active/cached/total)\n";

	msg += "physics shapes:         " + toString(physics_shape_map.numUsedItems()) + " / " + toString(physics_shape_map.numUnusedItems()) + " / " + toString(physics_shape_map.size()) + "    (active/cached/total)\n";

	msg += "physics shapes CPU mem: " + getMBSizeString(active_shape_mem) + " / " + getMBSizeString(shape_usage_unused) + " / " + getMBSizeString(this->shape_mem_usage) + "    (active/cached/total)\n";
	msg += "-----------------\n";

	//conPrint("MeshManager::getDiagnostics took " + timer.elapsedStringNSigFigs(4));

	return msg;
}
