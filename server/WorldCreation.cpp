/*=====================================================================
WorldCreation.cpp
-----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WorldCreation.h"


#include <graphics/Map2D.h>
#include <graphics/PNGDecoder.h>
#include <PCG32.h>
#include <Rect2.h>
#include <Clock.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <FileChecksum.h>
#include <Lock.h>


static const int parcel_coords[10][4][2] ={
	{ { 5, 50 },{ 25, 50 },{ 25, 70 },{ 5, 70 } }, // 0
	{ { 25, 50 },{ 45, 50 },{ 45, 70 },{ 25, 70 } }, // 1
	{ { 45, 50 },{ 45, 50 },{ 65, 70 },{ 45, 70 } }, // 2
	{ { 5, 70 },{ 25, 70 },{ 25, 90 },{ 5, 90 } }, // 3
	{ { 25, 70 },{ 45, 70 },{ 45, 90 },{ 25, 90 } }, // 4
	{ { 45, 70 },{ 65, 70 },{ 65, 90 },{ 45, 90 } }, // 5
	{ { 45, 90 },{ 65, 90 },{ 65, 115 },{ 45, 115 } }, // 6
	{ { 5, 115 },{ 25, 115 },{ 25, 135 },{ 5, 135 } }, // 7
	{ { 25, 115 },{ 45, 115 },{ 45, 135 },{ 25, 135 } }, // 8
	{ { 45, 115 },{ 65, 115 },{ 65, 135 },{ 45, 135 } }, // 9
};

static void makeParcels(Matrix2d M, int& next_id, Reference<ServerWorldState> world_state)
{
	// Add up then right parcels
	for(int i=0; i<10; ++i)
	{
		const ParcelID parcel_id(next_id++);
		ParcelRef test_parcel = new Parcel();
		test_parcel->state = Parcel::State_Alive;
		test_parcel->id = parcel_id;
		test_parcel->owner_id = UserID(0);
		test_parcel->admin_ids.push_back(UserID(0));
		test_parcel->writer_ids.push_back(UserID(0));
		test_parcel->created_time = TimeStamp::currentTime();
		test_parcel->zbounds = Vec2d(-1, 10);

		for(int v=0; v<4; ++v)
			test_parcel->verts[v] = M * Vec2d(parcel_coords[i][v][0], parcel_coords[i][v][1]);

		test_parcel->build();

		world_state->parcels[parcel_id] = test_parcel;
		world_state->addParcelAsDBDirty(test_parcel);
	}
}


static void makeRandomParcel(const Vec2d& region_botleft, const Vec2d& region_topright, PCG32& rng, int& next_id, Reference<ServerWorldState> world_state, Map2DRef road_map,
	float base_w, float rng_width, float base_h, float rng_h)
{
	for(int i=0; i<100; ++i)
	{
		const float max_z = base_h + (rng.unitRandom() * rng.unitRandom() * rng.unitRandom() * rng_h);

		const float w = base_w + rng.unitRandom() * rng.unitRandom() * rng_width;
		const float h = base_w + rng.unitRandom() * rng.unitRandom() * rng_width;
		const Vec2d botleft = Vec2d(
			region_botleft.x + rng.unitRandom() * (region_topright.x - region_botleft.x), 
			region_botleft.y + rng.unitRandom() * (region_topright.y - region_botleft.y));

		const Vec2d topright = botleft + Vec2d(w, h);

		const Rect2d bounds(botleft, topright);

		// Invalid if extends out of region
		if(topright.x > region_topright.x || topright.y > region_topright.y)
			continue;
			

		bool valid_parcel = true;
		// Check against road map
		if(road_map.nonNull())
		{
			const int RES = 8;
			for(int x=0; x<RES; ++x)
			for(int y=0; y<RES; ++y)
			{
				// Point in parcel
				const Vec2d p(
					botleft.x + (topright.x - botleft.x) * ((float)x / (RES-1)),
					botleft.y + (topright.y - botleft.y) * ((float)y / (RES-1))
				);

				const Vec2d impos(
					(p.x - region_botleft.x) / (region_topright.x - region_botleft.x),
					(p.y - region_botleft.y) / (region_topright.y - region_botleft.y));
				const float val = road_map->sampleSingleChannelTiled((float)impos.x, (float)impos.y, 0);
				if(val < 0.5f)
				{
					// We are interesecting a road
					valid_parcel = false;
					break;
				}
			}

			if(!valid_parcel)
				continue;
		}
		 
		// Check against existing parcels.
		for(auto it = world_state->parcels.begin(); it != world_state->parcels.end(); ++it)
		{
			const Parcel* p = it->second.ptr();

			const Rect2d other_bounds(Vec2d(p->aabb_min.x, p->aabb_min.y), Vec2d(p->aabb_max.x, p->aabb_max.y));

			if(bounds.intersectsRect2(other_bounds))
			{
				valid_parcel = false;
				break;
			}
		}

		if(valid_parcel)
		{
			const ParcelID parcel_id(next_id++);
			ParcelRef test_parcel = new Parcel();
			test_parcel->state = Parcel::State_Alive;
			test_parcel->id = parcel_id;
			test_parcel->owner_id = UserID(0);
			test_parcel->admin_ids.push_back(UserID(0));
			test_parcel->writer_ids.push_back(UserID(0));
			test_parcel->created_time = TimeStamp::currentTime();
			test_parcel->zbounds = Vec2d(-1, max_z);

			test_parcel->verts[0] = botleft;
			test_parcel->verts[1] = Vec2d(topright.x, botleft.y);
			test_parcel->verts[2] = topright;
			test_parcel->verts[3] = Vec2d(botleft.x, topright.y);

			test_parcel->build();

			world_state->parcels[parcel_id] = test_parcel;
			world_state->addParcelAsDBDirty(test_parcel);
			return;
		}
	}

	conPrint("Reached max iters without finding parcel position.");
}



static void makeBlock(const Vec2d& botleft, PCG32& rng, int& next_id, Reference<ServerWorldState> world_state, double parcel_w, double parcel_max_z)
{
	// Randomly omit one of the 4 edge blocks
	const int e = (int)(rng.unitRandom() * 3.9999);
	for(int xi=0; xi<3; ++xi)
		for(int yi=0; yi<3; ++yi)
		{
			if(xi == 1 && yi == 1)
			{
				// Leave middle of block empty.
			}
			else if(xi == 1 && yi == 0 && e == 0)
			{
			}
			else if(xi == 2 && yi == 1 && e == 1)
			{
			}
			else if(xi == 1 && yi == 2 && e == 2)
			{
			}
			else if(xi == 0 && yi == 1 && e == 3)
			{
			}
			else
			{
				const ParcelID parcel_id(next_id++);
				ParcelRef test_parcel = new Parcel();
				test_parcel->state = Parcel::State_Alive;
				test_parcel->id = parcel_id;
				test_parcel->owner_id = UserID(0);
				test_parcel->admin_ids.push_back(UserID(0));
				test_parcel->writer_ids.push_back(UserID(0));
				test_parcel->created_time = TimeStamp::currentTime();
				test_parcel->zbounds = Vec2d(-1, parcel_max_z);

				test_parcel->verts[0] = botleft + Vec2d(xi *     parcel_w,     yi * parcel_w);
				test_parcel->verts[1] = botleft + Vec2d((xi+1) * parcel_w,     yi * parcel_w);
				test_parcel->verts[2] = botleft + Vec2d((xi+1) * parcel_w, (yi+1) * parcel_w);
				test_parcel->verts[3] = botleft + Vec2d((xi) *   parcel_w, (yi+1) * parcel_w);
				test_parcel->build();

				if(test_parcel->verts[0].x < -170 && test_parcel->verts[0].y >= 405)
				{
					// Don't create parcel, leave room for zombot parcel
				}
				else
				{
					world_state->parcels[parcel_id] = test_parcel;
					world_state->addParcelAsDBDirty(test_parcel);
				}
			}
		}
}


static void makeRoad(ServerAllWorldsState& world_state, const Vec3d& pos, const Vec3f& scale, float rotation_angle)
{
	WorldObjectRef test_object = new WorldObject();
	test_object->creator_id = UserID(0);
	test_object->state = WorldObject::State_Alive;
	test_object->uid = world_state.getNextObjectUID();//  world_state->UID(road_uid++);
	test_object->pos = pos;
	test_object->angle = rotation_angle;
	test_object->axis = Vec3f(0,0,1);
	test_object->model_url = "Cube_obj_11907297875084081315.bmesh";
	test_object->scale = scale;
	test_object->content = "road";
	test_object->materials.push_back(new WorldMaterial());

	// Set tex matrix based on scale
	test_object->materials[0]->tex_matrix = Matrix2f(scale.x / 10.f, 0, 0, scale.y / 10.f);
	test_object->materials[0]->colour_texture_url = "stone_floor_jpg_6978110256346892991.jpg";

	world_state.getRootWorldState()->objects[test_object->uid] = test_object;
}


// For some past versions, when users added images or video objects, The same image-cube mesh was created but with different filenames, like 'bitdriver_gif_5438347426447337425.bmesh'.
// Go over the objects and change the use of such meshes to just use 'image_cube_5438347426447337425.bmesh', to reduce the number of files.
// We will detect the use of such meshes by loading the mesh and seeing if the content is the same as for 'image_cube_5438347426447337425.bmesh'.
// We will do this by using a checksum and checking the file length.
#if 0
static void updateToUseImageCubeMeshes(ServerAllWorldsState& all_worlds_state)
{
	Timer timer;

	/*const std::string image_cube_mesh_path = "C:\\Users\\nick\\AppData\\Roaming\\Cyberspace\\resources\\image_cube_5438347426447337425.bmesh";
	const uint64 image_cube_mesh_checksum = FileChecksum::fileChecksum(image_cube_mesh_path);
	const uint64 image_cube_mesh_filesize = FileUtils::getFileSize(image_cube_mesh_path);*/
	const uint64 image_cube_mesh_checksum = 5438347426447337425ull; // The result of the code above
	const uint64 image_cube_mesh_filesize = 210; // The result of the code above

	size_t num_updated = 0;
	{
		Lock lock(all_worlds_state.mutex);
		
		for(auto world_it = all_worlds_state.world_states.begin(); world_it != all_worlds_state.world_states.end(); ++world_it)
		{
			Reference<ServerWorldState> world_state = world_it->second;

			for(auto i = world_state->objects.begin(); i != world_state->objects.end(); ++i)
			{
				WorldObject* ob = i->second.ptr();

				if(!ob->model_url.empty() && all_worlds_state.resource_manager->isFileForURLPresent(ob->model_url) && hasExtension(ob->model_url, "bmesh"))
				{
					try
					{
						const std::string local_path = all_worlds_state.resource_manager->pathForURL(ob->model_url);

						const uint64 filesize = FileUtils::getFileSize(local_path); // Check file size first as it just uses file metadata, without loading the whole file.
						if(filesize == image_cube_mesh_filesize)
						{
							const uint64 checksum = FileChecksum::fileChecksum(local_path);
							if(checksum == image_cube_mesh_checksum)
							{
								conPrint("updateToUseImageCubeMeshes(): Updating model_url '" + ob->model_url + "' to 'image_cube_5438347426447337425.bmesh'.");
								ob->model_url = "image_cube_5438347426447337425.bmesh";

								world_state->addWorldObjectAsDBDirty(ob);
								num_updated++;
							}
						}
					}
					catch(glare::Exception& e)
					{
						conPrint("updateToUseImageCubeMeshes(): Error: " + e.what());
					}
				}
			}
		}
	}

	if(num_updated > 0)
		all_worlds_state.markAsChanged();

	conPrint("updateToUseImageCubeMeshes(): Updated " + toString(num_updated) + " objects to use image_cube_5438347426447337425.bmesh.  Elapsed: " + timer.elapsedStringNSigFigs(3));
}
#endif


// Clear materials for hypercard objects, there was a bug with the material editor that was creating materials for them, when they are not needed.
// Remove because they may create spurious dependencies.
void WorldCreation::removeHypercardMaterials(ServerAllWorldsState& all_worlds_state)
{
	Timer timer;

	size_t num_updated = 0;
	{
		Lock lock(all_worlds_state.mutex);

		for(auto world_it = all_worlds_state.world_states.begin(); world_it != all_worlds_state.world_states.end(); ++world_it)
		{
			Reference<ServerWorldState> world_state = world_it->second;

			for(auto i = world_state->objects.begin(); i != world_state->objects.end(); ++i)
			{
				WorldObject* ob = i->second.ptr();

				if((ob->object_type == WorldObject::ObjectType_Hypercard) && !ob->materials.empty())
				{
					ob->materials.clear();
					world_state->addWorldObjectAsDBDirty(ob);
					num_updated++;
				}
			}
		}
	}

	if(num_updated > 0)
		all_worlds_state.markAsChanged();

	conPrint("removeHypercardMaterials(): Updated " + toString(num_updated) + " objects.  Elapsed: " + timer.elapsedStringNSigFigs(3));
}


void WorldCreation::createParcelsAndRoads(Reference<ServerAllWorldsState> world_state)
{
	// Add 'town square' parcels
	if(world_state->getRootWorldState()->parcels.empty())
	{
		conPrint("Adding some parcels!");

		int next_id = 10;
		makeParcels(Matrix2d(1, 0, 0, 1), next_id, world_state->getRootWorldState());
		makeParcels(Matrix2d(-1, 0, 0, 1), next_id, world_state->getRootWorldState()); // Mirror in y axis (x' = -x)
		makeParcels(Matrix2d(0, 1, 1, 0), next_id, world_state->getRootWorldState()); // Mirror in x=y line(x' = y, y' = x)
		makeParcels(Matrix2d(0, 1, -1, 0), next_id, world_state->getRootWorldState()); // Rotate right 90 degrees (x' = y, y' = -x)
		makeParcels(Matrix2d(1, 0, 0, -1), next_id, world_state->getRootWorldState()); // Mirror in x axis (y' = -y)
		makeParcels(Matrix2d(-1, 0, 0, -1), next_id, world_state->getRootWorldState()); // Rotate 180 degrees (x' = -x, y' = -y)
		makeParcels(Matrix2d(0, -1, -1, 0), next_id, world_state->getRootWorldState()); // Mirror in x=-y line (x' = -y, y' = -x)
		makeParcels(Matrix2d(0, -1, 1, 0), next_id, world_state->getRootWorldState()); // Rotate left 90 degrees (x' = -y, y' = x)

		PCG32 rng(1);
		const int D = 4;
		for(int x=-D; x<D; ++x)
			for(int y=-D; y<D; ++y)
			{
				if(x >= -2 && x <= 1 && y >= -2 && y <= 1)// && 
					//!(x == -2 && -y == 2) && !(x == 1 && y == 1) && !(x == -2 && y == 1) && !(x == 1 && y == -2))
				{
					// Special town square blocks
				}
				else
					makeBlock(Vec2d(5 + x*70, 5 + y*70), rng, next_id, world_state->getRootWorldState(), /*parcel_w=*/20, /*parcel_max_z=*/10);
			}
	}

	// TEMP: make all parcels have zmax = 10
	if(false)
	{
		for(auto i = world_state->getRootWorldState()->parcels.begin(); i != world_state->getRootWorldState()->parcels.end(); ++i)
		{
			ParcelRef parcel = i->second;
			parcel->zbounds.y = 10.0f;
		}
	}

	/*
	// Make parcel with id 20 a 'sandbox', world-writeable parcel
	{
		auto res = world_state->getRootWorldState()->parcels.find(ParcelID(20));
		if(res != world_state->getRootWorldState()->parcels.end())
		{
			res->second->all_writeable = true;
			conPrint("Made parcel 20 all-writeable.");
		}
	}*/


	//server.world_state->objects.clear();

	ParcelID max_parcel_id(0);
	for(auto it = world_state->getRootWorldState()->parcels.begin(); it != world_state->getRootWorldState()->parcels.end(); ++it)
	{
		const Parcel* parcel = it->second.ptr();
		max_parcel_id = myMax(max_parcel_id, parcel->id);
	}

	// Add park parcels if not already created.
	if(max_parcel_id.value() == 425)
	{
		for(int i=0; i<4; ++i)
		{
			const ParcelID parcel_id(426 + i);
			ParcelRef parcel = new Parcel();
			parcel->state = Parcel::State_Alive;
			parcel->id = parcel_id;
			parcel->owner_id = UserID(0);
			parcel->admin_ids.push_back(UserID(0));
			parcel->writer_ids.push_back(UserID(0));
			parcel->created_time = TimeStamp::currentTime();
			parcel->zbounds = Vec2d(-2, 20);

			Vec2d centre(-105 + 210 * (i % 2), -105 + 210 * (i / 2));
			parcel->verts[0] = centre - Vec2d(-30, -30);
			parcel->verts[1] = centre - Vec2d(30, -30);
			parcel->verts[2] = centre - Vec2d(30, 30);
			parcel->verts[3] = centre - Vec2d(-30, 30);

			parcel->build();

			world_state->getRootWorldState()->parcels[parcel_id] = parcel;
		}
	}


	// Delete parcels newer than id 429.
	/*for(auto it = world_state->getRootWorldState()->parcels.begin(); it != world_state->getRootWorldState()->parcels.end();)
	{
		if(it->first.value() > 429)
			it = world_state->getRootWorldState()->parcels.erase(it);
		else
			it++;
	}*/


	// Recompute max_parcel_id
	max_parcel_id = ParcelID(0);
	for(auto it = world_state->getRootWorldState()->parcels.begin(); it != world_state->getRootWorldState()->parcels.end(); ++it)
	{
		const Parcel* parcel = it->second.ptr();
		max_parcel_id = myMax(max_parcel_id, parcel->id);
	}


	if(false)//max_parcel_id.value() == 429)
	{
		// Make market and random east district
		const int start_id = (int)max_parcel_id.value() + 1;
		int next_id = start_id;

		// Make market district
		if(false)
		{
			PCG32 rng(1);

#ifdef WIN32
			Map2DRef road_map = PNGDecoder::decode("D:\\art\\substrata\\parcels\\roads.png");
#else
			Map2DRef road_map = PNGDecoder::decode("/home/nick/substrata/roads.png");
#endif

			for(int i=0; i<300; ++i)
				makeRandomParcel(/*region botleft=*/Vec2d(335.f, 75), /*region topright=*/Vec2d(335.f + 130.f, 205.f), rng, next_id, world_state->getRootWorldState(), road_map,
					/*base width=*/3, /*rng width=*/4, /*base_h=*/4, /*rng_h=*/4);

			conPrint("Made market district, parcel ids " + toString(start_id) + " to " + toString(next_id - 1));
		}

		// Make random east district
		{
			const int east_district_start_id = next_id;

			PCG32 rng(1);

			for(int x = 0; x<4; ++x)
				for(int y = 0; y<4; ++y)
				{
					Vec2d offset(x * 70, y * 70);
					for(int i=0; i<100; ++i)
					{
						const Vec2d botleft = Vec2d(335.f, -275) + offset;
						makeRandomParcel(/*region botleft=*/botleft, /*region topright=*/botleft + Vec2d(60, 60), rng, next_id, world_state->getRootWorldState(), NULL/*road_map*/,
							/*base width=*/8, /*rng width=*/40, /*base_h=*/8, /*rng_h=*/20);
					}
				}

			conPrint("Made random east district, parcel ids " + toString(east_district_start_id) + " to " + toString(next_id - 1));
		}
	}

	if(max_parcel_id.value() == 953)
	{
		// Make Zombot's parcels: a 105m^2 plot of land, split vertically down the middle into two plots
		{
			const ParcelID parcel_id(954);
			ParcelRef parcel = new Parcel();
			parcel->state = Parcel::State_Alive;
			parcel->id = parcel_id;
			parcel->owner_id = UserID(0);
			parcel->admin_ids.push_back(UserID(0));
			parcel->writer_ids.push_back(UserID(0));
			parcel->created_time = TimeStamp::currentTime();
			parcel->zbounds = Vec2d(-2, 50);

			parcel->verts[0] = Vec2d(-275, 335 + 80); // 335 = y coord of north edge of north town belt, place 80 m above that
			parcel->verts[1] = Vec2d(-275 + 105.0/2, 335 + 80);
			parcel->verts[2] = Vec2d(-275 + 105.0/2, 335 + 80 + 105);
			parcel->verts[3] = Vec2d(-275, 335 + 80 + 105);

			parcel->build();

			world_state->getRootWorldState()->parcels[parcel_id] = parcel;
			world_state->getRootWorldState()->addParcelAsDBDirty(parcel);
		}
		{
			const ParcelID parcel_id(955);
			ParcelRef parcel = new Parcel();
			parcel->state = Parcel::State_Alive;
			parcel->id = parcel_id;
			parcel->owner_id = UserID(0);
			parcel->admin_ids.push_back(UserID(0));
			parcel->writer_ids.push_back(UserID(0));
			parcel->created_time = TimeStamp::currentTime();
			parcel->zbounds = Vec2d(-2, 50);

			parcel->verts[0] = Vec2d(-275 + 105.0/2, 335 + 80); // 335 = y coord of north edge of north town belt, place 80 m above that
			parcel->verts[1] = Vec2d(-275 + 105.0, 335 + 80);
			parcel->verts[2] = Vec2d(-275 + 105.0, 335 + 80 + 105);
			parcel->verts[3] = Vec2d(-275 + 105.0/2, 335 + 80 + 105);

			parcel->build();

			world_state->getRootWorldState()->parcels[parcel_id] = parcel;
			world_state->getRootWorldState()->addParcelAsDBDirty(parcel);
		}
	}


	if(max_parcel_id.value() == 955)
	{
		conPrint("Adding north district parcels!");

		const int initial_next_id = 956;
		int next_id = initial_next_id;

		const double parcel_width = 14;
		const double block_width = parcel_width * 3 + 8;
		PCG32 rng(1);
		for(int x=0; x<11; ++x)
			for(int y=0; y<4; ++y)
			{
				//if(x <= 2 && y >= 1)
				//{
				//	// leave space for zombot parcel
				//}
				//else 
				if(
					/*(x == 1 && y == 1) ||
					(x == 4 && y == 1) ||
					(x == 2 && y == 2) ||
					(x == 6 && y == 2) ||
					(x == 8 && y == 1) ||
					(x == 10 && y == 2)*/
					(x == 1 && y == 1) ||
					//(x == 2 && y == 2) ||
					(x == 3 && y == 1) ||
					(x == 5 && y == 0) ||
					(x == 5 && y == 2) ||
					(x == 7 && y == 1) ||
					(x == 9 && y == 2)
					)
				{
					// Make empty space for park/square
				}
				else
					makeBlock(/*botleft=*/Vec2d(-275 + x * block_width, 335 + y * block_width), rng, next_id, world_state->getRootWorldState(), /*parcel_w=*/parcel_width,
						/*parcel_max_z=*/15 + rng.unitRandom() * 8);
			}

		world_state->markAsChanged();
		conPrint("Num parcels added: " + toString(next_id - initial_next_id));
	}


	// Add road objects
	if(false)
	{
		bool have_added_roads = false;
		for(auto it = world_state->getRootWorldState()->objects.begin(); it != world_state->getRootWorldState()->objects.end(); ++it)
		{
			const WorldObject* object = it->second.ptr();
			if(object->creator_id.value() == 0 && object->content == "road")
				have_added_roads = true;
		}

		printVar(have_added_roads);


		if(false)
		{
			// Remove all existing road objects (UID > 1000000)
			for(auto it = world_state->getRootWorldState()->objects.begin(); it != world_state->getRootWorldState()->objects.end();)
			{
				if(it->second->uid.value() >= 1000000)
					it = world_state->getRootWorldState()->objects.erase(it);
				else
					++it;
			}
		}

		if(!have_added_roads)
		{
			const UID next_uid = world_state->getNextObjectUID();
			conPrint(next_uid.toString());

			const float z_scale = 0.1;

			// Long roads near centre
			for(int x=-1; x <= 1; ++x)
			{
				if(x != 0)
				{
					makeRoad(*world_state,
						Vec3d(x * 92.5, 0, 0), // pos
						Vec3f(87, 8, z_scale), // scale
						0 // rot angle
					);
				}
			}

			for(int y=-1; y <= 1; ++y)
			{
				if(y != 0)
				{
					makeRoad(*world_state,
						Vec3d(0, y * 92.5, 0), // pos
						Vec3f(8, 87, z_scale), // scale
						0 // rot angle
					);
				}
			}

			// Diagonal roads
			{
				const float diag_z_scale = z_scale / 2; // to avoid z-fighting
				makeRoad(*world_state,
					Vec3d(57.5, 57.5, 0), // pos
					Vec3f(30, 6, diag_z_scale), // scale
					Maths::pi<float>() / 4 // rot angle
				);

				makeRoad(*world_state,
					Vec3d(57.5, -57.5, 0), // pos
					Vec3f(30, 6, diag_z_scale), // scale
					-Maths::pi<float>() / 4 // rot angle
				);

				makeRoad(*world_state,
					Vec3d(-57.5, 57.5, 0), // pos
					Vec3f(30, 6, diag_z_scale), // scale
					Maths::pi<float>() * 3 / 4 // rot angle
				);

				makeRoad(*world_state,
					Vec3d(-57.5, -57.5, 0), // pos
					Vec3f(30, 6, diag_z_scale), // scale
					-Maths::pi<float>() * 3 / 4 // rot angle
				);
			}


			// Roads along x axis:
			for(int x = -4; x <= 3; ++x)
				for(int y = -3; y <= 3; ++y)
				{
					bool near_centre = y >= -1 && y <= 1 && x >= -1 && x <= 0;

					bool long_roads = (x >= -2 && x <= 1) && y == 0;

					if(!near_centre && !long_roads)
					{
						makeRoad(*world_state,
							Vec3d(35 + x * 70, y * 70.0, 0), // pos
							Vec3f(62, 8, z_scale), // scale
							0 // rot angle
						);
					}
				}

			// Roads along y axis:
			for(int y = -4; y <= 3; ++y)
				for(int x = -3; x <= 3; ++x)
				{
					bool near_centre = x >= -1 && x <= 1 && y >= -1 && y <= 0;

					bool long_roads = (y >= -2 && y <= 1) && x == 0;

					if(!near_centre && !long_roads)
					{
						makeRoad(*world_state,
							Vec3d(x * 70.0, 35 + y * 70, 0), // pos
							Vec3f(8, 62, z_scale), // scale
							0 // rot angle
						);
					}
				}

			// Intersections
			for(int y = -3; y <= 3; ++y)
				for(int x = -3; x <= 3; ++x)
				{
					bool near_centre = x >= -1 && x <= 1 && y >= -1 && y <= 1;

					if(!near_centre)
					{
						makeRoad(*world_state,
							Vec3d(x * 70.0, y * 70, 0), // pos
							Vec3f(8, 8, z_scale), // scale
							0 // rot angle
						);
					}
				}

			// Intersections with diagonal roads (outer)
			for(int y = -1; y <= 1; ++y)
				for(int x = -1; x <= 1; ++x)
				{
					if(x != 0 && y != 0)
					{
						makeRoad(*world_state,
							Vec3d(x * 70.0, y * 70, 0), // pos
							Vec3f(8, 8, z_scale), // scale
							0 // rot angle
						);
					}
				}

			// Intersections with diagonal roads (inner)
			for(int y = -1; y <= 1; ++y)
				for(int x = -1; x <= 1; ++x)
				{
					if(x != 0 && y != 0)
					{
						makeRoad(*world_state,
							Vec3d(x * 45, y * 45, 0), // pos
							Vec3f(8, 8, z_scale), // scale
							0 // rot angle
						);
					}
				}

			// Centre roads
			makeRoad(*world_state,
				Vec3d(0, 45, 0), // pos
				Vec3f(82, 8, z_scale), // scale
				0 // rot angle
			);
			makeRoad(*world_state,
				Vec3d(0, -45, 0), // pos
				Vec3f(82, 8, z_scale), // scale
				0 // rot angle
			);
			makeRoad(*world_state,
				Vec3d(45, 0, 0), // pos
				Vec3f(8, 82, z_scale), // scale
				0 // rot angle
			);
			makeRoad(*world_state,
				Vec3d(-45, 0, 0), // pos
				Vec3f(8, 82, z_scale), // scale
				0 // rot angle
			);
		}
	}
}
