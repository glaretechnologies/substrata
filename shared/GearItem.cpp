/*=====================================================================
GearItem.cpp
------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearItem.h"


#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/RuntimeCheck.h>


static const uint32 GEAR_ITEM_SERIALISATION_VERSION = 1;


GearItem::GearItem()
:	translation(0.f),
	axis(1.f,0,0),
	angle(0),
	scale(1.f),
	flags(0),
	max_supply(0)
{
}


bool GearItem::operator==(const GearItem& other) const
{
	// Check materials are the same
	if(materials.size() != other.materials.size())
		return false;

	for(size_t i=0; i<materials.size(); ++i)
		if(!(*materials[i] == *other.materials[i]))
			return false;

	return
		id == other.id &&
		created_time == other.created_time &&
		creator_id == other.creator_id &&
		owner_id == other.owner_id &&
		model_url == other.model_url &&
	
		bone_name == other.bone_name &&

		translation == other.translation &&
		axis == other.axis &&
		angle == other.angle &&
		scale == other.scale &&

		flags == other.flags &&
		max_supply == other.max_supply &&

		name == other.name &&
		description == other.description &&

		preview_image_URL == other.preview_image_URL;
}


void GearItem::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(GEAR_ITEM_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	stream.writeUInt64(id);
	::writeToStream(creator_id, stream);
	::writeToStream(owner_id, stream);
	created_time.writeToStream(stream);

	stream.writeStringLengthFirst(model_url);

	// Write materials
	stream.writeUInt32((uint32)materials.size());
	for(size_t i=0; i<materials.size(); ++i)
		::writeWorldMaterialToStream(*materials[i], stream);

	stream.writeStringLengthFirst(bone_name);

	::writeToStream(translation, stream);
	::writeToStream(axis, stream);
	stream.writeFloat(angle);
	::writeToStream(scale, stream);

	stream.writeUInt32(flags);
	stream.writeUInt32(max_supply);

	stream.writeStringLengthFirst(name);
	stream.writeStringLengthFirst(description);

	stream.writeStringLengthFirst(preview_image_URL);

	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readGearItemFromStream(RandomAccessInStream& stream, GearItem& item)
{
	const size_t initial_read_index = stream.getReadIndex();

	/*const uint32 version =*/ stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readGearItemFromNetworkStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readGearItemFromNetworkStream: buffer_size was too large");

	item.id = stream.readUInt64();
	item.creator_id = readUserIDFromStream(stream);
	item.owner_id = readUserIDFromStream(stream);
	item.created_time.readFromStream(stream);

	item.model_url = stream.readStringLengthFirst(GearItem::MAX_MODEL_URL_SIZE);

	// Read materials
	const size_t num_mats = stream.readUInt32();
	if(num_mats > GearItem::MAX_NUM_MATERIALS)
		throw glare::Exception("Too many materials: " + toString(num_mats));
	item.materials.resize(num_mats);
	for(size_t i=0; i<item.materials.size(); ++i)
	{
		if(item.materials[i].isNull())
			item.materials[i] = new WorldMaterial();
		::readWorldMaterialFromStream(stream, *item.materials[i]);
	}

	item.bone_name = stream.readStringLengthFirst(GearItem::MAX_BONE_NAME_SIZE);

	item.translation = readVec3FromStream<float>(stream);
	item.axis = readVec3FromStream<float>(stream);
	item.angle = stream.readFloat();
	item.scale = readVec3FromStream<float>(stream);

	item.flags = stream.readUInt32();
	item.max_supply = stream.readUInt32();

	item.name = stream.readStringLengthFirst(GearItem::MAX_NAME_SIZE);
	item.description = stream.readStringLengthFirst(GearItem::MAX_DESCRIPTION_SIZE);

	item.preview_image_URL = stream.readStringLengthFirst(GearItem::MAX_PREVIEW_URL_SIZE);

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
