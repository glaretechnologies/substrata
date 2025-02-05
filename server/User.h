/*=====================================================================
User.h
------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include "PasswordReset.h"
#include <TimeStamp.h>
#include "../shared/UserID.h"
#include "../shared/WorldMaterial.h"
#include "../shared/Avatar.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <string>
#include <OutStream.h>
#include <InStream.h>
#include <DatabaseKey.h>


class RandomAccessInStream;


struct EmailSendingInfo
{
	std::string smtp_servername;
	std::string smtp_username;
	std::string smtp_password;
	std::string from_name;
	std::string from_email_addr;
	std::string reset_webserver_hostname;
};


/*=====================================================================
User
----
User account
=====================================================================*/
class User : public ThreadSafeRefCounted
{
public:
	User();
	~User();


	// Return digest
	static std::string computePasswordHash(const std::string& password, const std::string& salt);

	// Does the password, when hashed, match the password disgest we have stored?
	bool isPasswordValid(const std::string& password) const;

	// Adds reset token to list of reset tokens for user.

	void sendPasswordResetEmail(const EmailSendingInfo& sending_info); // throws glare::Exception on error

	bool isResetTokenHashValidForUser(const std::array<uint8, 32>& reset_token_hash) const;
	bool resetPasswordWithTokenHash(const std::array<uint8, 32>& reset_token_hash, const std::string& new_password);

	void setNewPasswordAndSalt(const std::string& new_password);

	UserID id;

	TimeStamp created_time;

	std::string name;
	std::string email_address;

	std::string hashed_password; // SHA-256 hash, so 256/8 = 32 bytes
	std::string password_hash_salt; // Base-64 encoded 256 random bits.

	std::string current_eth_signing_nonce; // Doesn't need to be serialised, should be generated and used relatively quickly.
	std::string controlled_eth_address; // Eth address that user controls, in hex encoding with 0x prefix.  Empty if no such address.

	std::vector<PasswordReset> password_resets; // pending password reset tokens

	AvatarSettings avatar_settings;

	static const uint32 WORLD_GARDENER_FLAG           = 1; // Can this user add objects outside of parcels
	static const uint32 ALLOW_DYN_TEX_UPDATE_CHECKING = 2; // Will the user's dynamic_texture_update scripts be run by the server?

	uint32 flags;

	DatabaseKey database_key;
};


typedef Reference<User> UserRef;


void writeUserToStream(const User& user, RandomAccessOutStream& stream);
void readUserFromStream(RandomAccessInStream& stream, User& userob);


struct UserRefHash
{
	size_t operator() (const UserRef& ob) const
	{
		return (size_t)ob.ptr() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};
