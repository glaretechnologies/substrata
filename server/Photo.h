/*=====================================================================
Photo.h
-------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "../shared/UserID.h"
#include "../shared/ParcelID.h"
#include "../maths/vec3.h"
#include <utils/TimeStamp.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/DatabaseKey.h>
#include <vector>
class RandomAccessInStream;
class RandomAccessOutStream;


/*=====================================================================
PhotoReaction
-------------
An emoji reaction that a user gave a photo on the photo web page.
=====================================================================*/
struct PhotoReaction
{
	// Values are serialised, so only append new emoji types.
	enum EmojiType
	{
		EmojiType_ThumbsUp = 0,
		EmojiType_Heart = 1,
		EmojiType_Laughing = 2,
		EmojiType_Fire = 3,
		EmojiType_Wow = 4,
		EmojiType_Clap = 5,
		EmojiType_Hundred = 6,
		EmojiType_NumTypes = 7
	};

	static const char* emojiString(EmojiType emoji_type); // Returns UTF-8 encoded emoji
	static const char* emojiName(EmojiType emoji_type); // Returns e.g. "Thumbs up"

	EmojiType emoji_type;
	UserID user_id;
};


/*=====================================================================
Photo
-----
A photo / screenshot that a user captured in the substrata client photo mode, 
and then uploaded to the server.

See WorkerThreadUploadPhotoHandling.cpp/.h for more code.
=====================================================================*/
class Photo : public ThreadSafeRefCounted
{
public:
	Photo();
	~Photo();

	void writeToStream(RandomAccessOutStream& stream) const;

	std::string fullSizeImageURLPath()       const { return "/photo_image/"         + toString(id); }
	std::string midSizeImageURLPath()        const { return "/photo_midsize_image/" + toString(id); }
	std::string thumbnailSizeImageURLPath()  const { return "/photo_thumb_image/"   + toString(id); }
	std::string videoURLPath()               const { return "/photo_video/"         + toString(id); }

	static const size_t MAX_CAPTION_SIZE = 10000;
	static const size_t MAX_WORLD_NAME_SIZE = 1000;
	static const size_t MAX_NUM_REACTIONS = 100000;

	bool toggleReaction(PhotoReaction::EmojiType emoji_type, UserID user_id); // Adds the reaction if the user hasn't given it, otherwise removes it.  Returns true if the user now has the reaction.
	bool hasReaction(PhotoReaction::EmojiType emoji_type, UserID user_id) const;
	size_t numReactions(PhotoReaction::EmojiType emoji_type) const;

	uint64 id;
	UserID creator_id;
	ParcelID parcel_id;
	TimeStamp created_time;
	Vec3d cam_pos;
	Vec3d cam_angles;
	std::string caption;
	uint32 flags;
	std::string world_name;

	std::string local_filename; // file name on disk of full resolution saved photo
	std::string local_thumbnail_filename; // file name on disk of thumbnail (200 px wide)
	std::string local_midsize_filename; // file name on disk of mid-size image (max 1000px wide)

	// A Photo can be a video.  In that case the image files above are made from a poster frame uploaded with the video.
	enum Type
	{
		Type_Image = 0,
		Type_Video = 1
	};
	Type type;
	std::string local_video_filename; // file name on disk of the MP4 video, if type is Type_Video.

	bool isVideo() const { return type == Type_Video; }

	enum State
	{
		State_published = 0,
		State_deleted = 1
	};
	State state;

	std::vector<PhotoReaction> reactions;

	DatabaseKey database_key;
};


typedef Reference<Photo> PhotoRef;


void readPhotoFromStream(RandomAccessInStream& stream, Photo& s);


struct PhotoRefHash
{
	size_t operator() (const PhotoRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
