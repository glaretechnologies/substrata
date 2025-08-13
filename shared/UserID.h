/*=====================================================================
UserID.h
--------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#pragma once


#include <Platform.h>
#include <OutStream.h>
#include <InStream.h>
#include <StringUtils.h>
#include <Hasher.h>
#include <limits>
#include <string>


/*=====================================================================
UserID
------
Basically an index into user array on the server.
=====================================================================*/
class UserID
{
public:
	UserID() : v(invalidUserID().value()) {}
	explicit UserID(uint32 v_) : v(v_) {}

	static UserID invalidUserID() { return UserID(std::numeric_limits<uint32>::max()); }

	uint32 value() const { return v; }

	bool valid() const { return v != invalidUserID().value(); }

	const std::string toString() const { return ::toString(v); }

	bool operator == (const UserID& other) const { return v == other.v; }
	bool operator != (const UserID& other) const { return v != other.v; }

	bool operator < (const UserID& other) const { return v < other.v; }
	bool operator > (const UserID& other) const { return v > other.v; }
	bool operator >= (const UserID& other) const { return v >= other.v; }

//private:
	uint32 v;
};


// Does this UserID correspond to the admin user that has full control over the server?
static inline bool isGodUser(const UserID logged_in_user_id)
{
	return logged_in_user_id.value() == 0;
}



inline void writeToStream(const UserID& uid, OutStream& stream)
{
	stream.writeData(&uid.v, sizeof(uid.v));
}


[[nodiscard]] inline UserID readUserIDFromStream(InStream& stream)
{
	UserID uid;
	stream.readData(&uid.v, sizeof(uid.v));
	return uid;
}


struct UserIDHasher
{
	size_t operator() (const UserID& uid) const
	{
		return hashBytes((const uint8*)&uid, sizeof(UserID));
	}
};
