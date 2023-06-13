/*=====================================================================
ServerSideScripting.cpp
-----------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ServerSideScripting.h"


#include <utils/Timer.h>
#include <utils/Lock.h>
#include <utils/IndigoXMLDoc.h>
#include <utils/Parser.h>
#include <utils/XMLParseUtils.h>


namespace ServerSideScripting
{


Reference<ServerSideScript> parseXMLScript(const std::string& script)
{
	try
	{
		IndigoXMLDoc doc(script.c_str(), script.size());

		pugi::xml_node root_elem = doc.getRootElement(); // Expected to be 'script'.  TODO: check?

		{
			pugi::xml_node dynamic_texture_update_elem = root_elem.child("dynamic_texture_update");
			if(dynamic_texture_update_elem)
			{
				const std::string base_url = XMLParseUtils::parseString(dynamic_texture_update_elem, "base_url");
				const uint32 material_index = XMLParseUtils::parseIntWithDefault(dynamic_texture_update_elem, "material_index", 0);
				const std::string material_texture = XMLParseUtils::parseStringWithDefault(dynamic_texture_update_elem, "material_texture", "colour");

				Reference<ServerSideScript> script_ob = new ServerSideScript();
				script_ob->base_image_URL = base_url;
				script_ob->material_index = material_index;
				script_ob->material_texture = material_texture;
				return script_ob;
			}
		}

		return Reference<ServerSideScript>();
	}
	catch(glare::Exception& e)
	{
		throw e;
	}
}


} // end namespace ServerSideScripting
