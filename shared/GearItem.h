/*=====================================================================
GearItem.h
----------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "UserID.h"
#include "WorldMaterial.h"
#include <utils/TimeStamp.h>
#include <utils/DatabaseKey.h>
#include <maths/vec3.h>
#include <string>
class RandomAccessInStream;
class RandomAccessOutStream;


/*=====================================================================
GearItem
--------
Corresponds to a NFT in the Substrata gear contract with number = id.
=====================================================================*/
class GearItem : public ThreadSafeRefCounted
{
public:
	GearItem();

	bool operator == (const GearItem& other) const;
	void writeToStream(RandomAccessOutStream& stream) const;

	static const size_t MAX_NAME_SIZE               = 1000;
	static const size_t MAX_MODEL_URL_SIZE          = 512;
	static const size_t MAX_BONE_NAME_SIZE          = 100;
	static const size_t MAX_DESCRIPTION_SIZE        = 10000;
	static const size_t MAX_NUM_MATERIALS           = 128;
	static const size_t MAX_PREVIEW_URL_SIZE        = 512;



	UID id;

	// NOTE: if changing these fields, update operator ==

	TimeStamp created_time;
	UserID creator_id; // ID of Substrata user who minted/created the gear item.
	UserID owner_id; // Current owner: either the creator, or some other user whose inventory it's in.

	URLString model_url;
	std::vector<WorldMaterialRef> materials;
	
	std::string bone_name; // Name of bone attached to.  Should be an element of RPM_bone_names from glare-core\graphics\AnimationData.cpp. (from the Mixamo rig, but with the "mixamorig:" prefix removed)

	// Transform from bone they are attached to
	Vec3f translation;
	Vec3f axis;
	float angle;
	Vec3f scale;

	uint32 flags;
	uint32 max_supply;

	std::string name;
	std::string description;

	std::string preview_image_URL;

	DatabaseKey database_key;
};


typedef Reference<GearItem> GearItemRef;



void readGearItemFromStream(RandomAccessInStream& stream, GearItem& item);



// A list of gear items.
struct GearItems
{
	bool operator == (const GearItems& other) const;
	void writeToStream(RandomAccessOutStream& stream) const;

	std::vector<GearItemRef> items;
};

void readGearItemsFromStream(RandomAccessInStream& stream, GearItems& settings);
