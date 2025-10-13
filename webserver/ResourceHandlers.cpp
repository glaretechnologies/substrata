/*=====================================================================
ResourceHandlers.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ResourceHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "../server/ServerWorldState.h"
#include "../server/Order.h"
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/BatchedMesh.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <MemMappedFile.h>
#include <FileUtils.h>
#include <RuntimeCheck.h>


namespace ResourceHandlers
{


void handleResourceRequest(ServerAllWorldsState& world_state, const web::RequestInfo& request, web::ReplyInfo& reply_info)
{
	try
	{
		const URLString resource_URL = toURLString(web::Escaping::URLUnescape(::eatPrefix(request.path, "/resource/")));

		// TEMP: print out request and headers
		//conPrint("handleResourceRequest: resource_URL: " + resource_URL);
		//for(size_t i=0; i<request.headers.size(); ++i)
		//	conPrint("\theader: " + request.headers[i].key + " : " + request.headers[i].value);
		

		// Lookup resource manager to see if we have a resource for this URL.  If so, set local_path to the local path of the resource.
		std::string local_path;
		{
			Lock lock(world_state.resource_manager->getMutex());

			auto res = world_state.resource_manager->getResourcesForURL().find(resource_URL);
			if(res != world_state.resource_manager->getResourcesForURL().end())
			{
				ResourceRef resource = res->second;
				if(resource->getState() == Resource::State_Present)
				{
					local_path = world_state.resource_manager->getLocalAbsPathForResource(*resource);
				}
			}
		} // End lock scope

		// TEMP: always rebuild gltf
#if 0
		if(/*local_path.empty() && */hasExtension(resource_URL, "glb")) // If the resource was not found, and a glb was requested:
		{
			const std::string URL_without_glb_ext = ::removeDotAndExtension(resource_URL); // Remove ".glb" suffix

			if(hasExtension(URL_without_glb_ext, "bmesh")) // If we have a xx.bmesh.glb request: (e.g. from web client)
			{
				// See if the bmesh file is present
				std::string local_bmesh_path;
				{
					Lock lock(world_state.resource_manager->getMutex());

					auto res = world_state.resource_manager->getResourcesForURL().find(URL_without_glb_ext);
					if(res != world_state.resource_manager->getResourcesForURL().end())
					{
						ResourceRef resource = res->second;
						if(resource->getState() == Resource::State_Present)
							local_bmesh_path = resource->getLocalPath();
					}
				}

				// Convert the bmesh file to GLB, if the bmesh file is present
				if(!local_bmesh_path.empty()) // If the bmesh file is present:
				{
					std::string local_GLB_path = local_bmesh_path + ".glb";
					try
					{
						conPrint("Converting '" + local_bmesh_path + "' to '" + local_GLB_path + "'...");

						BatchedMeshRef mesh = new BatchedMesh();
						mesh->readFromFile(local_bmesh_path, *mesh); // Load bmesh file
						FormatDecoderGLTF::writeBatchedMeshToGLBFile(*mesh, local_GLB_path, GLTFWriteOptions()); // Save as GLB

						// Add to resource manager
						ResourceRef new_resource = new Resource(resource_URL, local_GLB_path, Resource::State_Present, /*owner id=*/UserID(0));
						world_state.resource_manager->addResource(new_resource);

						local_path = local_GLB_path;
					}
					catch(glare::Exception& e)
					{
						conPrint("Error while converting bmesh to GLB: " + e.what());
						throw e;
					}
				}
				else
					conPrint("BMesh file for URL '" + URL_without_glb_ext + "' not present.");
			}
		}
#endif

		if(!local_path.empty())
		{
			// Resource is present, send it
			try
			{
				// Since resources have content hashes in URLs, the content for a given resource doesn't change.
				// Therefore we can always return HTTP 304 not modified responses.
				for(size_t i=0; i<request.headers.size(); ++i)
					if(StringUtils::equalCaseInsensitive(request.headers[i].key, "if-modified-since"))
					{
						conPrint("returning 304 Not Modified...");
						
						const std::string response = 
							"HTTP/1.1 304 Not Modified\r\n"
							"Connection: Keep-Alive\r\n"
							"\r\n";
				
						reply_info.socket->writeData(response.c_str(), response.size());
						return;
					}


				const std::string content_type = web::ResponseUtils::getContentTypeForPath(local_path); // Guess content type

				MemMappedFile file(local_path);

				// NOTE: only handle a single range for now, because the response content types (and encoding?) get different for multiple ranges.
				if(request.ranges.size() == 1)
				{
					for(size_t i=0; i<request.ranges.size(); ++i)
					{
						const web::Range range = request.ranges[i];
						if(range.start < 0 || range.start >= (int64)file.fileSize())
							throw glare::Exception("invalid range");
						
						int64 range_size;
						if(range.end_incl == -1) // if this range is just to the end:
							range_size = (int64)file.fileSize() - range.start;
						else
						{
							if(range.start > range.end_incl)
								throw glare::Exception("invalid range");
							range_size = range.end_incl - range.start + 1;
						}

						const int64 use_range_end = range.start + range_size;
						if(use_range_end > (int64)file.fileSize())
							throw glare::Exception("invalid range");

						//conPrint("\thandleResourceRequest: serving data range (start: " + toString(range.start) + ", range_size: " + toString(range_size) + ")");
				
						const std::string response = 
							"HTTP/1.1 206 Partial Content\r\n"
							"Content-Type: " + content_type + "\r\n"
							"Content-Range: bytes " + toString(range.start) + "-" + toString(use_range_end - 1) + "/" + toString(file.fileSize()) + "\r\n" // Note that ranges are inclusive, hence the - 1.
							"Cache-Control: max-age=1000000000, immutable\r\n"
							"Connection: Keep-Alive\r\n"
							"Content-Length: " + toString(range_size) + "\r\n"
							"\r\n";

						reply_info.socket->writeData(response.c_str(), response.size());

						// Sanity check range.start and range_size.  Should be valid by here.
						runtimeCheck((range.start >= 0) && (range.start <= (int64)file.fileSize()) && (range.start + range_size <= (int64)file.fileSize()));

						reply_info.socket->writeData((const uint8*)file.fileData() + range.start, range_size);
				
						// conPrint("\thandleResourceRequest: sent data range. (len: " + toString(range_size) + ")");
					}
				}
				else
				{
					// conPrint("handleResourceRequest: serving data for '" + resource_URL + "' (len: " + toString(file.fileSize()) + " B)");

					web::ResponseUtils::writeHTTPOKHeaderAndDataWithCacheControl(reply_info, file.fileData(), file.fileSize(), content_type.c_str(), /*cache control=*/"max-age=1000000000, immutable");

					// conPrint("\thandleResourceRequest: sent data. (len: " + toString(file.fileSize()) + ")");
				}
			}
			catch(glare::Exception&)
			{
				// conPrint("Error while handling resource request: " + e.what());
				web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "resource not found.");
				return;
			}
		}
		else
			web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "resource not found.");
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error while returning resource.");
	}
	catch(std::exception&)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error while returning resource.");
	}
}


void listResources(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
#if 0
	try
	{
		std::vector<std::string> URL_and_filenames;
		{
			Lock lock(world_state.resource_manager->getMutex());

			for(auto it = world_state.resource_manager->getResourcesForURL().begin(); it != world_state.resource_manager->getResourcesForURL().end(); ++it)
			{
				ResourceRef resource = it->second;
				
				if(resource->getState() == Resource::State_Present)
				{
					URL_and_filenames.push_back(resource->URL);
					URL_and_filenames.push_back(FileUtils::getFilename(resource->getLocalPath()));
				}
			}
		} // End lock scope

		const std::string reply = StringUtils::join(URL_and_filenames, "\n");
		web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, reply);
	}
	catch(glare::Exception&)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error while returning resource.");
	}
	catch(std::exception&)
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "Error while returning resource.");
	}
#endif
}



} // end namespace ResourceHandlers
