/*=====================================================================
Avatar.h
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#pragma once


#include "WorldMaterial.h"
#if GUI_CLIENT
#include "../gui_client/AvatarGraphics.h"
#endif
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include "../shared/UID.h"
#include "graphics/colour3.h"
#include "vec3.h"
#include "Matrix4f.h"
#include <string>
#include <vector>
#include <set>
struct GLObject;
class AvatarGraphics;
struct MeshData;
class WorldObject;
class VehiclePhysics;



#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif


struct AvatarSettings
{
	std::string model_url;
	std::vector<WorldMaterialRef> materials;
	Matrix4f pre_ob_to_world_matrix; // For y-up to z-up transformation, and translating so feet are on ground etc..

	void copyNetworkStateFrom(const AvatarSettings& other);

	bool operator == (const AvatarSettings& other) const;
};


/*=====================================================================
Avatar
-------------------

=====================================================================*/
class Avatar : public ThreadSafeRefCounted
{
public:
	Avatar();
	~Avatar();

	GLARE_ALIGNED_16_NEW_DELETE

	int getLODLevel(const Vec3d& campos) const;
	float getMaxDistForLODLevel(int level) const;

	std::string getLODModelURLForLevel(const std::string& base_model_url, int level);
	int getModelLODLevelForObLODLevel(int ob_lod_level) const; // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.

	void appendDependencyURLs(int ob_lod_level, std::vector<DependencyURL>& URLs_out);
	void appendDependencyURLsForAllLODLevels(std::vector<DependencyURL>& URLs_out);
	void getDependencyURLSet(int ob_lod_level, std::set<DependencyURL>& URLS_out);
	void getDependencyURLSetForAllLODLevels(std::set<DependencyURL>& URLS_out);
	void convertLocalPathsToURLS(ResourceManager& resource_manager);


	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& rotation_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& rotation);

	void generatePseudoRandomNameColour();

	void copyNetworkStateFrom(const Avatar& other);

	

	UID uid;
	std::string name;
	AvatarSettings avatar_settings;
	Vec3d pos;
	Vec3f rotation; // (roll, pitch, heading)
	uint32 anim_state; // See AvatarGraphics::ANIM_STATE_IN_AIR flag etc..

	UID selected_object_uid; // Will be set to invalidUID if no object selected.

	enum State
	{
		State_JustCreated,
		State_Alive,
		State_Dead
	};

	State state;
	bool transform_dirty;
	bool other_dirty;


	//Reference<GLObject> opengl_engine_ob;
#if GUI_CLIENT

	Colour3f name_colour;

	Reference<GLObject> opengl_engine_nametag_ob;
	float nametag_z_offset; // To adjust nametag up when animation requires.  Smoothed over time value.

	AvatarGraphics graphics;

	Reference<MeshData> mesh_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

	Reference<WorldObject> entered_vehicle; // Reference to vehicle that the avatar has entered (e.g. is driving or a passenger)
	uint32 vehicle_seat_index;

	Reference<VehiclePhysics> vehicle_physics;

#endif

	/*
		Snapshots for client-side interpolation purposes.
		next_i = index to write next snapshot in.
		pos_snapshots[next_i - 1] is the last received update, received at time last_snapshot_time.
		pos_snapshots[next_i - 2] is the update received before that, will be considerd to be received at last_snapshot_time - update_send_period.
	*/
	static const int HISTORY_BUF_SIZE = 4;
	Vec3d pos_snapshots[HISTORY_BUF_SIZE];
	Vec3f rotation_snapshots[HISTORY_BUF_SIZE];
	double snapshot_times[HISTORY_BUF_SIZE]; // Time as measured by Clock::getTimeSinceInit().
	//double last_snapshot_time;
	uint32 next_snapshot_i;
private:

};


#ifdef _WIN32
#pragma warning(pop)
#endif


typedef Reference<Avatar> AvatarRef;


const Matrix4f obToWorldMatrix(const Avatar& ob);


void writeToStream(const AvatarSettings& settings, OutStream& stream);
void readFromStream(InStream& stream, AvatarSettings& settings);


void writeToNetworkStream(const Avatar& world_ob, OutStream& stream); // Write without version.  Writes UID.
void readFromNetworkStreamGivenUID(InStream& stream, Avatar& ob); // UID will have been read already
