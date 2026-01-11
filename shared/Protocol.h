/*=====================================================================
Protocol.h
----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "utils/Platform.h"

/*
CyberspaceProtocolVersion
20: Added lightmap_url to WorldObject.
21: Added parcel auction stuff
22: Added grid object querying stuff
23: Added ObjectModelURLChanged
24: Added material emission_lum_flux
25: Added AvatarIsHere message, Added anim_state to avatar
26: Added aabb_ws and max_lod_level to WorldObject
27: Added flags to WorldMaterial
28: Added pre_ob_to_world_matrix, materials to Avatar
29: Added AvatarPerformGesture, AvatarStopGesture
30: Added audio_source_url to WorldObject
31: Added length prefixing to messages.
32: Added flags field to Parcel
33: Added spawn_point field to Parcel
34: Added AABB to ObjectTransformUpdate
35: Added emission_rgb and emission_texture_url to WorldMaterial
	Added sending user flags in LoggedInMessage
36: Sending position at start of QueryObjects and QueryObjectsInAABB messages.
37: Not sending aabb_ws with various messages, sending aabb_os in WorldObject.
	Added last_modified_time to WorldObject.
	Added scale to ObjectTransformUpdate message.
38: Use length-prefixed serialisation for WorldMaterial, sending server version to client.
39: Added QueryMapTiles, MapTilesResult
40: Added QueryLODChunksMessage, LODChunkInitialSend, LODChunkUpdatedMessage
41: Added server capabilities uint sent back in initial handshake
42: Added ParcelInitialSendCompressed, client_capabilities
43: Added sending mesh optimisation version to client
44: Added ChangeToDifferentWorld
45: Added Parcel title
*/
namespace Protocol
{

const uint32 CyberspaceHello = 1357924680;

const uint32 CyberspaceProtocolVersion = 45;

const uint32 ClientProtocolOK		= 10000;
const uint32 ClientProtocolTooOld	= 10001;
const uint32 ClientProtocolTooNew	= 10002;
const uint32 CyberspaceGoodbye		= 10010;
const uint32 ClientUDPSocketOpen	= 10003;

const uint32 AudioStreamToServerStarted			= 10020;
const uint32 AudioStreamToServerEnded			= 10021;

const uint32 ConnectionTypeUpdates				= 500;
const uint32 ConnectionTypeUploadResource		= 501;
const uint32 ConnectionTypeDownloadResources	= 502;
//const uint32 ConnectionTypeWebsite				= 503; // A connection from the webserver.
const uint32 ConnectionTypeScreenshotBot		= 504; // A connection from the screenshot bot.
const uint32 ConnectionTypeEthBot				= 505; // A connection from the Ethereum bot.
const uint32 ConnectionTypeUploadPhoto			= 506;

const uint32 ChangeToDifferentWorld			= 600;


const uint32 AvatarCreated			= 1000;
const uint32 AvatarDestroyed		= 1001;
const uint32 AvatarTransformUpdate	= 1002;
const uint32 AvatarFullUpdate		= 1003;
const uint32 CreateAvatar			= 1004;
const uint32 AvatarIsHere			= 1005;
const uint32 AvatarPerformGesture	= 1010;
const uint32 AvatarStopGesture		= 1011;

const uint32 AvatarEnteredVehicle	= 1100;
const uint32 AvatarExitedVehicle	= 1101;

const uint32 ChatMessageID			= 2000;

const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;
const uint32 ObjectFullUpdate		= 3003;
const uint32 ObjectLightmapURLChanged		= 3010; // The object's lightmap URL changed.
const uint32 ObjectFlagsChanged		= 3011;
const uint32 ObjectModelURLChanged	= 3012;
const uint32 ObjectPhysicsOwnershipTaken	= 3013;
const uint32 ObjectPhysicsTransformUpdate	= 3016;
const uint32 ObjectContentChanged	= 3017;
const uint32 SummonObject			= 3030;

const uint32 CreateObject			= 3004; // Client wants to create an object.
const uint32 DestroyObject			= 3005; // Client wants to destroy an object.

const uint32 QueryObjects			= 3020; // Client wants to query objects in certain grid cells
const uint32 ObjectInitialSend		= 3021;
const uint32 QueryObjectsInAABB		= 3022; // Client wants to query objects in a particular AABB
const uint32 ObjectInitialSendCompressed		= 3023; // A message composed of compressed ObjectInitialSend messages.


const uint32 ParcelCreated			= 3100;
const uint32 ParcelDestroyed		= 3101;
const uint32 ParcelFullUpdate		= 3103;
const uint32 ParcelInitialSendCompressed		= 3104;

const uint32 QueryParcels			= 3150;
const uint32 ParcelList				= 3160;

const uint32 GetAllObjects			= 3600; // Client wants to get all objects from server
const uint32 AllObjectsSent			= 3601; // Server has sent all objects

const uint32 WorldSettingsInitialSendMessage	= 3700;
const uint32 WorldSettingsUpdate	= 3701;

const uint32 WorldDetailsInitialSendMessage	= 3750; // Server is sending ServerWorldState to client.

const uint32 QueryMapTiles			= 3800; // Client wants to query map tile image URLs
const uint32 MapTilesResult			= 3801; // Server is sending back a list of tile image URLs to the client.


const uint32 QueryLODChunksMessage		= 3900;
const uint32 LODChunkInitialSend		= 3901;
const uint32 LODChunkUpdatedMessage		= 3902;



//TEMP HACK move elsewhere
const uint32 GetFile				= 4000;
const uint32 GetFiles				= 4001; // Client wants to download multiple resources from the server.

const uint32 NewResourceOnServer	= 4100; // A file has been uploaded to the server


const uint32 UploadAllowed			= 5100;
const uint32 LogInFailure			= 5101;
const uint32 InvalidFileSize		= 5102;
const uint32 NoWritePermissions		= 5103;
const uint32 ServerIsInReadOnlyMode	= 5104;
const uint32 InvalidFileType		= 5105;


//TEMP HACK move elsewhere
const uint32 UserSelectedObject		= 6000;
const uint32 UserDeselectedObject	= 6001;

const uint32 UserUsedObjectMessage			= 6500;
const uint32 UserTouchedObjectMessage		= 6501;
const uint32 UserMovedNearToObjectMessage	= 6510;
const uint32 UserMovedAwayFromObjectMessage	= 6511;
const uint32 UserEnteredParcelMessage		= 6512;
const uint32 UserExitedParcelMessage		= 6513;

const uint32 InfoMessageID			= 7001;
const uint32 ErrorMessageID			= 7002;
const uint32 ServerAdminMessageID	= 7010; // Allows server to send a message like "Server going down for reboot soon"

const uint32 LogInMessage			= 8000;
const uint32 LogOutMessage			= 8001;
const uint32 SignUpMessage			= 8002;
const uint32 LoggedInMessageID		= 8003;
const uint32 LoggedOutMessageID		= 8004;
const uint32 SignedUpMessageID		= 8005;

const uint32 RequestPasswordReset	= 8010; // Client wants to reset the password for a given email address.  Obsolete, does nothing.  Use website instead.
const uint32 ChangePasswordWithResetToken = 8011; // Client is sending the password reset token, email address, and the new password.  Obsolete, does nothing.  Use website instead.

const uint32 TimeSyncMessage		= 9000; // Sends the current time

const uint32 ScreenShotRequest		= 11001;
const uint32 ScreenShotSucceeded	= 11002;
const uint32 TileScreenShotRequest	= 11003;


const uint32 SubmitEthTransactionRequest		= 12001;
const uint32 EthTransactionSubmitted			= 12002;
const uint32 EthTransactionSubmissionFailed		= 12003;

const uint32 KeepAlive				= 13000; // A message that doesn't do anything apart from provide a means for the client or server to check a connection is still working by making a socket call.

const uint32 PhotoUploadSucceeded	= 14000;
const uint32 PhotoUploadFailed		= 14001;




// Client capabilities
const uint32 STREAMING_COMPRESSED_OBJECT_SUPPORT	= 0x1; // Can the client handle ObjectInitialSendCompressed messages?

// Server capabilities
const uint32 OBJECT_TEXTURE_BASISU_SUPPORT			= 0x1;
const uint32 TERRAIN_DETAIL_MAPS_BASISU_SUPPORT		= 0x2;
const uint32 OPTIMISED_MESH_SUPPORT					= 0x4;

const int OPTIMISED_MESH_VERSION = 3;

} // end namespace Protocol
