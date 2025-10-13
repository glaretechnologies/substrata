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

static void makeParcels(Matrix2d M, int& next_id, Reference<ServerWorldState> world_state, WorldStateLock& lock)
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

		world_state->getParcels(lock)[parcel_id] = test_parcel;
		world_state->addParcelAsDBDirty(test_parcel, lock);
	}
}


static void makeRandomParcel(const Vec2d& region_botleft, const Vec2d& region_topright, PCG32& rng, int& next_id, Reference<ServerWorldState> world_state, Map2DRef road_map,
	float base_w, float rng_width, float base_h, float rng_h, WorldStateLock& lock)
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
		//Lock lock(world_state->mutex);
		for(auto it = world_state->getParcels(lock).begin(); it != world_state->getParcels(lock).end(); ++it)
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

			world_state->getParcels(lock)[parcel_id] = test_parcel;
			world_state->addParcelAsDBDirty(test_parcel, lock);
			return;
		}
	}

	conPrint("Reached max iters without finding parcel position.");
}



static void makeBlock(const Vec2d& botleft, PCG32& rng, int& next_id, Reference<ServerWorldState> world_state, double parcel_w, double parcel_max_z, WorldStateLock& lock)
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
					//Lock lock(world_state->mutex);
					world_state->getParcels(lock)[parcel_id] = test_parcel;
					world_state->addParcelAsDBDirty(test_parcel, lock);
				}
			}
		}
}


#if 0
static void makeTowerParcels(const Vec2d& botleft, int& next_id, Reference<ServerWorldState> world_state, double parcel_w, double story_height, int num_stories)
{
	for(int i=0; i<num_stories; ++i)
	{
		const ParcelID parcel_id(next_id++);
		ParcelRef test_parcel = new Parcel();
		test_parcel->state = Parcel::State_Alive;
		test_parcel->id = parcel_id;
		test_parcel->owner_id = UserID(0);
		test_parcel->admin_ids.push_back(UserID(0));
		test_parcel->writer_ids.push_back(UserID(0));
		test_parcel->created_time = TimeStamp::currentTime();

		test_parcel->zbounds = Vec2d(i * story_height - 0.5, (i + 1) * story_height - 0.5);

		const int xi = 0;
		const int yi = 0;
		test_parcel->verts[0] = botleft + Vec2d(xi *     parcel_w,     yi * parcel_w);
		test_parcel->verts[1] = botleft + Vec2d((xi+1) * parcel_w,     yi * parcel_w);
		test_parcel->verts[2] = botleft + Vec2d((xi+1) * parcel_w, (yi+1) * parcel_w);
		test_parcel->verts[3] = botleft + Vec2d((xi) *   parcel_w, (yi+1) * parcel_w);
		test_parcel->build();

		world_state->parcels[parcel_id] = test_parcel;
		world_state->addParcelAsDBDirty(test_parcel);
	}
}
#endif


static WorldObjectRef findObWithModelURL(Reference<ServerAllWorldsState> world_state, const URLString& URL)
{
	WorldStateLock lock(world_state->mutex);

	WorldObjectRef ob;
	//Lock lock(world_state->getRootWorldState()->mutex);
	for(auto it = world_state->getRootWorldState()->getObjects(lock).begin(); it != world_state->getRootWorldState()->getObjects(lock).end(); ++it)
	{
		if(it->second->model_url == URL)
			ob = it->second;
	}
	
	if(ob.isNull())
		throw glare::Exception("Could not find an object using model URL '" + toStdString(URL) + "'.");
	else
		return ob;
}


static void makeTowerObjects(const Vec2d& botleft, int& next_id, Reference<ServerAllWorldsState> world_state, PCG32& rng, double parcel_w, double story_height, int num_stories)
{
	WorldStateLock lock(world_state->mutex);

	// Find an object using room model to copy from
	WorldObjectRef room1_ob = findObWithModelURL(world_state, "room1_show_noBeam_glb_5590447676997932357.bmesh");
	WorldObjectRef room2_ob = findObWithModelURL(world_state, "room2_WindowsFLAT_glb_13600392068904710101.bmesh");
	WorldObjectRef room3_ob = findObWithModelURL(world_state, "room3_WindowsFLAT_2_glb_12220979663580597788.bmesh");

	WorldObjectRef platform1_ob = findObWithModelURL(world_state, "PlatformFinal_glb_6209101633263662392.bmesh");
	WorldObjectRef platform2_ob = findObWithModelURL(world_state, "PlatformFinalLEFT_glb_2715111425173727191.bmesh");

	WorldObjectRef couch1_ob = findObWithModelURL(world_state, "VoxCouch__Final_grey_glb_3629570434401678297.bmesh");
	WorldObjectRef couch2_ob = findObWithModelURL(world_state, "Couch_Holz_Grau_glb_10451238764035445915.bmesh");

	WorldObjectRef seat1_ob = findObWithModelURL(world_state, "grey_Gustav_glb_14897620384736448070.bmesh");
	WorldObjectRef seat2_ob = findObWithModelURL(world_state, "VoxSeat__Final_grey_glb_6666984473320402552.bmesh");

	WorldObjectRef table1_ob = findObWithModelURL(world_state, "60_tisch_glb_11656912686065707806.bmesh");
	WorldObjectRef table2_ob = findObWithModelURL(world_state, "couch_table_Geschwungen_glb_3686631209284750062.bmesh");
	WorldObjectRef table3_ob = findObWithModelURL(world_state, "CouchTisch_Holz_Grau_glb_6583257170939054006.bmesh");

	WorldObjectRef carpet1_ob = findObWithModelURL(world_state, "Carpet_Yellow_2_glb_10610072849682925808.bmesh");
	WorldObjectRef carpet2_ob = findObWithModelURL(world_state, "Carpet_grey_2_glb_16725751618769902725.bmesh");

	WorldObjectRef lamp_ob = findObWithModelURL(world_state, "Lampe_Kaiser_glb_6366364697719938472.bmesh");

	std::vector<WorldObjectRef> room_obs({room1_ob, room2_ob, room3_ob});
	std::vector<WorldObjectRef> platform_obs({platform1_ob, platform2_ob});
	std::vector<WorldObjectRef> couch_obs({couch1_ob, couch2_ob});
	std::vector<WorldObjectRef> seat_obs({seat1_ob, seat2_ob});
	std::vector<WorldObjectRef> table_obs({table1_ob, table2_ob, table3_ob});
	std::vector<WorldObjectRef> carpet_obs({carpet1_ob, carpet2_ob});
	std::vector<WorldObjectRef> lamp_obs({lamp_ob});

	for(int i=0; i<num_stories; ++i)
	{
		const double floor_z = i * story_height;

		{
			WorldObjectRef source_ob = room_obs[/*i % room_obs.size()*/rng.nextUInt((uint32)room_obs.size())];

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(botleft.x + parcel_w/2, botleft.y + parcel_w/2, floor_z);
			new_object->angle = Maths::pi_2<float>();
			new_object->axis = Vec3f(1,0,0);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower prefab";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();

			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
		}



		// Make platform object
		if((i % 4 == 0) && (i >= 3))
		{
			const int platform_i = (i / 4) % 2;
			WorldObjectRef source_ob = platform_obs[platform_i];

			// second platform model needs a pi rotation around z axis.
			const Quatf rot = (platform_i == 0 ? Quatf::identity() : Quatf::fromAxisAndAngle(Vec3f(0,0,1), Maths::pi<float>())) * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			Vec4f axis;
			float angle;
			rot.toAxisAndAngle(axis, angle);

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(botleft.x + parcel_w/2, botleft.y + parcel_w/2, floor_z);
			new_object->angle = angle;
			new_object->axis = Vec3f(axis);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower platform";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();
			
			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
		}

		// Make couches etc..

		// Where coffee table will go, where couches are arranged around
		Vec3d lounge_focus_pos = Vec3d(
			botleft.x + parcel_w - (4.5 + rng.unitRandom() * 3), 
			botleft.y + 3.7 + rng.unitRandom() * 6, 
			floor_z
		);

		Quatf z_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), rng.unitRandom() < 0.5 ? 0 : -Maths::pi_2<float>());


		// Add table
		{
			const int table_i = rng.nextUInt((uint32)table_obs.size());
			WorldObjectRef source_ob = table_obs[table_i];

			//Quatf z_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), rng.unitRandom() < 0.5 ? 0 : -Maths::pi_2<float>());

			Vec3d table_pos = lounge_focus_pos;

			const Quatf rot = z_rot * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			Vec4f axis;
			float angle;
			rot.toAxisAndAngle(axis, angle);

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(table_pos.x, table_pos.y, floor_z);
			new_object->angle = angle;
			new_object->axis = Vec3f(axis);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower furniture";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();

			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
		}

		// Add carpet
		//if(rng.unitRandom() < 0.6f)
		{
			const int carpet_i = rng.nextUInt((uint32)carpet_obs.size());
			WorldObjectRef source_ob = carpet_obs[carpet_i];

			Vec3d carpet_pos = lounge_focus_pos;

			const Quatf rot = z_rot * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			Vec4f axis;
			float angle;
			rot.toAxisAndAngle(axis, angle);

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(carpet_pos.x, carpet_pos.y, floor_z);
			new_object->angle = angle;
			new_object->axis = Vec3f(axis);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower furniture";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();

			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
		}

		// Add couch 1
		{
			const int couch_i = rng.nextUInt((uint32)couch_obs.size());
			WorldObjectRef source_ob = couch_obs[couch_i];

			Vec4f couch_forwards_ws = z_rot.rotateVector(Vec4f(0,-1,0,0));

			Vec3d couch_pos = lounge_focus_pos - Vec3d(couch_forwards_ws) * 2.0;

			const Quatf rot = z_rot * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			Vec4f axis;
			float angle;
			rot.toAxisAndAngle(axis, angle);

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(couch_pos.x, couch_pos.y, floor_z);
			new_object->angle = angle;
			new_object->axis = Vec3f(axis);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower furniture";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();

			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
		}

		// Add seat
		{
			const int seat_i = rng.nextUInt((uint32)seat_obs.size());
			WorldObjectRef source_ob = seat_obs[seat_i];

			Quatf extra_z_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), -Maths::pi_2<float>());

			Vec4f couch_forwards_ws = (extra_z_rot * z_rot).rotateVector(Vec4f(0,-1,0,0));

			Vec3d couch_pos = lounge_focus_pos - Vec3d(couch_forwards_ws) * 3.0;

			const Quatf rot = extra_z_rot * z_rot * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			Vec4f axis;
			float angle;
			rot.toAxisAndAngle(axis, angle);

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(couch_pos.x, couch_pos.y, floor_z);
			new_object->angle = angle;
			new_object->axis = Vec3f(axis);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower furniture";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();

			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
		}

		// Add lamp
		{
			const int lamp_i = rng.nextUInt((uint32)lamp_obs.size());
			WorldObjectRef source_ob = lamp_obs[lamp_i];

			Vec4f lounge_forwards = z_rot.rotateVector(Vec4f(0,-1,0,0));
			Vec4f lounge_left     = z_rot.rotateVector(Vec4f(1,0,0,0));

			Quatf extra_z_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), -1.f);

			Vec3d lamp_pos = lounge_focus_pos - Vec3d(lounge_forwards) * 3.0 + Vec3d(lounge_left) * 4.0;

			const Quatf rot = extra_z_rot * z_rot * Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			Vec4f axis;
			float angle;
			rot.toAxisAndAngle(axis, angle);

			WorldObjectRef new_object = new WorldObject();
			new_object->creator_id = UserID(0);
			new_object->created_time = TimeStamp::currentTime();
			new_object->last_modified_time = TimeStamp::currentTime();
			new_object->state = WorldObject::State_Alive;
			new_object->uid = world_state->getNextObjectUID();
			new_object->pos = Vec3d(lamp_pos.x, lamp_pos.y, floor_z);
			new_object->angle = angle;
			new_object->axis = Vec3f(axis);
			new_object->model_url = source_ob->model_url;
			new_object->scale = Vec3f(1.0);
			new_object->setAABBOS(source_ob->getAABBOS());
			new_object->content = "tower furniture";
			new_object->max_model_lod_level = 2;

			new_object->materials.resize(source_ob->materials.size());
			for(size_t z=0; z<new_object->materials.size(); ++z)
				new_object->materials[z] = source_ob->materials[z]->clone();

			world_state->getRootWorldState()->getObjects(lock)[new_object->uid] = new_object; // Insert into world
			world_state->getRootWorldState()->addWorldObjectAsDBDirty(new_object, lock);
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

	WorldStateLock lock(world_state.mutex);

	world_state.getRootWorldState()->getObjects(lock)[test_object->uid] = test_object;
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
		WorldStateLock lock(all_worlds_state.mutex);

		for(auto world_it = all_worlds_state.world_states.begin(); world_it != all_worlds_state.world_states.end(); ++world_it)
		{
			Reference<ServerWorldState> world_state = world_it->second;

			for(auto i = world_state->getObjects(lock).begin(); i != world_state->getObjects(lock).end(); ++i)
			{
				WorldObject* ob = i->second.ptr();

				if((ob->object_type == WorldObject::ObjectType_Hypercard) && !ob->materials.empty())
				{
					ob->materials.clear();
					world_state->addWorldObjectAsDBDirty(ob, lock);
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
	WorldStateLock lock(world_state->mutex);

	// Add 'town square' parcels
	if(world_state->getRootWorldState()->getParcels(lock).empty())
	{
		conPrint("Adding some parcels!");

		int next_id = 10;
		makeParcels(Matrix2d(1, 0, 0, 1), next_id, world_state->getRootWorldState(), lock);
		makeParcels(Matrix2d(-1, 0, 0, 1), next_id, world_state->getRootWorldState(), lock); // Mirror in y axis (x' = -x)
		makeParcels(Matrix2d(0, 1, 1, 0), next_id, world_state->getRootWorldState(), lock); // Mirror in x=y line(x' = y, y' = x)
		makeParcels(Matrix2d(0, 1, -1, 0), next_id, world_state->getRootWorldState(), lock); // Rotate right 90 degrees (x' = y, y' = -x)
		makeParcels(Matrix2d(1, 0, 0, -1), next_id, world_state->getRootWorldState(), lock); // Mirror in x axis (y' = -y)
		makeParcels(Matrix2d(-1, 0, 0, -1), next_id, world_state->getRootWorldState(), lock); // Rotate 180 degrees (x' = -x, y' = -y)
		makeParcels(Matrix2d(0, -1, -1, 0), next_id, world_state->getRootWorldState(), lock); // Mirror in x=-y line (x' = -y, y' = -x)
		makeParcels(Matrix2d(0, -1, 1, 0), next_id, world_state->getRootWorldState(), lock); // Rotate left 90 degrees (x' = -y, y' = x)

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
					makeBlock(Vec2d(5 + x*70, 5 + y*70), rng, next_id, world_state->getRootWorldState(), /*parcel_w=*/20, /*parcel_max_z=*/10, lock);
			}
	}

	// TEMP: make all parcels have zmax = 10
	if(false)
	{
		for(auto i = world_state->getRootWorldState()->getParcels(lock).begin(); i != world_state->getRootWorldState()->getParcels(lock).end(); ++i)
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
	for(auto it = world_state->getRootWorldState()->getParcels(lock).begin(); it != world_state->getRootWorldState()->getParcels(lock).end(); ++it)
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

			world_state->getRootWorldState()->getParcels(lock)[parcel_id] = parcel;
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
	for(auto it = world_state->getRootWorldState()->getParcels(lock).begin(); it != world_state->getRootWorldState()->getParcels(lock).end(); ++it)
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
					/*base width=*/3, /*rng width=*/4, /*base_h=*/4, /*rng_h=*/4, lock);

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
							/*base width=*/8, /*rng width=*/40, /*base_h=*/8, /*rng_h=*/20, lock);
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

			world_state->getRootWorldState()->getParcels(lock)[parcel_id] = parcel;
			world_state->getRootWorldState()->addParcelAsDBDirty(parcel, lock);
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

			world_state->getRootWorldState()->getParcels(lock)[parcel_id] = parcel;
			world_state->getRootWorldState()->addParcelAsDBDirty(parcel, lock);
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
						/*parcel_max_z=*/15 + rng.unitRandom() * 8, lock);
			}

		world_state->markAsChanged();
		conPrint("Num parcels added: " + toString(next_id - initial_next_id));
	}

	
	// TEMP: Delete parcels newer than id 1221.
	/*for(auto it = world_state->getRootWorldState()->parcels.begin(); it != world_state->getRootWorldState()->parcels.end();)
	{
		if(it->first.value() > 1221)
			it = world_state->getRootWorldState()->parcels.erase(it);
		else
			it++;
	}

	// TEMP: Recompute max_parcel_id
	max_parcel_id = ParcelID(0);
	for(auto it = world_state->getRootWorldState()->parcels.begin(); it != world_state->getRootWorldState()->parcels.end(); ++it)
	{
		const Parcel* parcel = it->second.ptr();
		max_parcel_id = myMax(max_parcel_id, parcel->id);
	}

	// TEMP: remove any objects with 'tower' content
	for(auto it = world_state->getRootWorldState()->objects.begin(); it != world_state->getRootWorldState()->objects.end(); ++it)
	{
		if(it->second->content == "tower" || it->second->content == "tower prefab" || it->second->content == "tower platform" || it->second->content == "tower furniture")
			it = world_state->getRootWorldState()->objects.erase(it);
		else
			it++;
	}

	// TEMP: remove any objects with 'tower' content
	for(auto it = world_state->getRootWorldState()->objects.begin(); it != world_state->getRootWorldState()->objects.end(); ++it)
	{
		WorldObject* ob = it->second.ptr();
		if(ob->content == "tower" || ob->content == "tower prefab" || ob->content == "tower platform" || ob->content == "tower furniture")
		{
			ob->state = WorldObject::State_Dead;
			ob->from_remote_other_dirty = true;
			world_state->getRootWorldState()->dirty_from_remote_objects.insert(ob);
		}
	}*/
	


	//if(max_parcel_id.value() == 1221)
	if(false)
	{
		try
		{
			//conPrint("Adding north-east district parcels!");
			conPrint("Rebuilding tower furniture!");

			const int initial_next_id = 1222;
			int next_id = initial_next_id;

			const double parcel_width = 14;
			const double block_width = parcel_width * 3 + 8;
			PCG32 rng(1);
			//for(int x=0; x<5; ++x)
			//	for(int y=0; y<4; ++y)
			//	{
			//		if(
			//			(x == 1 && y == 2) || // tower block
			//			(x == 1 && y == 1) ||
			//			(x == 2 && y == 0) || // tower block
			//			(x == 3 && y == 2) || // tower block
			//			(x == 3 && y == 1) ||
			//			(x == 3 && y == 0) ||
			//			(x == 5 && y == 0) ||
			//			(x == 5 && y == 2) ||
			//			(x == 7 && y == 1) ||
			//			(x == 9 && y == 2)
			//			)
			//		{
			//			// Make empty space for park/square or tower
			//		}
			//		else
			//			makeBlock(/*botleft=*/Vec2d(335 + x * block_width, 335 + y * block_width), rng, next_id, world_state->getRootWorldState(), /*parcel_w=*/parcel_width,
			//				/*parcel_max_z=*/18 /*+ rng.unitRandom() * 8*/);
			//	}

			const double padding = 0.3;
			const double tower_parcel_w = 14 + padding * 2; // Large enough to hold the story 3d models
			//makeTowerParcels(/*botleft=*/Vec2d(335 + 3 * block_width + parcel_width - padding, 335 + 2 * block_width + parcel_width - padding), next_id, world_state->getRootWorldState(), /*parcel_w=*/tower_parcel_w, /*story height=*/9.0, /*num stories=*/24);
			makeTowerObjects(/*botleft=*/Vec2d(335 + 3 * block_width + parcel_width - padding, 335 + 2 * block_width + parcel_width - padding), next_id, world_state, rng, /*parcel_w=*/tower_parcel_w, /*story height=*/9.0, /*num stories=*/24);

			//makeTowerParcels(/*botleft=*/Vec2d(335 + 1 * block_width + parcel_width - padding, 335 + 2 * block_width + parcel_width - padding), next_id, world_state->getRootWorldState(), /*parcel_w=*/tower_parcel_w, /*story height=*/9.0, /*num stories=*/22);
			makeTowerObjects(/*botleft=*/Vec2d(335 + 1 * block_width + parcel_width - padding, 335 + 2 * block_width + parcel_width - padding), next_id, world_state, rng, /*parcel_w=*/tower_parcel_w, /*story height=*/9.0, /*num stories=*/22);

			//makeTowerParcels(/*botleft=*/Vec2d(335 + 2 * block_width + parcel_width - padding, 335 + 0 * block_width + parcel_width - padding), next_id, world_state->getRootWorldState(), /*parcel_w=*/tower_parcel_w, /*story height=*/9.0, /*num stories=*/20);
			makeTowerObjects(/*botleft=*/Vec2d(335 + 2 * block_width + parcel_width - padding, 335 + 0 * block_width + parcel_width - padding), next_id, world_state, rng, /*parcel_w=*/tower_parcel_w, /*story height=*/9.0, /*num stories=*/20);

			world_state->markAsChanged();
			//conPrint("Num parcels added: " + toString(next_id - initial_next_id));
			conPrint("Tower furniture rebuilt.");
		}
		catch(glare::Exception& e)
		{
			conPrint("Error while adding tower parcels and furniture: " + e.what());
		}
	}


	// Make hillside parcels
	// highest existing parcel id is 1385
	{
		const Vec3f parcel_positions[] = 
		{
			Vec3f(-794.1, -249, 109.06),
			Vec3f(-756.4, -212, 90.38), 
			Vec3f(-802.4, -210.8, 113.79),
			Vec3f(-751.3, -178.5, 87.20),
			Vec3f(-765.9, -138.0, 94.91),
			Vec3f(-735.1, -108.6, 77.71),
			Vec3f(-780.8, -85.2, 101.63),
			Vec3f(-848.2, -178.6, 134.33),
			Vec3f(-815.6, -151, 122.7),
			Vec3f(-812, -46, 115.7),
			Vec3f(-750, 30.8, 74.89),
			Vec3f(-777, 103.2, 82.8),
			Vec3f(-850, 9.7, 123),
			Vec3f(-853, -108, 137.5),
			Vec3f(-880, -35, 141.5),
			Vec3f(-777, -306, 96.3),
			Vec3f(-685, -69.7, 50.3),
			Vec3f(-777, 162, 76.3),
			Vec3f(-795.7, -378, 103.5),
			Vec3f(-745, -461, 79.05),
			Vec3f(-709, -370, 65.0),
			Vec3f(-666, -451, 45.8),
			Vec3f(-748, -512, 72.3),
			Vec3f(-704, -575, 48.2),
			Vec3f(-699, -702, 19.5),
			Vec3f(-669, -512, 42.0),
			Vec3f(-637, -555, 18.5),
			Vec3f(-735, -641, 42.9),
		};

		PCG32 rng(1);

		for(int i=0; i<(int)staticArrayNumElems(parcel_positions); ++i)
		{
			const Vec3f pos = parcel_positions[i];

			const ParcelID parcel_id(1386 + i);

			//TEMP: remove existing parcel
			//world_state->getRootWorldState()->parcels.erase(parcel_id);

			if(world_state->getRootWorldState()->getParcels(lock).count(parcel_id) == 0)
			{
				ParcelRef parcel = new Parcel();
				parcel->state = Parcel::State_Alive;
				parcel->id = parcel_id;
				parcel->owner_id = UserID(0);
				parcel->admin_ids.push_back(UserID(0));
				parcel->writer_ids.push_back(UserID(0));
				parcel->created_time = TimeStamp::currentTime();
				parcel->zbounds = Vec2d(pos.z - 20, pos.z + 8);

				const double parcel_w = 22 + rng.unitRandom() * 8;

				parcel->verts[0] = Vec2d(pos.x,            pos.y - 7);
				parcel->verts[1] = Vec2d(pos.x + parcel_w, pos.y - 7);
				parcel->verts[2] = Vec2d(pos.x + parcel_w, pos.y - 7 + parcel_w);
				parcel->verts[3] = Vec2d(pos.x,            pos.y - 7 + parcel_w);

				parcel->build();

				world_state->getRootWorldState()->getParcels(lock)[parcel_id] = parcel;
				world_state->getRootWorldState()->addParcelAsDBDirty(parcel, lock);
				world_state->markAsChanged();

				conPrint("Added hillside parcel with UID " + parcel_id.toString());
			}
		}
	}



	// Add road objects
	if(false)
	{
		bool have_added_roads = false;
		for(auto it = world_state->getRootWorldState()->getObjects(lock).begin(); it != world_state->getRootWorldState()->getObjects(lock).end(); ++it)
		{
			const WorldObject* object = it->second.ptr();
			if(object->creator_id.value() == 0 && object->content == "road")
				have_added_roads = true;
		}

		printVar(have_added_roads);


		if(false)
		{
			// Remove all existing road objects (UID > 1000000)
			for(auto it = world_state->getRootWorldState()->getObjects(lock).begin(); it != world_state->getRootWorldState()->getObjects(lock).end();)
			{
				if(it->second->uid.value() >= 1000000)
					it = world_state->getRootWorldState()->getObjects(lock).erase(it);
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


void WorldCreation::createPhysicsTest(ServerAllWorldsState& all_worlds_state)
{
	conPrint("creating blocks...");

	const int N = 30000;
	double phase = 0;
	double r = 4;
	for(int i=0; i<N; ++i)
	{
		const double block_height = 0.6;

		
		/*const double r = 4.0 + (double)i / N * 100.0;

		const double phase = (double)i / N * Maths::get2Pi<double>() * 20000 / r;

		const Vec3d pos(200 + cos(phase) * r, sin(phase) * r, block_height/2);*/

		const Vec3d pos(200 + cos(phase) * r, sin(phase) * r, block_height/2);

		


		WorldObjectRef test_object = new WorldObject();
		test_object->creator_id = UserID(0);
		test_object->state = WorldObject::State_Alive;
		test_object->uid = UID(10000 + i); // all_worlds_state.getNextObjectUID();//  world_state->UID(road_uid++);
		test_object->pos = pos;
		test_object->angle = (float)phase;
		test_object->axis = Vec3f(0,0,1);
		test_object->model_url = "Cube_obj_11907297875084081315.bmesh";
		test_object->scale = Vec3f(0.2, 0.05, block_height);
		test_object->materials.push_back(new WorldMaterial());

		test_object->mass = 1;
		test_object->setDynamic(true);

		// Set tex matrix based on scale
		test_object->materials[0]->tex_matrix = Matrix2f(1, 0, 0, 1);
		test_object->materials[0]->colour_texture_url = "stone_floor_jpg_6978110256346892991.jpg";

		//all_worlds_state.getRootWorldState()->objects[test_object->uid] = test_object;
		WorldStateLock lock(all_worlds_state.mutex);
		all_worlds_state.getRootWorldState()->getObjects(lock).erase(test_object->uid);
		//all_worlds_state.getRootWorldState()->addWorldObjectAsDBDirty(test_object);


		// we want to move ahead by distance d = r * theta
		const float d = 0.5f;
		const double theta = d / r;
		phase += theta;

		const double num_dominoes_per_circle = Maths::get2Pi<double>() / theta;
		r += 1.5 / num_dominoes_per_circle; // increase radius so when we have done 1 circle we have gone x metres out.
	}

}

