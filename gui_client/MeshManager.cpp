/*=====================================================================
MeshManager.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshManager.h"


#include <utils/PlatformUtils.h>


void MeshData::meshDataBecameUnused() const
{
	if(mesh_manager)
		mesh_manager->meshDataBecameUnused(this);
}


MeshManager::MeshManager()
{
	main_thread_id = PlatformUtils::getCurrentThreadID();
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
		it->second->mesh_manager = NULL;

	model_URL_to_mesh_map.clear();
}


Reference<MeshData> MeshManager::insertMeshes(const std::string& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata, Reference<RayMesh>& raymesh)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
	//Lock lock(mutex);

	// conPrint("Inserting mesh '" + model_url + "' into mesh manager.");
	//assert((model_url == "Quad_obj_17249492137259942610.bmesh") || getMeshData(model_url).isNull());

	// Simpler code than below:
	auto res = model_URL_to_mesh_map.find(model_url);
	if(res == model_URL_to_mesh_map.end())
	{
		Reference<MeshData> mesh_data = new MeshData();
		mesh_data->gl_meshdata = gl_meshdata;
		mesh_data->raymesh = raymesh;
		mesh_data->mesh_manager = this;
		mesh_data->model_url = model_url;

		model_URL_to_mesh_map.insert(std::make_pair(model_url, mesh_data));
		return mesh_data;
	}
	else
	{
		return res->second;
	}


	// Reference<MeshData> mesh_data = new MeshData();
	// mesh_data->gl_meshdata = gl_meshdata;
	// mesh_data->raymesh = raymesh;
	// mesh_data->mesh_manager = this;
	// mesh_data->model_url = model_url;
	// 
	// const auto res = model_URL_to_mesh_map.insert(std::make_pair(model_url, mesh_data));
	// 
	// return res.first->second; // Return existing mesh_data, or new mesh_data if it was inserted.
}


Reference<MeshData> MeshManager::getMeshData(const std::string& model_url)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
	//Lock lock(mutex);

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res != model_URL_to_mesh_map.end())
		return res->second;
	else
		return NULL;
}


void MeshManager::meshDataBecameUnused(const MeshData* meshdata)
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
	//Lock lock(mutex);

	// conPrint("meshDataBecameUnused(): Removing mesh '" + meshdata->model_url + "' from mesh manager.");

	//assert(model_URL_to_mesh_map.count(meshdata->model_url) > 0);

	model_URL_to_mesh_map.erase(meshdata->model_url);
}


GLMemUsage MeshManager::getTotalMemUsage() const
{
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
	//Lock lock(mutex);

	GLMemUsage sum;
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
	{
		sum.geom_cpu_usage += it->second->raymesh->getTotalMemUsage();

		sum += it->second->gl_meshdata->getTotalMemUsage();
	}
	return sum;
}
