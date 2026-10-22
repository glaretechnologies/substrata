/*=====================================================================
VoxelsParcelImporter.h
----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include <networking/HTTPClient.h> // Needed for Reference<HTTPClient>, since VoxelsParcelImportStopper's destructor is inline.
#include <maths/vec3.h>
#include <utils/Platform.h>
#include <utils/MessageableThread.h>
#include <utils/ThreadSafeQueue.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <utils/Mutex.h>
#include <atomic>
#include <string>
#include <vector>
class GUIClient;


// Lets another thread stop an import, including interrupting a download in progress, which could otherwise take a long time against a
// throttled host.
class VoxelsParcelImportStopper : public ThreadSafeRefCounted
{
public:
	VoxelsParcelImportStopper() : should_stop(false) {}

	void stop(); // Can be called from any thread.
	bool shouldStop() const { return should_stop; }

	// The client the import is downloading with, if any, so stop() can interrupt it.
	void setCurrentHTTPClient(const Reference<HTTPClient>& client);

private:
	std::atomic<bool> should_stop;
	Mutex mutex;
	Reference<HTTPClient> current_http_client  GUARDED_BY(mutex);
};


struct VoxelsParcelImportOptions
{
	VoxelsParcelImportOptions() : origin(0,0,0), voxels_x(0), voxels_y(0), voxels_z(0), replace_previous_import(false), stopper(NULL), log_queue(NULL) {}

	// Where the parcel goes: the Substrata position of the parcel's horizontal centre, at ground level.  The parcel is placed one voxel lower than
	// this, as Voxels builds usually have their bottom layer of voxels below the ground.
	Vec3d origin;

	// Dimensions of the parcel's voxel grid, in voxels (2 per metre).  0 to take them from the parcel bounds in the JSON, or, for a
	// snapshot, which has no bounds, to work them out from the voxel data.
	int voxels_x, voxels_y, voxels_z;

	// Whether the objects from an earlier import of the same parcel may be deleted.  If false and there are any, the import fails before
	// changing anything.
	bool replace_previous_import;

	// If non-empty, objects from an earlier import are saved to an XML file in this directory before being deleted.
	std::string backup_dir;

	// If non-null, can be used to stop the import.
	VoxelsParcelImportStopper* stopper;

	// If non-null, progress lines are written here as well as to the log window, for VoxelsParcelImportDialog.
	ThreadSafeQueue<std::string>* log_queue;

	// The server and world the parcel is being imported into, for the Substrata URLs that Voxels teleport links are converted to.
	std::string server_hostname;
	std::string server_worldname;
};


struct VoxelsParcelImportResults
{
	VoxelsParcelImportResults() : voxels_x(0), voxels_y(0), voxels_z(0), dims_were_inferred(false), num_objects_sent(0), num_objects_deleted(0),
		num_assets_downloaded(0), num_creates_answered(0), num_creates_refused(0), elapsed_s(0) {}

	std::string group_name;
	int voxels_x, voxels_y, voxels_z;
	bool dims_were_inferred;

	size_t num_objects_sent; // Including the group object if one was made.
	size_t num_objects_deleted; // Objects from an earlier import of the same parcel, removed first.
	std::string backup_path; // Where those objects were saved, if any were.
	size_t num_assets_downloaded;

	std::vector<std::string> feature_type_counts; // e.g. "cube: 375 imported", "sign: 68 skipped (not supported)"
	std::vector<std::string> skipped_assets; // An entry per asset URL that wasn't used, with the reason.

	size_t num_creates_answered;
	size_t num_creates_refused;
	std::string first_refusal_msg;

	double elapsed_s;

	std::string summary() const;
};


/*=====================================================================
VoxelsParcelImporter
--------------------
Imports a single Voxels (formerly CryptoVoxels) parcel build into the
connected world, from the parcel JSON: either a /api/parcels/N.json
response or a snapshot export.

Makes a VoxelGroup for the parcel voxels, and objects for the cube, image,
nft-image, vox-model, megavox, video, youtube, lantern, sign and richtext
features, all in one object group named
after the parcel.  Importing the same parcel again replaces what the last
import made.

An nft-image's picture is found from OpenSea's metadata for the token, and
the object links to the token's OpenSea page.

Assets are fetched from the URLs stored in the features and put in the
resource system like any locally added file.  Assets hosted by Voxels
itself (ugc:// URLs, voxels.com and cryptovoxels.com) are not fetched, and
neither is the parcel's tileset, so voxels get palette colours only.

Blocks until done, so call it from a thread other than the main thread.
=====================================================================*/
class VoxelsParcelImporter
{
public:
	// Throws glare::Exception on failures that stop the import.  Problems with individual features are listed in the results instead.
	static void importParcel(GUIClient* gui_client, const std::string& json_path, const VoxelsParcelImportOptions& options,
		VoxelsParcelImportResults& results_out);

	// The name of the object group an import of the parcel file goes in.  Throws glare::Exception if the file can't be read.
	static std::string getGroupNameForParcelFile(const std::string& json_path);

	// Finds this user's group with the given name in the world state, and the objects in it, including those in groups within it.
	// group_uid_out is invalid if there's no such group.  Locks the world state mutex.
	static void findPreviousImport(GUIClient* gui_client, const std::string& group_name, UID& group_uid_out, std::vector<UID>& member_uids_out);

	// Picks the grid dimensions, x outermost and z innermost, under which neighbouring voxels most often match.  Each must be even
	// (whole metres) and at least 8 voxels.  Throws glare::Exception if no dimensions fit num_voxels.
	static void inferVoxelDims(const uint16* voxels, size_t num_voxels, int& x_out, int& y_out, int& z_out);

	static void test();
};


/*=====================================================================
VoxelsParcelImportThread
------------------------
Runs a VoxelsParcelImporter import off the main thread, for the
Edit > Import Voxels Parcel menu action.  Reports the summary to the log
and a notification through GUIClient's message queue, and to
options.log_queue if set, followed by finishedMarker() once done, whether
the import succeeded, failed or was stopped.
=====================================================================*/
class VoxelsParcelImportThread : public MessageableThread
{
public:
	VoxelsParcelImportThread(GUIClient* gui_client_, const std::string& json_path_, const VoxelsParcelImportOptions& options_)
	:	gui_client(gui_client_), json_path(json_path_), options(options_), stopper(new VoxelsParcelImportStopper()) {}

	virtual void doRun() override;
	virtual void kill() override { stopper->stop(); }

	// Written to options.log_queue when the thread has finished.  Not something a log line can be.
	static const std::string finishedMarker() { return std::string("\x01" "finished"); }

private:
	GUIClient* gui_client;
	std::string json_path;
	VoxelsParcelImportOptions options;
	Reference<VoxelsParcelImportStopper> stopper;
};
