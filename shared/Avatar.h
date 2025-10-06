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
#include "../shared/UID.h"
#include <graphics/colour3.h>
#include <maths/vec3.h>
#include <maths/Matrix4f.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <vector>
#include <set>
struct GLObject;
class AvatarGraphics;
struct MeshData;
class GLUIImage;
class WorldObject;
class VehiclePhysics;
class RandomAccessInStream;
class RandomAccessOutStream;
namespace glare { class AudioSource; }


#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif


struct AvatarSettings
{
	URLString model_url;
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

	struct GetLODModelURLOptions
	{
		GetLODModelURLOptions(bool get_optimised_mesh_, int opt_mesh_version_) : get_optimised_mesh(get_optimised_mesh_), opt_mesh_version(opt_mesh_version_) {}
		bool get_optimised_mesh;
		int opt_mesh_version;
	};

	URLString getLODModelURLForLevel(const URLString& base_model_url, int level, const GetLODModelURLOptions& options) const;
	int getModelLODLevelForObLODLevel(int ob_lod_level) const; // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.

	struct GetDependencyOptions
	{
		GetDependencyOptions() : use_basis(true), get_optimised_mesh(false), opt_mesh_version(-1) {}
		bool use_basis;
		bool get_optimised_mesh;
		int opt_mesh_version;
	};
	void appendDependencyURLs(int ob_lod_level, const GetDependencyOptions& options, DependencyURLVector& URLs_out);
	void appendDependencyURLsForAllLODLevels(const GetDependencyOptions& options, DependencyURLVector& URLs_out);
	void appendDependencyURLsBaseLevel(const GetDependencyOptions& options, DependencyURLVector& URLs_out) const;
	
	void getDependencyURLSet(int ob_lod_level, const GetDependencyOptions& options, DependencyURLSet& URLS_out);
	void getDependencyURLSetForAllLODLevels(const GetDependencyOptions& options, DependencyURLSet& URLS_out);
	void getDependencyURLSetBaseLevel(const GetDependencyOptions& options, DependencyURLSet& URLS_out) const;

	void convertLocalPathsToURLS(ResourceManager& resource_manager);


	void getInterpolatedTransform(double cur_time, Vec3d& pos_out, Vec3f& rotation_out) const;
	void setTransformAndHistory(const Vec3d& pos, const Vec3f& rotation);

	void generatePseudoRandomNameColour();

	void copyNetworkStateFrom(const Avatar& other);

#if GUI_CLIENT
	bool isOurAvatar() const { return our_avatar; }
#endif

	static Colour3f defaultMat0Col() { return Colour3f(0.5f, 0.6f, 0.7f); }
	static Colour3f defaultMat1Col() { return Colour3f(0.8f); }
	static constexpr float default_mat0_metallic_frac = 0.5f;
	static constexpr float default_mat1_metallic_frac = 0.0f;
	static constexpr float default_mat0_roughness = 0.3f;


	UID uid;
	std::string name;
	AvatarSettings avatar_settings;
	Vec3d pos;
	Vec3f rotation; // (roll, pitch, heading)
	uint32 anim_state; // See AvatarGraphics::ANIM_STATE_IN_AIR flag etc..
	uint32 last_physics_input_bitflags;

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
	bool our_avatar;

	Colour3f name_colour;

	Reference<GLObject> nametag_gl_ob;
	float nametag_z_offset; // To adjust nametag up when animation requires.  Smoothed over time value.
	Reference<GLObject> speaker_gl_ob;

	AvatarGraphics graphics;
	Reference<GLUIImage> hud_marker;
	Reference<GLUIImage> hud_marker_arrow;
	Reference<GLUIImage> minimap_marker;
	Reference<GLUIImage> minimap_marker_arrow;

	Reference<MeshData> mesh_data; // Hang on to a reference to the mesh data, so when object-uses of it are removed, it can be removed from the MeshManager with meshDataBecameUnused().

	Reference<WorldObject> entered_vehicle; // Reference to vehicle object that the avatar has entered, or should enter (e.g. is driving or a passenger)
	uint32 vehicle_seat_index; // The index of the seat the avatar is sitting in, or will sit in, or was sitting in if the avatar has just exited the vehicle.
	enum PendingVehicleTransition
	{
		VehicleNoChange,
		EnterVehicle, // The avatar should enter the vehicle 'entered_vehicle' as soon as possible (i.e. when the physics object has been loaded)
		ExitVehicle // The avatar should exit the vehicle as soon as possible.
	};
	PendingVehicleTransition pending_vehicle_transition;


	bool use_materialise_effect_on_load; // When the opengl object is loaded, enable materialise effect on the materials.
	float materialise_effect_start_time;

	Reference<glare::AudioSource> audio_source; // audio source for voice chat
	uint32 audio_stream_sampling_rate; // NOTE: remote-user controlled data.
	uint32 audio_stream_id;

	bool underwater;
	double last_foam_decal_creation_time;
#endif
#if SERVER
	// Some state for Lua scripting
	UID vehicle_inside_uid;
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
};


#ifdef _WIN32
#pragma warning(pop)
#endif


typedef Reference<Avatar> AvatarRef;


const Matrix4f obToWorldMatrix(const Avatar& ob);


void writeAvatarSettingsToStream(const AvatarSettings& settings, RandomAccessOutStream& stream);
void readAvatarSettingsFromStream(RandomAccessInStream& stream, AvatarSettings& settings);


void writeAvatarToNetworkStream(const Avatar& world_ob, RandomAccessOutStream& stream); // Write without version.  Writes UID.
void readAvatarFromNetworkStreamGivenUID(RandomAccessInStream& stream, Avatar& ob); // UID will have been read already
