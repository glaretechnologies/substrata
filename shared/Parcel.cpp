/*=====================================================================
Parcel.cpp
----------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#include "Parcel.h"


#include <Exception.h>
#include <StringUtils.h>
#if GUI_CLIENT
#include "opengl/OpenGLEngine.h"
#endif
#include "../gui_client/PhysicsObject.h"


Parcel::Parcel()
:	state(State_JustCreated),
	from_remote_dirty(false),
	from_local_dirty(false)
{
}


Parcel::~Parcel()
{

}


static const uint32 PARCEL_SERIALISATION_VERSION = 1;


static void writeToStreamCommon(const Parcel& parcel, OutStream& stream)
{
	writeToStream(parcel.id, stream);
	writeToStream(parcel.owner_id, stream);
	parcel.created_time.writeToStream(stream);
	stream.writeStringLengthFirst(parcel.description);
	
	// Write admin_ids
	stream.writeUInt32((uint32)parcel.admin_ids.size());
	for(size_t i=0; i<parcel.admin_ids.size(); ++i)
		writeToStream(parcel.admin_ids[i], stream);

	// Write writer_ids
	stream.writeUInt32((uint32)parcel.writer_ids.size());
	for(size_t i=0; i<parcel.writer_ids.size(); ++i)
		writeToStream(parcel.writer_ids[i], stream);

	// Write child_parcel_ids
	stream.writeUInt32((uint32)parcel.child_parcel_ids.size());
	for(size_t i=0; i<parcel.child_parcel_ids.size(); ++i)
		writeToStream(parcel.child_parcel_ids[i], stream);

	writeToStream(parcel.aabb_min, stream);
	writeToStream(parcel.aabb_max, stream);
}


static void readFromStreamCommon(InStream& stream, Parcel& parcel) // UID will have been read already
{
	parcel.owner_id = readUserIDFromStream(stream);
	parcel.created_time.readFromStream(stream);
	parcel.description = stream.readStringLengthFirst(10000);

	// Read admin_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw Indigo::Exception("Too many admin_ids: " + toString(num));
		parcel.admin_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.admin_ids[i] = readUserIDFromStream(stream);
	}

	// Read writer_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw Indigo::Exception("Too many writer_ids: " + toString(num));
		parcel.writer_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.writer_ids[i] = readUserIDFromStream(stream);
	}

	// Read child_parcel_ids
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw Indigo::Exception("Too many child_parcel_ids: " + toString(num));
		parcel.child_parcel_ids.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.child_parcel_ids[i] = readParcelIDFromStream(stream);
	}

	parcel.aabb_min = readVec3FromStream<double>(stream);
	parcel.aabb_max = readVec3FromStream<double>(stream);
}


void writeToStream(const Parcel& parcel, OutStream& stream)
{
	// Write version
	stream.writeUInt32(PARCEL_SERIALISATION_VERSION);

	writeToStreamCommon(parcel, stream);
}


void readFromStream(InStream& stream, Parcel& parcel)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > PARCEL_SERIALISATION_VERSION)
		throw Indigo::Exception("Parcel readFromStream: Unsupported version " + toString(v) + ", expected " + toString(PARCEL_SERIALISATION_VERSION) + ".");

	parcel.id = readParcelIDFromStream(stream);
	
	readFromStreamCommon(stream, parcel);
}


void writeToNetworkStream(const Parcel& parcel, OutStream& stream)
{
	writeToStreamCommon(parcel, stream);

	stream.writeStringLengthFirst(parcel.owner_name);

	// Write admin_names
	stream.writeUInt32((uint32)parcel.admin_names.size());
	for(size_t i=0; i<parcel.admin_names.size(); ++i)
		stream.writeStringLengthFirst(parcel.admin_names[i]);

	// Write writer_names
	stream.writeUInt32((uint32)parcel.writer_names.size());
	for(size_t i=0; i<parcel.writer_names.size(); ++i)
		stream.writeStringLengthFirst(parcel.writer_names[i]);
}


void readFromNetworkStreamGivenID(InStream& stream, Parcel& parcel) // UID will have been read already
{
	readFromStreamCommon(stream, parcel);

	parcel.owner_name = stream.readStringLengthFirst(10000);

	// Read admin_names
	{
		const uint32 num = stream.readUInt32();
		if(num > 1000)
			throw Indigo::Exception("Too many admin_names: " + toString(num));
		parcel.admin_names.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.admin_names[i] = stream.readStringLengthFirst(/*max length=*/1000);
	}

	// Read writer_names
	{
		const uint32 num = stream.readUInt32();
		if(num > 1000)
			throw Indigo::Exception("Too many writer_names: " + toString(num));
		parcel.writer_names.resize(num);
		for(size_t i=0; i<num; ++i)
			parcel.writer_names[i] = stream.readStringLengthFirst(/*max length=*/1000);
	}
}
