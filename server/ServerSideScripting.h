/*=====================================================================
ServerSideScripting.h
---------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/WorldObject.h"


namespace ServerSideScripting
{


class ServerSideScript : public ThreadSafeRefCounted
{
public:
	virtual ~ServerSideScript() {}

	std::string base_image_URL;
	size_t material_index;
	std::string material_texture; // One of "colour", "emission".  Default is "colour"
};


Reference<ServerSideScript> parseXMLScript(const std::string& script);


} // end namespace ServerSideScripting
