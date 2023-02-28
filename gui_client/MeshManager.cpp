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


MeshManager::MeshManager()
{
	main_thread_id = PlatformUtils::getCurrentThreadID();
	mesh_CPU_mem_usage = 0;
	mesh_GPU_mem_usage = 0;
	shape_mem_usage = 0;
}


MeshManager::~MeshManager()
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
	clear();
}


void MeshManager::clear()
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

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


Reference<MeshData> MeshManager::insertMesh(const std::string& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

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
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

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


Reference<MeshData> MeshManager::getMeshData(const std::string& model_url)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res != model_URL_to_mesh_map.end())
		return res->second.value;
	else
		return NULL;
}


Reference<PhysicsShapeData> MeshManager::getPhysicsShapeData(const MeshManagerPhysicsShapeKey& key)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

	auto res = physics_shape_map.find(key);
	if(res != physics_shape_map.end())
		return res->second.value;
	else
		return NULL;
}


void MeshManager::meshDataBecameUsed(const MeshData* meshdata)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

	//conPrint("meshDataBecameUsed(): '" + meshdata->model_url + "'");

	model_URL_to_mesh_map.itemBecameUsed(meshdata->model_url);
}


void MeshManager::meshDataBecameUnused(const MeshData* meshdata)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

	//conPrint("meshDataBecameUnused():'" + meshdata->model_url + "'");

	model_URL_to_mesh_map.itemBecameUnused(meshdata->model_url);
}


void MeshManager::physicsShapeDataBecameUsed(const PhysicsShapeData* shape_data)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

	//conPrint("physicsShapeDataBecameUsed(): '" + shape_data->model_url + "'");

	physics_shape_map.itemBecameUsed(MeshManagerPhysicsShapeKey(shape_data->model_url, shape_data->dynamic));
}


void MeshManager::physicsShapeDataBecameUnused(const PhysicsShapeData* shape_data)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);

	//conPrint("physicsShapeDataBecameUnused(): '" + shape_data->model_url + "'");

	physics_shape_map.itemBecameUnused(MeshManagerPhysicsShapeKey(shape_data->model_url, shape_data->dynamic));
}


void MeshManager::trimMeshMemoryUsage()
{
	const size_t max_mesh_CPU_mem_usage = 1024ull * 1024ull * 1024ull;
	const size_t max_mesh_GPU_mem_usage = 1024ull * 1024ull * 1024ull;

	const size_t max_shape_mem_usage    = 1024ull * 1024ull * 1024ull;

	// Remove textures from unused texture list until we are using <= max_tex_mem_usage
	while(((mesh_CPU_mem_usage > max_mesh_CPU_mem_usage) || (mesh_GPU_mem_usage > max_mesh_GPU_mem_usage)) && (model_URL_to_mesh_map.numUnusedItems() > 0))
	{
		std::string removed_key;
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

	// Get total size of unused gl meshes.
	GLMemUsage unused_gl_mesh_usage;
	for(auto it = model_URL_to_mesh_map.unused_items.begin(); it != model_URL_to_mesh_map.unused_items.end(); ++it)
	{
		auto res = model_URL_to_mesh_map.items.find(*it); // Look up actual item for key
		assert(res != model_URL_to_mesh_map.end());
		if(res != model_URL_to_mesh_map.end())
			unused_gl_mesh_usage += res->second.value->gl_meshdata->getTotalMemUsage();
	}

	// Get total size of unused physics shapes
	size_t unused_shape_mem = 0;
	for(auto it = physics_shape_map.unused_items.begin(); it != physics_shape_map.unused_items.end(); ++it)
	{
		auto res = physics_shape_map.items.find(*it); // Look up actual item for key
		assert(res != physics_shape_map.end());
		if(res != physics_shape_map.end())
			unused_shape_mem += res->second.value->physics_shape.size_B;
	}


	// CPU mem used by meshes is generally zero, so don't bother reporting it.  Just report GPU mem usage.
	const size_t mesh_usage_used_GPU = this->mesh_GPU_mem_usage - unused_gl_mesh_usage.totalGPUUsage(); // GPU Mem usage for active/used textures

	const size_t shape_usage_used = this->shape_mem_usage - unused_shape_mem; // GPU Mem usage for active/used textures

	std::string msg;
	msg += "mesh_manager gl meshes:                 " + toString(model_URL_to_mesh_map.size()) + "\n";
	msg += "mesh_manager gl meshes active:          " + toString(model_URL_to_mesh_map.numUsedItems()) + "\n";
	msg += "mesh_manager gl meshes cached:          " + toString(model_URL_to_mesh_map.numUnusedItems()) + "\n";

	msg += "mesh_manager gl meshes total GPU usage: " + getNiceByteSize(this->mesh_GPU_mem_usage) + "\n";
	msg += "mesh_manager gl meshes GPU active:      " + getNiceByteSize(mesh_usage_used_GPU) + "\n";
	msg += "mesh_manager gl meshes GPU cached:      " + getNiceByteSize(unused_gl_mesh_usage.totalGPUUsage()) + "\n";

	msg += "mesh_manager physics shapes:            " + toString(physics_shape_map.size()) + "\n";
	msg += "mesh_manager physics active:            " + toString(physics_shape_map.numUsedItems()) + "\n";
	msg += "mesh_manager physics cached:            " + toString(physics_shape_map.numUnusedItems()) + "\n";

	msg += "mesh_manager physics total CPU usage:   " + getNiceByteSize(this->shape_mem_usage) + "\n";
	msg += "mesh_manager physics CPU active:        " + getNiceByteSize(shape_usage_used) + "\n";
	msg += "mesh_manager physics CPU cached:        " + getNiceByteSize(unused_shape_mem) + "\n";

	//conPrint("MeshManager::getDiagnostics took " + timer.elapsedStringNSigFigs(4));

	return msg;
}
