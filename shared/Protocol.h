/*=====================================================================
Protocol.h
----------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include "utils/Platform.h"


const uint32 CyberspaceHello = 1357924680;
const uint32 CyberspaceProtocolVersion = 8;
const uint32 ClientProtocolOK		= 10000;
const uint32 ClientProtocolTooOld	= 10001;
const uint32 ClientProtocolTooNew	= 10002;

const uint32 ConnectionTypeUpdates				= 500;
const uint32 ConnectionTypeUploadResource		= 501;
const uint32 ConnectionTypeDownloadResources	= 502;


const uint32 AvatarCreated			= 1000;
const uint32 AvatarDestroyed		= 1001;
const uint32 AvatarTransformUpdate	= 1002;
const uint32 AvatarFullUpdate		= 1003;
const uint32 CreateAvatar			= 1004;

const uint32 ChatMessageID			= 2000;

const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;
const uint32 ObjectFullUpdate		= 3003;

const uint32 CreateObject			= 3004; // Client wants to create an object.
const uint32 DestroyObject			= 3005; // Client wants to destroy an object.


											//TEMP HACK move elsewhere
const uint32 GetFile				= 4000;

const uint32 NewResourceOnServer	= 4100; // A file has been uploaded to the server



											//TEMP HACK move elsewhere
const uint32 UploadResource			= 5000;


//TEMP HACK move elsewhere
const uint32 UserSelectedObject		= 6000;
const uint32 UserDeselectedObject	= 6001;

const uint32 InfoMessageID			= 7001;
const uint32 ErrorMessageID			= 7002;

const uint32 LogInMessage			= 8000;
const uint32 LogOutMessage			= 8001;
const uint32 SignUpMessage			= 8002;
const uint32 LoggedInMessageID		= 8003;
const uint32 LoggedOutMessageID		= 8004;
const uint32 SignedUpMessageID		= 8005;

const uint32 RequestPasswordReset	= 8010; // Client wants to reset the password for a given email address.
const uint32 ChangePasswordWithResetToken = 8011; // Client is sending the password reset token, email address, and the new password.
