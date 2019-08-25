/*=====================================================================
ServerWorldState.cpp
-------------------
Copyright Glare Technologies Limited 2018 -
Generated at 2016-01-12 12:22:34 +1300
=====================================================================*/
#include "ServerWorldState.h"


#include <FileInStream.h>
#include <FileOutStream.h>
#include <Exception.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <FileUtils.h>
#include <Lock.h>
#include <Clock.h>
#include <Timer.h>


#define MY_SQL_STUFF 1

#if MY_SQL_STUFF
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#endif


ServerWorldState::ServerWorldState()
{
	next_avatar_uid = UID(0);
	next_object_uid = UID(0);
	changed = false;
}


ServerWorldState::~ServerWorldState()
{
}


#if MY_SQL_STUFF


inline static const std::string twoDigitString(int x)
{
	return ::leftPad(::toString(x), '0', 2);
}


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
	const int month = thetime.tm_mon + 1; // tm_mon = Month (0 – 11; January = 0).
	const int year = thetime.tm_year + 1900; // tm_year = Year (current year minus 1900).

	return toString(year) + "-" + toString(month) + "-" + toString(day) + " " +
		twoDigitString(thetime.tm_hour) + ":" + twoDigitString(thetime.tm_min) + ":" + twoDigitString(thetime.tm_sec);
}


static void updateUsersTableFromDataStore(sql::Connection* sql_connection, ServerWorldState& world_state)
{
	// Clear users table
	std::unique_ptr<sql::PreparedStatement> delete_users(sql_connection->prepareStatement("DELETE FROM users"));
	delete_users->execute();

	for(auto i = world_state.user_id_to_users.begin(); i != world_state.user_id_to_users.end(); ++i)
	{
		Reference<User>& user = i->second;

		const std::string created_time_s = mySQLTimeStamp(user->created_time);

		std::unique_ptr<sql::PreparedStatement> add_user(sql_connection->prepareStatement("INSERT INTO users (id, created_time, name, email_address, hashed_password, password_hash_salt) VALUES (?, ?, ?, ?, ?, ?)"));
		add_user->setInt(1, user->id.value());
		add_user->setString(2, created_time_s);
		add_user->setString(3, user->name);
		add_user->setString(4, user->email_address);
		add_user->setString(5, user->hashed_password);
		add_user->setString(6, user->password_hash_salt);

		add_user->execute();
	}
}


static void updateParcelsTableFromDataStore(sql::Connection* sql_connection, ServerWorldState& world_state)
{
	// Clear tables
	std::unique_ptr<sql::PreparedStatement> delete_parcels(sql_connection->prepareStatement("DELETE FROM parcels"));  delete_parcels->execute();
	std::unique_ptr<sql::PreparedStatement> delete_parcel_admins(sql_connection->prepareStatement("DELETE FROM parcel_admins"));  delete_parcel_admins->execute();
	std::unique_ptr<sql::PreparedStatement> delete_parcel_writers(sql_connection->prepareStatement("DELETE FROM parcel_writers"));  delete_parcel_writers->execute();

	for(auto i=world_state.parcels.begin(); i != world_state.parcels.end(); ++i)
	{
		Reference<Parcel>& parcel = i->second;

		const std::string created_time_s = mySQLTimeStamp(parcel->created_time);

		std::unique_ptr<sql::PreparedStatement> add_parcel(sql_connection->prepareStatement("INSERT INTO parcels (id, owner_id, created_time, description, all_writeable,		\
			vert0_x, vert0_y,			\
			vert1_x, vert1_y,			\
			vert2_x, vert2_y,			\
			vert3_x, vert3_y,			\
			zbounds_x, zbounds_y) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
		int col = 1;
		add_parcel->setInt(col++, parcel->id.value());
		add_parcel->setInt(col++, parcel->owner_id.value());
		add_parcel->setString(col++, created_time_s);
		add_parcel->setString(col++, parcel->description);
		add_parcel->setInt(col++, parcel->all_writeable ? 1 : 0);
		for(int z=0; z<4; ++z)
		{
			add_parcel->setDouble(col++, parcel->verts[z].x);
			add_parcel->setDouble(col++, parcel->verts[z].y);
		}

		add_parcel->setDouble(col++, parcel->zbounds.x);
		add_parcel->setDouble(col++, parcel->zbounds.y);

		add_parcel->execute();


		// Add parcel admins for this parcel
		for(size_t z=0; z<parcel->admin_ids.size(); ++z)
		{
			std::unique_ptr<sql::PreparedStatement> add_parcel_admin(sql_connection->prepareStatement("INSERT INTO parcel_admins (parcel_id, user_id) VALUES (?, ?)"));
			add_parcel_admin->setInt(1, (int)parcel->id.value());
			add_parcel_admin->setInt(2, (int)parcel->admin_ids[z].value());
		}

		// Add parcel writers for this parcel
		for(size_t z=0; z<parcel->writer_ids.size(); ++z)
		{
			std::unique_ptr<sql::PreparedStatement> add_parcel_writer(sql_connection->prepareStatement("INSERT INTO parcel_writers (parcel_id, user_id) VALUES (?, ?)"));
			add_parcel_writer->setInt(1, (int)parcel->id.value());
			add_parcel_writer->setInt(2, (int)parcel->writer_ids[z].value());
		}
	}
}



static void readUsersFromMySQL(sql::Connection* sql_connection, ServerWorldState& world_state)
{
	conPrint("Reading users from MySQL...");
	Timer timer;

	std::unique_ptr<sql::PreparedStatement> stmt(sql_connection->prepareStatement(
		"SELECT id, UNIX_TIMESTAMP(CONVERT_TZ(created_time, '+00:00', 'SYSTEM')), name, email_address, hashed_password, password_hash_salt FROM users"));
	std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

	world_state.user_id_to_users.clear();
	world_state.name_to_users.clear();

	while(res->next())
	{
		UserRef user = new User();

		user->id = UserID(res->getInt("id"));

		user->created_time = TimeStamp((uint64)res->getUInt64(2)); // Get the UNIX_TIMESTAMP() result

		user->name = res->getString("name");
		user->email_address = res->getString("email_address");
		user->hashed_password = res->getString("hashed_password");
		user->password_hash_salt = res->getString("password_hash_salt");

		// conPrint("");
		// conPrint("user->id: " + user->id.toString());
		// conPrint("user->created_time: " + user->created_time.RFC822FormatedString());
		// conPrint(user->name);
		// conPrint(user->email_address);
		// conPrint(user->hashed_password);
		// conPrint(user->password_hash_salt);

		world_state.user_id_to_users[user->id] = user;
		world_state.name_to_users[user->name] = user;
	}

	conPrint("\tDone. (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
}


static void readParcelsFromMySQL(sql::Connection* sql_connection, ServerWorldState& world_state)
{
	conPrint("Reading parcels from MySQL...");
	Timer timer;
	{
		std::unique_ptr<sql::PreparedStatement> stmt(sql_connection->prepareStatement(
			"SELECT id, owner_id, UNIX_TIMESTAMP(CONVERT_TZ(created_time, '+00:00', 'SYSTEM')), description, all_writeable,		\
			vert0_x, vert0_y,																									\
			vert1_x, vert1_y,																									\
			vert2_x, vert2_y,																									\
			vert3_x, vert3_y,																									\
			zbounds_x, zbounds_y																								\
			FROM parcels"));
		std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

		world_state.parcels.clear();

		while(res->next())
		{
			ParcelRef parcel = new Parcel();

			parcel->id = ParcelID(res->getInt("id"));
			parcel->owner_id = UserID(res->getInt("owner_id"));

			parcel->created_time = TimeStamp((uint64)res->getUInt64(3)); // Get the UNIX_TIMESTAMP() result

			parcel->description = res->getString("description");
			parcel->all_writeable = res->getInt("all_writeable") != 0;

			parcel->verts[0].x = (double)res->getDouble("vert0_x");
			parcel->verts[0].y = (double)res->getDouble("vert0_y");
			parcel->verts[1].x = (double)res->getDouble("vert1_x");
			parcel->verts[1].y = (double)res->getDouble("vert1_y");
			parcel->verts[2].x = (double)res->getDouble("vert2_x");
			parcel->verts[2].y = (double)res->getDouble("vert2_y");
			parcel->verts[3].x = (double)res->getDouble("vert3_x");
			parcel->verts[3].y = (double)res->getDouble("vert3_y");
			parcel->zbounds.x = (double)res->getDouble("zbounds_x");
			parcel->zbounds.y = (double)res->getDouble("zbounds_y");
			/*
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
			parcel->zbounds.y = (double)row[col++];*/

			world_state.parcels[parcel->id] = parcel;
		}
	}
	{
		// Read data from parcel_admins table
		std::unique_ptr<sql::PreparedStatement> stmt(sql_connection->prepareStatement(
			"SELECT parcel_id, user_id FROM parcel_admins"));
		std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

		while(res->next())
		{
			const ParcelID parcel_id(res->getInt("parcel_id"));
			const UserID user_id(res->getInt("user_id"));

			assert(world_state.parcels.count(parcel_id) > 0);
			//assert(world_state.user_id_to_users.count(user_id) > 0);

			world_state.parcels[parcel_id]->admin_ids.push_back(user_id);
		}
	}
	{
		// Read data from parcel_writers table
		std::unique_ptr<sql::PreparedStatement> stmt(sql_connection->prepareStatement(
			"SELECT parcel_id, user_id FROM parcel_writers"));
		std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

		while(res->next())
		{
			const ParcelID parcel_id(res->getInt("parcel_id"));
			const UserID user_id(res->getInt("user_id"));

			assert(world_state.parcels.count(parcel_id) > 0);
			//assert(world_state.user_id_to_users.count(user_id) > 0);

			world_state.parcels[parcel_id]->writer_ids.push_back(user_id);
		}
	}
	conPrint("\tDone. (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
}


#endif // MY_SQL_STUFF


void ServerWorldState::updateFromDatabase()
{
#if MY_SQL_STUFF
	try
	{
		Timer timer;
		sql::mysql::MySQL_Driver* sql_driver = sql::mysql::get_mysql_driver_instance();
		sql_driver->threadInit();

		sql::Connection* sql_connection = sql_driver->connect("tcp://127.0.0.1:3306", "root", "5TD853_J6RQFm");
		sql_connection->setSchema("sys");

		conPrint("Conecting to DB took " + timer.elapsedStringNSigFigs(3));

		updateUsersTableFromDataStore(sql_connection, *this);
		updateParcelsTableFromDataStore(sql_connection, *this);
		readUsersFromMySQL(sql_connection, *this);
		readParcelsFromMySQL(sql_connection, *this);
		conPrint("done.");

		delete sql_connection;
	}
	catch(sql::SQLException& e)
	{
		throw Indigo::Exception("MySQL Error: " + std::string(e.what()));
	}
#endif // MY_SQL_STUFF
}


static const uint32 WORLD_STATE_MAGIC_NUMBER = 487173571;
static const uint32 WORLD_STATE_SERIALISATION_VERSION = 1;
static const uint32 WORLD_OBJECT_CHUNK = 100;
static const uint32 USER_CHUNK = 101;
static const uint32 PARCEL_CHUNK = 102;
static const uint32 RESOURCE_CHUNK = 103;
static const uint32 EOS_CHUNK = 1000;


void ServerWorldState::readFromDisk(const std::string& path)
{
	conPrint("Reading world state from '" + path + "'...");

	FileInStream stream(path);

	// Read magic number
	const uint32 m = stream.readUInt32();
	if(m != WORLD_STATE_MAGIC_NUMBER)
		throw Indigo::Exception("Invalid magic number " + toString(m) + ", expected " + toString(WORLD_STATE_MAGIC_NUMBER) + ".");

	// Read version
	const uint32 v = stream.readUInt32();
	if(v != WORLD_STATE_SERIALISATION_VERSION)
		throw Indigo::Exception("Unknown version " + toString(v) + ", expected " + toString(WORLD_STATE_SERIALISATION_VERSION) + ".");

	while(1)
	{
		const uint32 chunk = stream.readUInt32();
		if(chunk == WORLD_OBJECT_CHUNK)
		{
			// Deserialise object
			WorldObjectRef world_ob = new WorldObject();
			readFromStream(stream, *world_ob);

			objects[world_ob->uid] = world_ob; // Add to object map

			next_object_uid = UID(myMax(world_ob->uid.value() + 1, next_object_uid.value()));
		}
		else if(chunk == USER_CHUNK)
		{
			// Deserialise user
			UserRef user = new User();
			readFromStream(stream, *user);

			user_id_to_users[user->id] = user; // Add to user map
			name_to_users[user->name] = user; // Add to user map
		}
		else if(chunk == PARCEL_CHUNK)
		{
			// Deserialise parcel
			ParcelRef parcel = new Parcel();
			readFromStream(stream, *parcel);

			parcels[parcel->id] = parcel; // Add to parcel map
		}
		else if(chunk == RESOURCE_CHUNK)
		{
			// Deserialise resource
			ResourceRef resource = new Resource();
			readFromStream(stream, *resource);

			conPrint("Loaded resource:\n  URL: '" + resource->URL + "'\n  local_path: '" + resource->getLocalPath() + "'\n  owner_id: " + resource->owner_id.toString());

			this->resource_manager->addResource(resource);
		}
		else if(chunk == EOS_CHUNK)
		{
			break;
		}
		else
		{
			throw Indigo::Exception("Unknown chunk type '" + toString(chunk) + "'");
		}
	}

	denormaliseData();

	conPrint("Loaded " + toString(objects.size()) + " object(s), " + toString(user_id_to_users.size()) + " user(s), " + 
		toString(parcels.size()) + " parcel(s), " + toString(resource_manager->getResourcesForURL().size()) + " resource(s).");
}


void ServerWorldState::denormaliseData()
{
	// Build cached fields like creator_name
	for(auto i=objects.begin(); i != objects.end(); ++i)
	{
		auto res = user_id_to_users.find(i->second->creator_id);
		if(res != user_id_to_users.end())
			i->second->creator_name = res->second->name;
	}

	for(auto i=parcels.begin(); i != parcels.end(); ++i)
	{
		// Denormalise owner_name
		{
			auto res = user_id_to_users.find(i->second->owner_id);
			if(res != user_id_to_users.end())
				i->second->owner_name = res->second->name;
		}

		// Denormalise admin_names
		i->second->admin_names.resize(i->second->admin_ids.size());
		for(size_t z=0; z<i->second->admin_ids.size(); ++z)
		{
			auto res = user_id_to_users.find(i->second->owner_id);
			if(res != user_id_to_users.end())
			{
				//conPrint("admin: " + res->second->name);
				i->second->admin_names[z] = res->second->name;
			}
		}

		// Denormalise writer_names
		i->second->writer_names.resize(i->second->writer_ids.size());
		for(size_t z=0; z<i->second->writer_ids.size(); ++z)
		{
			auto res = user_id_to_users.find(i->second->owner_id);
			if(res != user_id_to_users.end())
			{
				//conPrint("writer: " + res->second->name);
				i->second->writer_names[z] = res->second->name;
			}
		}
	}
}


void ServerWorldState::serialiseToDisk(const std::string& path)
{
	conPrint("Saving world state to disk...");
	try
	{

		const std::string temp_path = path + "_temp";
		{
			FileOutStream stream(temp_path);

			// Write magic number
			stream.writeUInt32(WORLD_STATE_MAGIC_NUMBER);

			// Write version
			stream.writeUInt32(WORLD_STATE_SERIALISATION_VERSION);

			// Write objects
			{
				for(auto i=objects.begin(); i != objects.end(); ++i)
				{
					stream.writeUInt32(WORLD_OBJECT_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			// Write users
			{
				for(auto i=user_id_to_users.begin(); i != user_id_to_users.end(); ++i)
				{
					stream.writeUInt32(USER_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			// Write parcels
			{
				for(auto i=parcels.begin(); i != parcels.end(); ++i)
				{
					stream.writeUInt32(PARCEL_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			// Write resource objects
			{
				for(auto i=resource_manager->getResourcesForURL().begin(); i != resource_manager->getResourcesForURL().end(); ++i)
				{
					stream.writeUInt32(RESOURCE_CHUNK);
					writeToStream(*i->second, stream);
				}
			}

			stream.writeUInt32(EOS_CHUNK); // Write end-of-stream chunk
		}

		FileUtils::moveFile(temp_path, path);

		conPrint("Saved " + toString(objects.size()) + " object(s), " + toString(user_id_to_users.size()) + " user(s), " + 
			toString(parcels.size()) + " parcel(s), " + toString(resource_manager->getResourcesForURL().size()) + " resource(s).");
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw Indigo::Exception(e.what());
	}
}


UID ServerWorldState::getNextObjectUID()
{
	const UID next = next_object_uid;
	next_object_uid = UID(next_object_uid.value() + 1);
	return next;
}


UID ServerWorldState::getNextAvatarUID()
{
	Lock lock(mutex);

	const UID next = next_avatar_uid;
	next_avatar_uid = UID(next_avatar_uid.value() + 1);
	return next;
}
