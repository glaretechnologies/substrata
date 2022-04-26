/*=====================================================================
MeshManager.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshManager.h"


void MeshData::meshDataBecameUnused() const
{
	if(mesh_manager)
		mesh_manager->meshDataBecameUnused(this);
}


/*bool MeshManager::isMeshDataInserted(const std::string& model_url) const
{
	Lock lock(mutex);

	return model_URL_to_mesh_map.count(model_url) > 0;
}


bool MeshManager::isMeshDataInsertedNoLock(const std::string& model_url) const
{
	return model_URL_to_mesh_map.count(model_url) > 0;
}*/


MeshManager::MeshManager()
{}


MeshManager::~MeshManager()
{
	// Before we clear model_URL_to_mesh_map, NULL out references to mesh_manager so meshDataBecameUnused() doesn't trigger.
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
		it->second->mesh_manager = NULL;
}


Reference<MeshData> MeshManager::insertMeshes(const std::string& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata, Reference<RayMesh>& raymesh)
{
	Lock lock(mutex);

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
	Lock lock(mutex);

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res != model_URL_to_mesh_map.end())
		return res->second;
	else
		return NULL;
}


void MeshManager::meshDataBecameUnused(const MeshData* meshdata)
{
	Lock lock(mutex);

	// conPrint("meshDataBecameUnused(): Removing mesh '" + meshdata->model_url + "' from mesh manager.");

	//assert(model_URL_to_mesh_map.count(meshdata->model_url) > 0);

	model_URL_to_mesh_map.erase(meshdata->model_url);
}


GLMemUsage MeshManager::getTotalMemUsage() const
{
	Lock lock(mutex);

	GLMemUsage sum;
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
	{
		sum.geom_cpu_usage += it->second->raymesh->getTotalMemUsage();

		sum += it->second->gl_meshdata->getTotalMemUsage();
	}
	return sum;
}
