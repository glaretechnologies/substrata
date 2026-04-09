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
#include <maths/Matrix4f.h>
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

	void copyUserSettableFieldsFromOther(const GearItem& other);

	inline Matrix4f gearObToBoneSpaceMatrix() const;


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

	uint64 preview_image_screenshot_id;
	URLString preview_image_URL; // Resource URL of preview screenshot.  Denormalised data, should be the same as screenshot[preview_image_screenshot_id]->URL

	DatabaseKey database_key;
};


typedef Reference<GearItem> GearItemRef;


Matrix4f GearItem::gearObToBoneSpaceMatrix() const
{
	const Vec4f pos(translation.x, translation.y, translation.z, 1.f);

	// Don't use a zero scale component, because it makes the matrix uninvertible, which breaks various things, including picking and normals.
	// TODO: use branchless SSE for this.
	Vec3f use_scale = scale;
	if(std::fabs(use_scale.x) < 1.0e-6f) use_scale.x = 1.0e-6f;
	if(std::fabs(use_scale.y) < 1.0e-6f) use_scale.y = 1.0e-6f;
	if(std::fabs(use_scale.z) < 1.0e-6f) use_scale.z = 1.0e-6f;

	// Equivalent to
	//return Matrix4f::translationMatrix(pos + ob.translation) *
	//	Matrix4f::rotationMatrix(normalise(ob.axis.toVec4fVector()), ob.angle) *
	//	Matrix4f::scaleMatrix(use_scale.x, use_scale.y, use_scale.z));

	Matrix4f rot = Matrix4f::rotationMatrix(normalise(axis.toVec4fVector()), angle);
	rot.setColumn(0, rot.getColumn(0) * use_scale.x);
	rot.setColumn(1, rot.getColumn(1) * use_scale.y);
	rot.setColumn(2, rot.getColumn(2) * use_scale.z);
	rot.setColumn(3, pos);
	return rot;
}


struct GearItemRefHash
{
	size_t operator() (const GearItemRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};



void readGearItemFromStream(RandomAccessInStream& stream, GearItem& item);



// A list of gear items.
struct GearItems
{
	bool operator == (const GearItems& other) const;
	void writeToStream(RandomAccessOutStream& stream) const;
	void removeItem(const GearItemRef& item); // Removes the first item whose id matches item->id.

	std::vector<GearItemRef> items;
};

void readGearItemsFromStream(RandomAccessInStream& stream, GearItems& settings);


