/*=====================================================================
ProximityLoader.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ProximityLoader.h"


#include "OpenGLEngine.h"
#include <HashMapInsertOnly2.h>


static const float CELL_WIDTH = 200.f; // NOTE: has to be the same value as in WorkerThread.cpp


ProximityLoader::ProximityLoader(float load_distance_)
:	load_distance(load_distance_),
	load_distance2(load_distance_ * load_distance_),
	ob_grid(
		CELL_WIDTH, // grid cell width
		1 << 10 // expected_num_items = num buckets
	),
	last_cam_pos(0,0,0,1)
{
	// Number of cells to iterate over is approx (2*load_distance / cell_w)^3
	// If cell_w = load_distance / 2,
	// num = (2*load_distance / (load_distance / 2))^3 = 4^3 = 64
}


ProximityLoader::~ProximityLoader()
{}


void ProximityLoader::checkAddObject(WorldObjectRef ob)
{
	//conPrint("ProximityLoader:checkAddObject(): Adding ob " + ob->uid.toString() + " at " + ob->pos.toString());

	ob_grid.insert(ob);

	const float ob_load_dist2 = myMin(ob->max_load_dist2, load_distance2);

	const float dist2 = ob->pos.toVec4fPoint().getDist2(last_cam_pos);
	if(dist2 <= ob_load_dist2) // If object is inside of loading distance:
	{
		callbacks->loadObject(ob);
		ob->loaded = true;
	}
}


void ProximityLoader::removeObject(WorldObjectRef ob)
{
	//conPrint("ProximityLoader:removeObject(): Removing ob " + ob->uid.toString());

	callbacks->unloadObject(ob);

	ob_grid.remove(ob);
}


void ProximityLoader::clearAllObjects()
{
	ob_grid.clear();
}


void ProximityLoader::objectTransformChanged(WorldObject* ob)
{
	// See if the object has changed grid cells
	const Vec4i old_cell = ob_grid.bucketIndicesForPoint(ob->last_pos.toVec4fPoint());
	const Vec4i new_cell = ob_grid.bucketIndicesForPoint(ob->pos.toVec4fPoint());

	if(old_cell != new_cell)
	{
		conPrint("Cell changed!");

		// Remove from old grid cell
		ob_grid.removeAtLastPos(ob);

		// Add to new grid cell
		ob_grid.insert(ob);
	}

	// Check for moving in/out of load distance.
	const float ob_load_dist2 = myMin(ob->max_load_dist2, load_distance2);
	const bool old_in_load_dist = ob->last_pos.toVec4fPoint().getDist2(last_cam_pos) <= ob_load_dist2;
	const bool new_in_load_dist = ob->pos     .toVec4fPoint().getDist2(last_cam_pos) <= ob_load_dist2;
	if(old_in_load_dist && !new_in_load_dist) // If object has moved out of load distance:
	{
		callbacks->unloadObject(ob);
		ob->loaded = false;
	}
	else if(!old_in_load_dist && new_in_load_dist) // If object has moved into load distance:
	{
		callbacks->loadObject(ob);
		ob->loaded = true;
	}
}


void ProximityLoader::updateCamPos(const Vec4f& new_cam_pos)
{
	if(new_cam_pos.getDist(last_cam_pos) > 1.0f)
	{
		//conPrint("ProximityLoader: walking grid cells, new_cam_pos: " + new_cam_pos.toStringNSigFigs(3));

		// Iterate over grid cells around last_cam_pos
		// Unload any objects outside of load_distance of new_cam_pos
		const Vec4i old_begin = ob_grid.bucketIndicesForPoint(last_cam_pos - Vec4f(load_distance, load_distance, load_distance, 0));
		const Vec4i old_end   = ob_grid.bucketIndicesForPoint(last_cam_pos + Vec4f(load_distance, load_distance, load_distance, 0));
		{
			int num_iters = 0;
			for(int z = old_begin[2]; z <= old_end[2]; ++z)
			for(int y = old_begin[1]; y <= old_end[1]; ++y)
			for(int x = old_begin[0]; x <= old_end[0]; ++x)
			{
				const HashedObGridBucket& bucket = ob_grid.getBucketForIndices(x, y, z);
				for(auto it = bucket.objects.begin(); it != bucket.objects.end(); ++it)
				{
					WorldObject* ob = it->ptr();
					const float new_dist2 = ob->pos.toVec4fPoint().getDist2(new_cam_pos);
					const float ob_load_dist2 = myMin(ob->max_load_dist2, load_distance2);
					if(new_dist2 > ob_load_dist2) // If object is now outside of loading distance
					{
						if(ob->loaded)
						{
							//conPrint("Unloading object " + ob->uid.toString());
							callbacks->unloadObject(ob);
							ob->loaded = false;
						}
					}
				}

				//old_cells.insert(Vec3<int>(x, y, z));

				num_iters++;
			}
			//printVar(num_iters);
		}

		// Iterate over grid cells around new_cam_pos
		// Load any objects inside of load_distance of new_cam_pos
		{
			const Vec4i begin = ob_grid.bucketIndicesForPoint(new_cam_pos - Vec4f(load_distance, load_distance, load_distance, 0));
			const Vec4i end   = ob_grid.bucketIndicesForPoint(new_cam_pos + Vec4f(load_distance, load_distance, load_distance, 0));

			for(int z = begin[2]; z <= end[2]; ++z)
			for(int y = begin[1]; y <= end[1]; ++y)
			for(int x = begin[0]; x <= end[0]; ++x)
			{
				const HashedObGridBucket& bucket = ob_grid.getBucketForIndices(x, y, z);
				for(auto it = bucket.objects.begin(); it != bucket.objects.end(); ++it)
				{
					WorldObject* ob = it->ptr();
					const float new_dist2 = ob->pos.toVec4fPoint().getDist2(new_cam_pos);
					const float ob_load_dist2 = myMin(ob->max_load_dist2, load_distance2);
					if(new_dist2 <= ob_load_dist2) // If object is now inside of loading distance
					{
						if(!ob->loaded)
						{
							//conPrint("Loading object " + ob->uid.toString());
							callbacks->loadObject(ob);
							ob->loaded = true;
						}
					}
				}

				const Vec3<int> cell_coords(x, y, z);
				const bool is_in_old_cells =
					x >= old_begin[0] && y >= old_begin[1] && z >= old_begin[2] &&
					x <= old_end[0]   && y <= old_end[1]   && z <= old_end[2];
				if(!is_in_old_cells)
				{
					//conPrint("Loading cell " + cell_coords.toString());
					callbacks->newCellInProximity(cell_coords);
				}
			}
		}
	
		last_cam_pos = new_cam_pos;
	}
}


// Sets initial camera position, doesn't issue load object callbacks (assumes no objects downloaded yet)
// Returns initial cell coords within load distance.
std::vector<Vec3<int> > ProximityLoader::setCameraPosForNewConnection(const Vec4f& initial_cam_pos)
{
	const Vec4i begin = ob_grid.bucketIndicesForPoint(initial_cam_pos - Vec4f(load_distance, load_distance, load_distance, 0));
	const Vec4i end   = ob_grid.bucketIndicesForPoint(initial_cam_pos + Vec4f(load_distance, load_distance, load_distance, 0));

	std::vector<Vec3<int> > res;

	for(int z = begin[2]; z <= end[2]; ++z)
	for(int y = begin[1]; y <= end[1]; ++y)
	for(int x = begin[0]; x <= end[0]; ++x)
	{
		const Vec3<int> cell_coords(x, y, z);
		res.push_back(cell_coords);
	}

	return res;
}



#if BUILD_TESTS


void ProximityLoader::test()
{
#if 1
	conPrint("ProximityLoader::test()");

	
	conPrint("ProximityLoader::test() done.");
#endif
}


#endif // BUILD_TESTS
