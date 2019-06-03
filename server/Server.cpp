/*=====================================================================
Server.cpp
---------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include "WorkerThread.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../networking/networking.h"
#include <ThreadManager.h>
#include <PlatformUtils.h>
#include <Clock.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Parser.h>
#include <Base64.h>
#include <ArgumentParser.h>
#include <SocketBufferOutStream.h>
#include <TLSSocket.h>
#include <MTwister.h>

#if MY_SQL_STUFF
#include <mysqlx/xdevapi.h>
#endif


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

		world_state->parcels[parcel_id] = test_parcel;
	}
}


static void makeBlock(const Vec2d& botleft, MTwister& rng, int& next_id, Reference<ServerWorldState> world_state)
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
				test_parcel->zbounds = Vec2d(-1, 10);

				test_parcel->verts[0] = botleft + Vec2d(xi * 20, yi * 20);
				test_parcel->verts[1] = botleft + Vec2d((xi+1)* 20, yi * 20);
				test_parcel->verts[2] = botleft + Vec2d((xi+1)* 20, (yi+1) * 20);
				test_parcel->verts[3] = botleft + Vec2d((xi)* 20, (yi+1) * 20);

				world_state->parcels[parcel_id] = test_parcel;
			}
		}
}


static void enqueuePacketToBroadcast(SocketBufferOutStream& packet_buffer, std::vector<std::string>& broadcast_packets)
{
	if(packet_buffer.buf.size() > 0)
	{
		std::string packet_string(packet_buffer.buf.size(), '\0');

		std::memcpy(&packet_string[0], packet_buffer.buf.data(), packet_buffer.buf.size());

		broadcast_packets.push_back(packet_string);
	}
}


static void assignParcelToUser(Reference<ServerWorldState>& world_state, const ParcelID& parcel_id, const UserID& user_id)
{
	conPrint("Assigning parcel " + parcel_id.toString() + " to user " + user_id.toString());

	if(world_state->parcels.count(parcel_id) != 0)
	{
		ParcelRef parcel = world_state->parcels.find(parcel_id)->second;

		parcel->owner_id = user_id;
		parcel->admin_ids = std::vector<UserID>(1, user_id);
		parcel->writer_ids = std::vector<UserID>(1, user_id);

		conPrint("\tDone.");
	}
	else
	{
		conPrint("\tFailed, parcel not found.");
	}
}


inline static const std::string twoDigitString(int x)
{
	return ::leftPad(::toString(x), '0', 2);
}


#if MY_SQL_STUFF
// Format a timestamp into MySQL TIMESTAMP format: YYYY-MM-DD HH:MM:SS
static std::string mySQLTimeStamp(const TimeStamp& timestamp)
{
	time_t t = timestamp.time;

	tm thetime;
	// Get calender time in UTC.  Use threadsafe versions of gmtime.
#ifdef _WIN32
	gmtime_s(&thetime, &t);
#else
	gmtime_r(&t, &thetime);
#endif

	const int day = thetime.tm_mday; // Day of month (1 – 31).
	const int month = thetime.tm_mon + 1; // Month (0 – 11; January = 0).
	const int year = thetime.tm_year + 1900; // tm_year = Year (current year minus 1900).

	return toString(year) + "-" + toString(month) + "-" + toString(day) + " " + 
		twoDigitString(thetime.tm_hour) + ":" + twoDigitString(thetime.tm_min) + ":" + twoDigitString(thetime.tm_sec);
}


static void updateUsersTableFromDataStore(mysqlx::Schema& default_schema, ServerWorldState& world_state)
{
	mysqlx::Table users_table = default_schema.getTable("users");

	// Clear users table
	users_table.remove().execute();

	// https://dev.mysql.com/doc/x-devapi-userguide/en/sql-crud-functions.html#table-insert
	for(auto i = world_state.user_id_to_users.begin(); i != world_state.user_id_to_users.end(); ++i)
	{
		Reference<User>& user = i->second;

		const std::string created_time_s = mySQLTimeStamp(user->created_time);

		const mysqlx::Value hashed_password_val(mysqlx::bytes((const mysqlx::byte*)user->hashed_password.data(), user->hashed_password.size()));

		users_table.insert("id", "created_time", "name", "email_address", "hashed_password", "password_hash_salt")
			.values((int)user->id.value(), created_time_s, user->name, user->email_address, hashed_password_val, user->password_hash_salt)
			.execute();
	}
}


static void updateParcelsTableFromDataStore(mysqlx::Schema& default_schema, ServerWorldState& world_state)
{
	mysqlx::Table parcels_table = default_schema.getTable("parcels");
	mysqlx::Table parcel_admins_table = default_schema.getTable("parcel_admins");
	mysqlx::Table parcel_writers_table = default_schema.getTable("parcel_writers");

	// Clear tables
	parcels_table.remove().execute();
	parcel_admins_table.remove().execute();
	parcel_writers_table.remove().execute();

	// https://dev.mysql.com/doc/x-devapi-userguide/en/sql-crud-functions.html#table-insert
	for(auto i=world_state.parcels.begin(); i != world_state.parcels.end(); ++i)
	{
		Reference<Parcel>& parcel = i->second;

		const std::string created_time_s = mySQLTimeStamp(parcel->created_time);

		parcels_table.insert("id", "owner_id", "created_time", "description", "all_writeable",
			"vert0_x", "vert0_y",
			"vert1_x", "vert1_y",
			"vert2_x", "vert2_y",
			"vert3_x", "vert3_y",
			"zbounds_x", "zbounds_y")
			.values((int)parcel->id.value(), (int)parcel->owner_id.value(), created_time_s, parcel->description, parcel->all_writeable,
				parcel->verts[0].x, parcel->verts[0].y,
				parcel->verts[1].x, parcel->verts[1].y,
				parcel->verts[2].x, parcel->verts[2].y,
				parcel->verts[3].x, parcel->verts[3].y,
				parcel->zbounds.x, parcel->zbounds.y)
			.execute();

		// Add parcel admins for this parcel
		for(size_t z=0; z<parcel->admin_ids.size(); ++z)
			parcel_admins_table.insert("parcel_id", "user_id")
				.values((int)parcel->id.value(), (int)parcel->admin_ids[z].value())
				.execute();

		// Add parcel writers for this parcel
		for(size_t z=0; z<parcel->writer_ids.size(); ++z)
			parcel_writers_table.insert("parcel_id", "user_id")
			.values((int)parcel->id.value(), (int)parcel->writer_ids[z].value())
			.execute();
	}
}


static void readUsersFromMySQL(mysqlx::Schema& default_schema, ServerWorldState& world_state)
{
	conPrint("Reading users from MySQL...");
	Timer timer;

	mysqlx::Table users_table = default_schema.getTable("users");
	mysqlx::RowResult result = users_table.select("id", "UNIX_TIMESTAMP(CONVERT_TZ(created_time, '+00:00', 'SYSTEM'))", "name", "email_address", "hashed_password", "password_hash_salt").execute();

	std::list<mysqlx::Row> rows = result.fetchAll();

	world_state.user_id_to_users.clear();
	world_state.name_to_users.clear();

	for(auto i = rows.begin(); i != rows.end(); ++i)
	{
		mysqlx::Row row = *i;

		UserRef user = new User();

		user->id = UserID((int)row[0]);

		user->created_time = TimeStamp((uint64)row[1]);

		user->name = (std::string)row[2];
		user->email_address = (std::string)row[3];

		user->hashed_password = (std::string)row[4];
		user->password_hash_salt = (std::string)row[5];

		world_state.user_id_to_users[user->id] = user;
		world_state.name_to_users[user->name] = user;
	}

	conPrint("\tDone. (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
}


static void readParcelsFromMySQL(mysqlx::Schema& default_schema, ServerWorldState& world_state)
{
	conPrint("Reading parcels from MySQL...");
	Timer timer;
	{
		mysqlx::Table parcels_table = default_schema.getTable("parcels");
		mysqlx::RowResult result = parcels_table.select("id", "owner_id", "UNIX_TIMESTAMP(CONVERT_TZ(created_time, '+00:00', 'SYSTEM'))", "description", "all_writeable",
			"vert0_x", "vert0_y",
			"vert1_x", "vert1_y",
			"vert2_x", "vert2_y",
			"vert3_x", "vert3_y",
			"zbounds_x", "zbounds_y").execute();

		std::list<mysqlx::Row> rows = result.fetchAll();

		world_state.parcels.clear();

		for(auto i = rows.begin(); i != rows.end(); ++i)
		{
			mysqlx::Row row = *i;

			ParcelRef parcel = new Parcel();

			parcel->id = ParcelID((int)row[0]);
			parcel->owner_id = UserID((int)row[1]);

			parcel->created_time = TimeStamp((uint64)row[2]);

			parcel->description = (std::string)row[3];
			parcel->all_writeable = (int)row[4] != 0;

			int col = 5;
			parcel->verts[0].x = (double)row[col++];
			parcel->verts[0].y = (double)row[col++];
			parcel->verts[1].x = (double)row[col++];
			parcel->verts[1].y = (double)row[col++];
			parcel->verts[2].x = (double)row[col++];
			parcel->verts[2].y = (double)row[col++];
			parcel->verts[3].x = (double)row[col++];
			parcel->verts[3].y = (double)row[col++];
			parcel->zbounds.x = (double)row[col++];
			parcel->zbounds.y = (double)row[col++];

			world_state.parcels[parcel->id] = parcel;
		}
	}
	{
		// Read data from parcel_admins table
		mysqlx::Table parcel_admins_table = default_schema.getTable("parcel_admins");
		mysqlx::RowResult result = parcel_admins_table.select("parcel_id", "user_id").execute();
		std::list<mysqlx::Row> rows = result.fetchAll();
		for(auto i = rows.begin(); i != rows.end(); ++i)
		{
			mysqlx::Row row = *i;

			const ParcelID parcel_id((int)row[0]);
			const UserID user_id((int)row[1]);

			assert(world_state.parcels.count(parcel_id) > 0);
			//assert(world_state.user_id_to_users.count(user_id) > 0);

			world_state.parcels[parcel_id]->admin_ids.push_back(user_id);
		}
	}
	{
		// Read data from parcel_writers table
		mysqlx::Table parcel_writers_table = default_schema.getTable("parcel_writers");
		mysqlx::RowResult result = parcel_writers_table.select("parcel_id", "user_id").execute();
		std::list<mysqlx::Row> rows = result.fetchAll();
		for(auto i = rows.begin(); i != rows.end(); ++i)
		{
			mysqlx::Row row = *i;

			const ParcelID parcel_id((int)row[0]);
			const UserID user_id((int)row[1]);

			assert(world_state.parcels.count(parcel_id) > 0);
			//assert(world_state.user_id_to_users.count(user_id) > 0);

			world_state.parcels[parcel_id]->writer_ids.push_back(user_id);
		}
	}
	conPrint("\tDone. (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
}
#endif


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();
	TLSSocket::initTLS();

	conPrint("Substrata server v" + ::cyberspace_version);

	try
	{
		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--src_resource_dir"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax);

		// src_resource_dir can be set to something like C:\programming\chat_site\trunk to read e.g. script.js directly from trunk
		std::string src_resource_dir = "./";
		if(parsed_args.isArgPresent("--src_resource_dir"))
			src_resource_dir = parsed_args.getArgStringValue("--src_resource_dir");

		conPrint("src_resource_dir: '" + src_resource_dir + "'");

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
#if BUILD_TESTS
			Base64::test();
			Parser::doUnitTests();
			conPrint("----Finished tests----");
#endif
			return 0;
		}
		//-----------------------------------------------------------------------------------------

		const int listen_port = 7600;
		conPrint("listen port: " + toString(listen_port));

#if _WIN32
		const std::string server_state_dir = "D:/cyberspace_server_state";
#else
		const std::string server_state_dir = "/home/nick/cyberspace_server_state";
#endif

		const std::string server_resource_dir = server_state_dir + "/server_resources";
		conPrint("server_resource_dir: " + server_resource_dir);


		FileUtils::createDirIfDoesNotExist(server_resource_dir);
		
		Server server;
		server.world_state->resource_manager = new ResourceManager(server_resource_dir);

		const std::string server_state_path = server_state_dir + "/server_state.bin";
		if(FileUtils::fileExists(server_state_path))
			server.world_state->readFromDisk(server_state_path);



		//==============================================================================
		// https://dev.mysql.com/doc/x-devapi-userguide/en/using-sql.html
#if MY_SQL_STUFF
		try
		{
			Timer timer;
			const std::string url = "root:5TD853_J6RQFm@127.0.0.1:33060/sys?ssl-mode=required";
			mysqlx::SessionSettings settings(url);
			mysqlx::Session session(settings);

			conPrint("Conecting to DB took " + timer.elapsedStringNSigFigs(3));

			mysqlx::Schema default_schema = session.getDefaultSchema();

			updateUsersTableFromDataStore(default_schema, *server.world_state);
			updateParcelsTableFromDataStore(default_schema, *server.world_state);

			readUsersFromMySQL(default_schema, *server.world_state);
			readParcelsFromMySQL(default_schema, *server.world_state);

			conPrint("done.");
		}
		catch(mysqlx::Error& err)
		{
			conPrint("MySQL Error: " + std::string(err.what()));
			return 1;
		}
#endif

		//TEMP:
		//server.world_state->resource_manager->getResourcesForURL().clear();

		// Add a teapot object
		WorldObjectRef test_object;
		if(false)
		{
			const UID uid(6000);
			test_object = new WorldObject();
			test_object->state = WorldObject::State_Alive;
			test_object->uid = uid;
			test_object->pos = Vec3d(3, 0, 1);
			test_object->angle = 0;
			test_object->axis = Vec3f(1,0,0);
			test_object->model_url = "teapot_obj_12507117953098989663.obj";
			test_object->scale = Vec3f(1.f);
			server.world_state->objects[uid] = test_object;
		}

		//TEMP
		//server.world_state->parcels.clear();

		// TEMP: Add a parcel
		if(false)
		{
			const ParcelID parcel_id(7000);
			ParcelRef test_parcel = new Parcel();
			test_parcel->state = Parcel::State_Alive;
			test_parcel->id = parcel_id;
			test_parcel->owner_id = UserID(0);
			test_parcel->admin_ids.push_back(UserID(0));
			test_parcel->writer_ids.push_back(UserID(0));
			test_parcel->created_time = TimeStamp::currentTime();
			test_parcel->verts[0] = Vec2d(-10, -10);
			test_parcel->verts[1] = Vec2d( 10, -10);
			test_parcel->verts[2] = Vec2d( 10,  10);
			test_parcel->verts[3] = Vec2d(-10,  10);
			test_parcel->zbounds = Vec2d(-1, 10);
			test_parcel->build();

			test_parcel->description = "This is a pretty cool parcel.";
			server.world_state->parcels[parcel_id] = test_parcel;
		}

		
		// Add 'town square' parcels
		if(server.world_state->parcels.empty())
		{
			conPrint("Adding some parcels!");

			int next_id = 10;
			makeParcels(Matrix2d(1, 0, 0, 1), next_id, server.world_state);
			makeParcels(Matrix2d(-1, 0, 0, 1), next_id, server.world_state); // Mirror in y axis (x' = -x)
			makeParcels(Matrix2d(0, 1, 1, 0), next_id, server.world_state); // Mirror in x=y line(x' = y, y' = x)
			makeParcels(Matrix2d(0, 1, -1, 0), next_id, server.world_state); // Rotate right 90 degrees (x' = y, y' = -x)
			makeParcels(Matrix2d(1, 0, 0, -1), next_id, server.world_state); // Mirror in x axis (y' = -y)
			makeParcels(Matrix2d(-1, 0, 0, -1), next_id, server.world_state); // Rotate 180 degrees (x' = -x, y' = -y)
			makeParcels(Matrix2d(0, -1, -1, 0), next_id, server.world_state); // Mirror in x=-y line (x' = -y, y' = -x)
			makeParcels(Matrix2d(0, -1, 1, 0), next_id, server.world_state); // Rotate left 90 degrees (x' = -y, y' = x)

			MTwister rng(1);
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
						makeBlock(Vec2d(5 + x*70, 5 + y*70), rng, next_id, server.world_state);
				}
		}

		// TEMP: make all parcels have zmax = 10
		{
			for(auto i = server.world_state->parcels.begin(); i != server.world_state->parcels.end(); ++i)
			{
				ParcelRef parcel = i->second;
				parcel->zbounds.y = 10.0f;
			}
		}

		// TEMP: Assign some parcel permissions
		assignParcelToUser(server.world_state, ParcelID(10), UserID(1));
		assignParcelToUser(server.world_state, ParcelID(11), UserID(2)); // dirtypunk
		assignParcelToUser(server.world_state, ParcelID(12), UserID(3)); // zom-b
		assignParcelToUser(server.world_state, ParcelID(32), UserID(4)); // lycium
		assignParcelToUser(server.world_state, ParcelID(31), UserID(5)); // Harry
		assignParcelToUser(server.world_state, ParcelID(40), UserID(8)); // trislit
		assignParcelToUser(server.world_state, ParcelID(30), UserID(9)); // fused

		// Make parcel with id 20 a 'sandbox', world-writeable parcel
		{
			auto res = server.world_state->parcels.find(ParcelID(20));
			if(res != server.world_state->parcels.end())
			{
				res->second->all_writeable = true;
				conPrint("Made parcel 20 all-writeable.");
			}
		}



		server.world_state->denormaliseData();

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, &server));
		//thread_manager.addThread(new DataStoreSavingThread(data_store));

		Timer save_state_timer;

		// Main server loop
		uint64 loop_iter = 0;
		while(1)
		{
			PlatformUtils::Sleep(100);

			std::vector<std::string> broadcast_packets;

			Lock lock(server.world_state->mutex);

			//TEMP:
			/*{
				const double theta = Clock::getCurTimeRealSec();
				test_object->pos = Vec3d(sin(theta), 2, cos(theta) + 1);
				test_object->from_remote_transform_dirty = true;
			}*/

			// Generate packets for avatar changes
			for(auto i = server.world_state->avatars.begin(); i != server.world_state->avatars.end();)
			{
				Avatar* avatar = i->second.getPointer();
				if(avatar->other_dirty)
				{
					if(avatar->state == Avatar::State_Alive)
					{
						// Send AvatarFullUpdate packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::AvatarFullUpdate);
						writeToNetworkStream(*avatar, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						avatar->other_dirty = false;
						avatar->transform_dirty = false;
						i++;
					}
					else if(avatar->state == Avatar::State_JustCreated)
					{
						// Send AvatarCreated packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::AvatarCreated);
						writeToStream(avatar->uid, packet);
						packet.writeStringLengthFirst(avatar->name);
						packet.writeStringLengthFirst(avatar->model_url);
						writeToStream(avatar->pos, packet);
						writeToStream(avatar->rotation, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						avatar->state = Avatar::State_Alive;
						avatar->other_dirty = false;
						avatar->transform_dirty = false;

						i++;
					}
					else if(avatar->state == Avatar::State_Dead)
					{
						// Send AvatarDestroyed packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::AvatarDestroyed);
						writeToStream(avatar->uid, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						// Remove avatar from avatar map
						auto old_avatar_iterator = i;
						i++;
						server.world_state->avatars.erase(old_avatar_iterator);

						conPrint("Removed avatar from world_state->avatars");
					}
					else
					{
						assert(0);
					}
				}
				else if(avatar->transform_dirty)
				{
					if(avatar->state == Avatar::State_Alive)
					{
						// Send AvatarTransformUpdate packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::AvatarTransformUpdate);
						writeToStream(avatar->uid, packet);
						writeToStream(avatar->pos, packet);
						writeToStream(avatar->rotation, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						avatar->transform_dirty = false;
					}
					i++;
				}
				else
				{
					i++;
				}
			}



			// Generate packets for object changes
			for(auto i = server.world_state->objects.begin(); i != server.world_state->objects.end();)
			{
				WorldObject* ob = i->second.getPointer();
				if(ob->from_remote_other_dirty)
				{
					conPrint("Object 'other' dirty, sending full update");

					if(ob->state == WorldObject::State_Alive)
					{
						// Send ObjectFullUpdate packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::ObjectFullUpdate);
						writeToNetworkStream(*ob, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						ob->from_remote_other_dirty = false;
						ob->from_remote_transform_dirty = false; // transform is sent in full packet also.
						server.world_state->changed = true;
						i++;
					}
					else if(ob->state == WorldObject::State_JustCreated)
					{
						// Send ObjectCreated packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::ObjectCreated);
						writeToNetworkStream(*ob, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						ob->state = WorldObject::State_Alive;
						ob->from_remote_other_dirty = false;
						server.world_state->changed = true;
						i++;
					}
					else if(ob->state == WorldObject::State_Dead)
					{
						// Send ObjectDestroyed packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::ObjectDestroyed);
						writeToStream(ob->uid, packet);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						// Remove ob from object map
						auto old_ob_iterator = i;
						i++;
						server.world_state->objects.erase(old_ob_iterator);

						conPrint("Removed object from world_state->objects");
						server.world_state->changed = true;
					}
					else
					{
						conPrint("ERROR: invalid object state.");
						assert(0);
					}
				}
				else if(ob->from_remote_transform_dirty)
				{
					//conPrint("Object 'transform' dirty, sending transform update");

					if(ob->state == WorldObject::State_Alive)
					{
						// Send ObjectTransformUpdate packet
						SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
						packet.writeUInt32(Protocol::ObjectTransformUpdate);
						writeToStream(ob->uid, packet);
						writeToStream(ob->pos, packet);
						writeToStream(ob->axis, packet);
						packet.writeFloat(ob->angle);

						enqueuePacketToBroadcast(packet, broadcast_packets);

						ob->from_remote_transform_dirty = false;
						server.world_state->changed = true;
					}
					i++;
				}
				else
				{
					i++;
				}
			}

			// Enqueue packets to worker threads to send
			{
				Lock lock2(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					for(size_t z=0; z<broadcast_packets.size(); ++z)
					{
						assert(dynamic_cast<WorkerThread*>(i->getPointer()));
						static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(broadcast_packets[z]);
					}
				}
			}
			
			if((loop_iter % 40) == 0) // Approx every 4 s.
			{
				// Send out TimeSyncMessage packets to clients
				SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);
				packet.writeUInt32(Protocol::TimeSyncMessage);
				packet.writeDouble(server.getCurrentGlobalTime());
				std::string packet_string(packet.buf.size(), '\0');
				std::memcpy(&packet_string[0], packet.buf.data(), packet.buf.size());

				Lock lock3(server.worker_thread_manager.getMutex());
				for(auto i = server.worker_thread_manager.getThreads().begin(); i != server.worker_thread_manager.getThreads().end(); ++i)
				{
					assert(dynamic_cast<WorkerThread*>(i->getPointer()));
					static_cast<WorkerThread*>(i->getPointer())->enqueueDataToSend(packet_string);
				}
			}


			if(server.world_state->changed && (save_state_timer.elapsed() > 5.0))
			{
				try
				{
					// Save world state to disk
					Lock lock2(server.world_state->mutex);

					server.world_state->serialiseToDisk(server_state_path);

					server.world_state->changed = false;
					save_state_timer.reset();
				}
				catch(Indigo::Exception& e)
				{
					conPrint("Warning: saving world state to disk failed: " + e.what());
					save_state_timer.reset(); // Reset timer so we don't try again straight away.
				}
			}


			loop_iter++;
		} // End of main server loop
	}
	catch(ArgumentParserExcep& e)
	{
		conPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}
	catch(Indigo::Exception& e)
	{
		conPrint("Indigo::Exception: " + e.what());
		return 1;
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		conPrint("FileUtils::FileUtilsExcep: " + e.what());
		return 1;
	}

	Networking::destroyInstance();
	return 0;
}


Server::Server()
{
	world_state = new ServerWorldState();
}


double Server::getCurrentGlobalTime() const
{
	return Clock::getTimeSinceInit();
}
