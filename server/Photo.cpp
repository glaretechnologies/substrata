/*=====================================================================
Photo.cpp
---------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "Photo.h"


#include <utils/RandomAccessInStream.h>
#include <utils/RandomAccessOutStream.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>


// Emoji are written as UTF-8 byte escapes so the source file encoding doesn't matter.
const char* PhotoReaction::emojiString(EmojiType emoji_type)
{
	switch(emoji_type)
	{
	case EmojiType_ThumbsUp: return "\xF0\x9F\x91\x8D";
	case EmojiType_Heart:    return "\xE2\x9D\xA4\xEF\xB8\x8F";
	case EmojiType_Laughing: return "\xF0\x9F\x98\x82";
	case EmojiType_Fire:     return "\xF0\x9F\x94\xA5";
	case EmojiType_Wow:      return "\xF0\x9F\x98\xAE";
	case EmojiType_Clap:     return "\xF0\x9F\x91\x8F";
	case EmojiType_Hundred:  return "\xF0\x9F\x92\xAF";
	default:                 return "?";
	}
}


const char* PhotoReaction::emojiName(EmojiType emoji_type)
{
	switch(emoji_type)
	{
	case EmojiType_ThumbsUp: return "Thumbs up";
	case EmojiType_Heart:    return "Heart";
	case EmojiType_Laughing: return "Laughing";
	case EmojiType_Fire:     return "Fire";
	case EmojiType_Wow:      return "Wow";
	case EmojiType_Clap:     return "Clap";
	case EmojiType_Hundred:  return "100";
	default:                 return "Unknown";
	}
}


Photo::Photo()
{
	flags = 0;
	type = Type_Image;
	state = State_published;
}


Photo::~Photo()
{}


bool Photo::toggleReaction(PhotoReaction::EmojiType emoji_type, UserID user_id)
{
	for(size_t i=0; i<reactions.size(); ++i)
		if(reactions[i].user_id == user_id && reactions[i].emoji_type == emoji_type)
		{
			reactions.erase(reactions.begin() + i);
			return false;
		}

	PhotoReaction reaction;
	reaction.emoji_type = emoji_type;
	reaction.user_id = user_id;
	if(reactions.size() < MAX_NUM_REACTIONS)
		reactions.push_back(reaction);
	return true;
}


bool Photo::hasReaction(PhotoReaction::EmojiType emoji_type, UserID user_id) const
{
	for(size_t i=0; i<reactions.size(); ++i)
		if(reactions[i].user_id == user_id && reactions[i].emoji_type == emoji_type)
			return true;
	return false;
}


size_t Photo::numReactions(PhotoReaction::EmojiType emoji_type) const
{
	size_t count = 0;
	for(size_t i=0; i<reactions.size(); ++i)
		if(reactions[i].emoji_type == emoji_type)
			count++;
	return count;
}


static const uint32 PHOTO_SERIALISATION_VERSION = 4;
// v2: added state
// v3: added reactions
// v4: added type, local_video_filename


void Photo::writeToStream(RandomAccessOutStream& stream) const
{
	// Write to stream with a length prefix.  Do this by writing to the stream, them going back and writing the length of the data we wrote.
	// Writing a length prefix allows for adding more fields later, while retaining backwards compatibility with older code that can just skip over the new fields.

	const size_t initial_write_index = stream.getWriteIndex();

	stream.writeUInt32(PHOTO_SERIALISATION_VERSION);
	stream.writeUInt32(0); // Size of buffer will be written here later

	
	stream.writeUInt64(id);
	::writeToStream(creator_id, stream);
	::writeToStream(parcel_id, stream);
	created_time.writeToStream(stream);
	stream.writeData(&cam_pos, sizeof(cam_pos));
	stream.writeData(&cam_angles, sizeof(cam_angles));
	runtimeCheck(caption.size() <= 10000);
	stream.writeStringLengthFirst(caption);
	stream.writeUInt32(flags);

	runtimeCheck(world_name.size() <= 10000);
	stream.writeStringLengthFirst(world_name);
	
	runtimeCheck(local_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_filename);
	
	runtimeCheck(local_thumbnail_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_thumbnail_filename);

	runtimeCheck(local_midsize_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_midsize_filename);

	stream.writeUInt32((uint32)state);

	runtimeCheck(reactions.size() <= MAX_NUM_REACTIONS);
	stream.writeUInt32((uint32)reactions.size());
	for(size_t i=0; i<reactions.size(); ++i)
	{
		stream.writeUInt32((uint32)reactions[i].emoji_type);
		::writeToStream(reactions[i].user_id, stream);
	}

	stream.writeUInt32((uint32)type);
	runtimeCheck(local_video_filename.size() <= 10000);
	stream.writeStringLengthFirst(local_video_filename);


	// Go back and write size of buffer to buffer size field
	const uint32 buffer_size = (uint32)(stream.getWriteIndex() - initial_write_index);

	std::memcpy(stream.getWritePtrAtIndex(initial_write_index + sizeof(uint32)), &buffer_size, sizeof(uint32));
}


void readPhotoFromStream(RandomAccessInStream& stream, Photo& photo)
{
	const size_t initial_read_index = stream.getReadIndex();

	const uint32 version = stream.readUInt32();
	const size_t buffer_size = stream.readUInt32();

	checkProperty(buffer_size >= 8ul, "readPhotoFromStream: buffer_size was too small");
	checkProperty(buffer_size <= 1000000ul, "readPhotoFromStream: buffer_size was too large");

	photo.id = stream.readUInt64();
	photo.creator_id = ::readUserIDFromStream(stream);
	photo.parcel_id = ::readParcelIDFromStream(stream);
	photo.created_time.readFromStream(stream);
	stream.readData(&photo.cam_pos, sizeof(photo.cam_pos));
	stream.readData(&photo.cam_angles, sizeof(photo.cam_angles));
	photo.caption = stream.readStringLengthFirst(10000);
	photo.flags = stream.readUInt32();
	photo.world_name = stream.readStringLengthFirst(10000);

	photo.local_filename = stream.readStringLengthFirst(10000);
	photo.local_thumbnail_filename = stream.readStringLengthFirst(10000);
	photo.local_midsize_filename = stream.readStringLengthFirst(10000);

	if(version >= 2)
	{
		const uint32 s = stream.readUInt32();
		if(s > Photo::State_deleted)
			throw glare::Exception("Invalid state");
		photo.state = (Photo::State)s;
	}

	photo.reactions.clear();
	if(version >= 3)
	{
		const uint32 num_reactions = stream.readUInt32();
		checkProperty(num_reactions <= Photo::MAX_NUM_REACTIONS, "readPhotoFromStream: too many reactions");
		photo.reactions.reserve(num_reactions);
		for(uint32 i=0; i<num_reactions; ++i)
		{
			const uint32 emoji_type = stream.readUInt32();
			const UserID user_id = ::readUserIDFromStream(stream);
			if(emoji_type < PhotoReaction::EmojiType_NumTypes) // Skip emoji types this code doesn't know about.
			{
				PhotoReaction reaction;
				reaction.emoji_type = (PhotoReaction::EmojiType)emoji_type;
				reaction.user_id = user_id;
				photo.reactions.push_back(reaction);
			}
		}
	}

	photo.type = Photo::Type_Image;
	photo.local_video_filename.clear();
	if(version >= 4)
	{
		const uint32 t = stream.readUInt32();
		if(t > Photo::Type_Video)
			throw glare::Exception("Invalid photo type");
		photo.type = (Photo::Type)t;
		photo.local_video_filename = stream.readStringLengthFirst(10000);
	}

	// Discard any remaining unread data
	const size_t read_B = stream.getReadIndex() - initial_read_index; // Number of bytes we have read so far
	if(read_B < buffer_size)
		stream.advanceReadIndex(buffer_size - read_B);
}
