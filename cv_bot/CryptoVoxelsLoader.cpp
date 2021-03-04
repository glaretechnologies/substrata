/*=====================================================================
CryptoVoxelsLoader.cpp
----------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "CryptoVoxelsLoader.h"


//#include "ServerWorldState.h"
#include "../gui_client/ClientThread.h"
#include "../gui_client/ModelLoading.h"
#include "../shared/Protocol.h"
#include "../shared/ResourceManager.h"

#include <graphics/FormatDecoderVox.h>
#include <PlatformUtils.h>
#include <Timer.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Exception.h>
#include <Parser.h>
#include <Base64.h>
#include <JSONParser.h>
#include <zlib.h>
#include <Matrix4f.h>
#include <Quat.h>
#include <HTTPClient.h>
#include <Lock.h>
#include <KillThreadMessage.h>
#include <ContainerUtils.h>
#include <Sort.h>
#include <unordered_set>
//#include <iostream>
//#include <bitset>
#include <zstd.h>


void CryptoVoxelsLoaderThread::doRun()
{
	//while(1)
	//{
	//	CryptoVoxelsLoader::loadCryptoVoxelsData(*this->world_state);

	//	// Wait for N seconds or until we get a KillThreadMessage.
	//	ThreadMessageRef message;
	//	const bool got_message = getMessageQueue().dequeueWithTimeout(/*wait time (s)=*/120.0, message);
	//	if(got_message)
	//		if(dynamic_cast<KillThreadMessage*>(message.getPointer()))
	//			return;
	//}
}


void CryptoVoxelsLoaderThread::kill()
{

}


void sendCreateObjectMessageToServer(WorldObjectRef& ob, Reference<ClientThread>& client_thread)
{
	//conPrint("sendCreateObjectMessageToServer");

	// Send CreateObject message to server
	{
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		packet.writeUInt32(Protocol::CreateObject);
		ob->writeToNetworkStream(packet);

		client_thread->enqueueDataToSend(packet);
	}
}


void sendDestroyObjectMessageToServer(WorldObjectRef& ob, Reference<ClientThread>& client_thread)
{
	//conPrint("sendDestroyObjectMessageToServer");

	{
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		packet.writeUInt32(Protocol::DestroyObject);
		writeToStream(ob->uid, packet);

		client_thread->enqueueDataToSend(packet);
	}
}



void sendObjectFullUpdateMessageToServer(WorldObjectRef& ob, Reference<ClientThread>& client_thread)
{
	//conPrint("sendObjectFullUpdateMessageToServer");

	// Send ObjectFullUpdate message to server
	{
		SocketBufferOutStream packet(SocketBufferOutStream::DontUseNetworkByteOrder);

		packet.writeUInt32(Protocol::ObjectFullUpdate);
		ob->writeToNetworkStream(packet);

		client_thread->enqueueDataToSend(packet);
	}
}


/*
//NOTE: From Escaping.cpp in website core repo


See http://www.rfc-editor.org/rfc/rfc1738.txt

Section 2.2:

" The characters ";",
"/", "?", ":", "@", "=" and "&" are the characters which may be
reserved for special meaning within a scheme.
"

"Thus, only alphanumerics, the special characters "$-_.+!*'(),", and
reserved characters used for their reserved purposes may be used
unencoded within a URL."

Since we're encoding ' ' to '+', we'll encode '+' as well, to guarantee a one-to-one mapping
*/
const std::string URLEscape(const std::string& s)
{
	std::string result;
	result.reserve(s.size());

	for(size_t i=0; i<s.size(); ++i)
	{
		if(::isAlphaNumeric(s[i]))
		{
			result.push_back(s[i]);
		}
		else if(getNumMatches("$-_.!*'(),", s[i]) > 0)  // Was: "/?:@=&$-_.+!*'(),"
		{
			result.push_back(s[i]);
		}
		else if(s[i] == ' ')
		{
			result.push_back('+');
		}
		else
		{
			// Encode using '%'.
			// TODO: How is unicode handled?  Is unicode valid in URLs?
			result += std::string(1, '%') + ::toHexString((unsigned int)s[i]);
		}
	}
	return result;
}


struct VoxelComparator
{
	bool operator () (const Voxel& va, const Voxel& vb)
	{
		if(va.mat_index < vb.mat_index)
			return true;
		else if(va.mat_index > vb.mat_index)
			return false;
		else
		{
			const Vec3<int> a = va.pos;
			const Vec3<int> b = vb.pos;

			//if(a.x < b.x)
			//	return true;
			//else if(a.x > b.x)
			//	return false;
			//else // else a.x == b.x:
			//{
			//	if(a.y < b.y)
			//		return true;
			//	else if(a.y > b.y)
			//		return false;
			//	else // else a.y == b.y:
			//	{
			//		return a.z < b.z;
			//	}
			//}
			if(a.z < b.z)
				return true;
			else if(a.z > b.z)
				return false;
			else
			{
				if(a.y < b.y)
					return true;
				else if(a.y > b.y)
					return false;
				else
				{
					return a.x < b.x;
				}
			}
		}
	}
};


struct GetMatIndex
{
	size_t operator() (const Voxel& v)
	{
		return (size_t)v.mat_index;
	}
};

#if 0

std::vector<int> total_dict;
size_t total_compressed_size = 0;
size_t total_dict_compressed_size = 0;
size_t total_decoded_size = 0;

static void compressVoxelGroup(const VoxelGroup& group)
{
	// Step 1: sort by materials
	//std::vector<Voxel> sorted_voxels(group.voxels.size());
	//Sort::serialCountingSort(group.voxels.data(), sorted_voxels.data(), group.voxels.size(), GetMatIndex());

	std::vector<Voxel> sorted_voxels = group.voxels;
	std::sort(sorted_voxels.begin(), sorted_voxels.end(), VoxelComparator());

	size_t max_bucket = 0;
	for(size_t i=0; i<group.voxels.size(); ++i)
		max_bucket = myMax<size_t>(max_bucket, group.voxels[i].mat_index);

	const size_t num_buckets = max_bucket + 1;

	// Count num items in each bucket
	std::vector<size_t> counts(num_buckets, 0);
	for(size_t i=0; i<group.voxels.size(); ++i)
		counts[group.voxels[i].mat_index]++;


	Vec3<int> current_pos(0, 0, 0);
	int v_i = 0;

	std::vector<int> data;
	for(size_t z=0; z<counts.size(); ++z)
	{
		const int count = (int)counts[z];
		data.push_back(count); // count of voxels with that material

		for(size_t i=0; i<count; ++i)
		{
			Vec3<int> relative_pos = sorted_voxels[v_i].pos - current_pos;
			//conPrint("relative_pos: " + relative_pos.toString());

			data.push_back(relative_pos.x);
			data.push_back(relative_pos.y);
			data.push_back(relative_pos.z);

			current_pos = sorted_voxels[v_i].pos;

			v_i++;
		}
	}

	

	//std::vector<int> dict;

	// Print out data:
	//conPrint("------------data-----------");
	//for(size_t z=0; z<data.size(); ++z)
	//	conPrint(toString(data[z]));

	ZSTD_CCtx* context = ZSTD_createCCtx();

	// Compress data
	const size_t compressed_bound = ZSTD_compressBound(data.size() * sizeof(int));
	js::Vector<uint8, 16> compressed_data(compressed_bound);
	//Timer timer;
	const size_t compressed_size = ZSTD_compress(compressed_data.data(), compressed_data.size(), data.data(), data.size() * sizeof(int),
		ZSTD_CLEVEL_DEFAULT // compression level
	);

	const size_t dict_compressed_size = ZSTD_compress_usingDict(context, /*dest=*/compressed_data.data(), /*dest capacity=*/compressed_data.size(), data.data(), data.size() * sizeof(int),
		/*dict=*/total_dict.data(), total_dict.size() * sizeof(int),
		ZSTD_CLEVEL_DEFAULT // compression level
	);

	ContainerUtils::append(total_dict, data);

	total_compressed_size += compressed_size;
	total_dict_compressed_size += dict_compressed_size;

	conPrint("uncompressed size:      " + toString(group.voxels.size() * sizeof(Voxel)) + " B");
	conPrint("compressed_size:        " + toString(compressed_size) + " B");
	conPrint("dict_compressed_size:   " + toString(dict_compressed_size) + " B");

	const double ratio = (double)group.voxels.size() * sizeof(Voxel) / compressed_size;
	//conPrint("compression ratio: " + toString(ratio));
	
	printVar(total_decoded_size);
	printVar(total_compressed_size);
	printVar(total_dict_compressed_size);
}
#endif


// Load Ben's world data (CryptoVoxels), data in JSON format is from https://www.cryptovoxels.com/grid/parcels
void CryptoVoxelsLoader::loadCryptoVoxelsData(WorldState& world_state, Reference<ClientThread>& client_thread, Reference<ResourceManager>& resource_manager)
{
	conPrint("---------------loadCryptoVoxelsData()-----------------");

	glare::TaskManager task_manager;

	try
	{
		const std::string parcel_prefix = "CryptoVoxels Parcel #";
		const std::string feature_prefix = "CryptoVoxels Feature, uuid: ";

		// Build up a map from parcel id to WorldObject built from that parcel:
		std::map<int, WorldObjectRef> parcel_obs;
		std::map<std::string, WorldObjectRef> feature_obs; // Map from UUID to WorldObject built from that feature.
		uint64 max_existing_uid = 0;
		{
			Lock lock(world_state.mutex);

			for(auto it = world_state.objects.begin(); it != world_state.objects.end(); ++it)
			{
				max_existing_uid = myMax(it->second->uid.value(), max_existing_uid);

				if(::hasPrefix(it->second->content, parcel_prefix))
				{
					Parser parser(it->second->content.data(), it->second->content.size());
					parser.parseCString(parcel_prefix.c_str());
					int id;
					if(!parser.parseInt(id))
						throw glare::Exception("Parsing parcel UID failed.");

					parcel_obs[id] = it->second;
				}
				else if(::hasPrefix(it->second->content, feature_prefix))
				{
					Parser parser(it->second->content.data(), it->second->content.size());
					parser.parseCString(feature_prefix.c_str());
					string_view uuid;
					if(!parser.parseNonWSToken(uuid))
						throw glare::Exception("Parsing feature UID failed.");

					feature_obs[uuid.to_string()] = it->second;
				}
			}
		}

		const bool FORCE_UPDATE_ALL_CV_OBS = false;

		//TEMP: Remove all CV objects
		const bool REMOVE_ALL_CV_OBS = false;
		if(REMOVE_ALL_CV_OBS)
		{
			conPrint("Removing all CV objects...");
			Lock lock(world_state.mutex);
			const size_t initial_size = world_state.objects.size();
			for(auto it = world_state.objects.begin(); it != world_state.objects.end();)
			{
				if(::hasPrefix(it->second->content, parcel_prefix) || ::hasPrefix(it->second->content, feature_prefix)) // If a CV object
				{
					auto ob_it = it++;
					WorldObjectRef ob = ob_it->second;
					//world_state.objects.erase(ob_it);
					sendDestroyObjectMessageToServer(ob, client_thread);
				}
				else
					++it;
			}
			//if(world_state.objects.size() != initial_size)
			//	world_state.markAsChanged();

			parcel_obs.clear();
			feature_obs.clear();

			conPrint("Done removing all CV objects.");
			//return;
		}


		std::unordered_set<std::string> uuids_seen;

		size_t num_parcels_added = 0;
		size_t num_parcels_updated = 0;
		size_t num_parcels_unchanged = 0;
		size_t num_features_added = 0;
		size_t num_features_updated = 0;
		size_t num_features_unchanged = 0;

		

		const int MAX_PARCEL_ID = 5000;

		const bool download = false;
		if(download)
		{
			for(int i=0; i<MAX_PARCEL_ID; ++i)
			{

				// Download latest parcels.json
				// https://www.cryptovoxels.com/api/parcels.json to get list of all parcels
				// Current max id is 4522
				Timer timer;
				conPrint("Downloading parcel " + toString(i));
				HTTPClient client;
				std::string parcel_json;
				HTTPClient::ResponseInfo response_info = client.downloadFile("https://www.cryptovoxels.com/api/parcels/" + toString(i) + ".json", parcel_json);
				conPrint("\tDone.  (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
				if(response_info.response_code == 200)
				{
					if(parcel_json != "{\"success\":false}")
					{
						try
						{
							conPrint("saving to disk.");
							FileUtils::writeEntireFile("D:/substrata_stuff/CV_data/parcel" + toString(i) + ".json", parcel_json);
						}
						catch(glare::Exception& e)
						{
							conPrint(e.what());
						}
					}
				}
				else
				{
					conPrint("HTTP Download failed: response code was " + toString(response_info.response_code) + ": " + response_info.response_message);
				}
			}

			//// Download latest parcels.json
			//Timer timer;
			//conPrint("Downloading https://www.cryptovoxels.com/grid/parcels...");
			//HTTPClient client;
			//std::string parcels_json;
			//HTTPClient::ResponseInfo response_info = client.downloadFile("https://www.cryptovoxels.com/grid/parcels", parcels_json);
			//conPrint("\tDone.  (Elapsed: " + timer.elapsedStringNSigFigs(3) + ")");
			//if(response_info.response_code != 200)
			//	throw glare::Exception("HTTP Download failed: response code was " + toString(response_info.response_code) + ": " + response_info.response_message);

			//try
			//{
			//	conPrint("saving to parcels.json");
			//	FileUtils::writeEntireFile("D:/tempfiles/parcels.json", parcels_json);
			//}
			//catch(glare::Exception& e)
			//{
			//	conPrint(e.what());
			//}

			//parser.parseBuffer(parcels_json.data(), parcels_json.size());
		}
		else
		{
			//conPrint("Loading parcel data..");
			//parser.parseFile("D:\\downloads\\parcels8.json");
			//conPrint("Done.");
		}


		//const Vec3d final_offset_ws(600, 0, // Move to side of main parcels.
		//	-0.9);// parcels seem to have 2 voxels of stuff 'underground', so offset loaded data downwards a bit, in order to embed intro ground plane at z = 0.

		const Vec3d final_offset_ws(0, 0, // Move to side of main parcels.
			-0.9);// parcels seem to have 2 voxels of stuff 'underground', so offset loaded data downwards a bit, in order to embed intro ground plane at z = 0.

		const char* paths[] = {
			"00-grid.png",
			"01-grid.png",
			"02-window.png",
			"03-white-square.png",
			"04-line.png",
			"05-bricks.png",
			"06-the-xx.png",
			"07-lined.png",
			"08-nick-batt.png",
			"09-scots.png",
			"10-subgrid.png",
			"11-microblob.png",
			"12-smallblob.png",
			"13-smallblob.png",
			"14-blob.png",
			"03-white-square.png"
		};
		std::vector<std::string> inverted_paths(16);
#if _WIN32
		const std::string base_path = "O:\\new_cyberspace\\trunk\\resources\\CV";
#else
		const std::string base_path = "/home/nick/cyberspace_server_resources";
#endif
//		for(int i = 0; i < 16; ++i)
//		{
//			world_state.resource_manager->copyLocalFileToResourceDir(base_path + "/" + paths[i], /*URL=*/paths[i]);
//
//			inverted_paths[i] = std::string(paths[i]) + "-inverted.png";
//			if(FileUtils::fileExists(base_path + "/" + inverted_paths[i]))
//			{
//				world_state.resource_manager->copyLocalFileToResourceDir(base_path + "/" + inverted_paths[i], /*URL=*/inverted_paths[i]);
//			}
//		}

		// Make constant colour mats
		const char* colors[] = {
			"#ffffff",
			"#888888",
			"#000000",
			"#ff71ce",
			"#01cdfe",
			"#05ffa1",
			"#b967ff",
			"#fffb96"
		};
		std::vector<Colour3f> default_cols(8);
		for(int i = 0; i<8; ++i)
		{
			default_cols[i] = Colour3f(
				hexStringToUInt32(std::string(colors[i]).substr(1, 2)) / 255.0f,
				hexStringToUInt32(std::string(colors[i]).substr(3, 2)) / 255.0f,
				hexStringToUInt32(std::string(colors[i]).substr(5, 2)) / 255.0f
			);
		}

		Timer timer;
		

		std::vector<uint16> voxel_data;

		std::vector<unsigned char> data;
		int total_num_voxels = 0;
		uint64 next_uid = max_existing_uid + 1;
		/////conPrint("Num parcels: " + toString(parcels_array.child_indices.size()));
		for(int pz=0; pz<MAX_PARCEL_ID; ++pz)
		{
			conPrint("processing node " + toString(pz));

			const std::string json_path = "D:/substrata_stuff/CV_data/parcel" + toString(pz) + ".json";
			if(!FileUtils::fileExists(json_path))
				continue;
			
			JSONParser parser;
			parser.parseFile(json_path);

			assert(parser.nodes[0].type == JSONNode::Type_Object);

			const JSONNode& parcel_node = parser.nodes[0].getChildObject(parser, "parcel");

			int x1, y1, z1, x2, y2, z2, id;
			x1 = y1 = z1 = x2 = y2 = z2 = id = 0;

			std::vector<Colour3f> parcel_cols = default_cols;

			id = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "id", 0.0);
			//if(id != 177) continue; // 2 Cyber Junction (inverted doom skull)
			//if(id != 863) continue; // 20 Tune Drive (maze)
			//if(id != 50) continue; // house of pepe
			//if(id != 73) continue; NFT gallery
			// 2 = bens parcel in the middle

			//if(!(
			//	//id == 177 ||
			//	//id == 863 ||
			//	//id == 50 ||
			//	//id == 24 ||
			//	//id == 2 ||
			//	//id == 2783 // voxel farm+
			//	id == 6
			//	))
			//	continue;
			//if(id > 10)
			///	continue;

			x1 = (int)parcel_node.getChildDoubleValue(parser, "x1");
			y1 = (int)parcel_node.getChildDoubleValue(parser, "y1");
			z1 = (int)parcel_node.getChildDoubleValue(parser, "z1");
			x2 = (int)parcel_node.getChildDoubleValue(parser, "x2");
			y2 = (int)parcel_node.getChildDoubleValue(parser, "y2");
			z2 = (int)parcel_node.getChildDoubleValue(parser, "z2");

			// At this point hopefully we have parsed voxel data and coords
			const int xspan = x2 - x1;
			const int yspan = y2 - y1;
			const int zspan = z2 - z1;

			const int voxels_x = xspan * 2;
			const int voxels_y = yspan * 2;
			const int voxels_z = zspan * 2;

			voxel_data.resize(0);

			// Find content node
			if(parcel_node.hasChild("content"))
			{
				const JSONNode& content_node = parcel_node.getChildNode(parser, "content");

				if(content_node.type == JSONNode::Type_Object)
				for(size_t w = 0; w<content_node.name_val_pairs.size(); ++w)
				{
					if(content_node.name_val_pairs[w].name == "voxels")
					{
						const JSONNode& voxel_node = parser.nodes[content_node.name_val_pairs[w].value_node_index];

						assert(voxel_node.type == JSONNode::Type_String);

						Base64::decode(voxel_node.string_v, data);

						//conPrint("\nDecoded size: " + toString(data.size()) + " B");
						//total_decoded_size += data.size();

						// Allocate inflate state
						z_stream stream;
						stream.zalloc = Z_NULL;
						stream.zfree = Z_NULL;
						stream.opaque = Z_NULL;
						stream.next_in = (Bytef*)data.data();
						stream.avail_in = (unsigned int)data.size();

						int ret = inflateInit(&stream);
						if(ret != Z_OK)
							throw glare::Exception("inflateInit failed.");

						voxel_data.resize(1000000); // Hopefully big enough for all voxel fields
						stream.next_out = (Bytef*)voxel_data.data();
						stream.avail_out = (unsigned int)(voxel_data.size() * sizeof(uint16));

						int result = inflate(&stream, Z_FINISH);
						if(result != Z_STREAM_END)
							throw glare::Exception("inflate failed.");

						voxel_data.resize(stream.total_out / 2);

						inflateEnd(&stream);
					}
					else if(content_node.name_val_pairs[w].name == "palette")
					{
						// Load custom palette, e.g. "palette":["#ffffff","#888888","#000000","#80ffff","#01cdfe","#0080ff","#008080","#004080"]
						const JSONNode& palette_array = parser.nodes[content_node.name_val_pairs[w].value_node_index];

						if(palette_array.type != JSONNode::Type_Null)
						{
							if(palette_array.child_indices.size() != 8)
								throw glare::Exception("Invalid number of colours in palette.");

							for(size_t p = 0; p<8; ++p) // For each palette entry
							{
								const JSONNode& col_node = parser.nodes[palette_array.child_indices[p]];
								assert(col_node.type == JSONNode::Type_String);

								parcel_cols[p] = Colour3f(
									hexStringToUInt32(std::string(col_node.string_v).substr(1, 2)) / 255.0f,
									hexStringToUInt32(std::string(col_node.string_v).substr(3, 2)) / 255.0f,
									hexStringToUInt32(std::string(col_node.string_v).substr(5, 2)) / 255.0f
								);
							}
						}
					}
					else if(content_node.name_val_pairs[w].name == "features")
					{
						const JSONNode& features_array = parser.nodes[content_node.name_val_pairs[w].value_node_index];

						if(features_array.type != JSONNode::Type_Array)
							throw glare::Exception("features_array.type != JSONNode::Type_Array.");

						for(size_t s = 0; s < features_array.child_indices.size(); ++s)
						{
							try
							{
								const JSONNode& feature = parser.nodes[features_array.child_indices[s]];
								if(feature.type != JSONNode::Type_Object)
									throw glare::Exception("feature.type != JSONNode::Type_Object.");

								const std::string feature_type = feature.getChildStringValue(parser, "type");

								const std::string uuid = feature.getChildStringValueWithDefaultVal(parser, "uuid", "uuid_missing");
								if(uuid == "uuid_missing")
									conPrint("Warning: feature has no uuid.");
								else if(uuids_seen.count(uuid) == 1)
									conPrint("Warning: already processed feature with uuid " + uuid);
								else
								{
									uuids_seen.insert(uuid);

									if(feature_type == "image")
									{
										const std::string url = feature.getChildStringValueWithDefaultVal(parser, "url", "");

										// Load the image through the CV image proxy, in order to reduce the image size, and for faster loading.
										const std::string proxy_url = "http://cdn.cryptovoxels.com/node/img?url=" + URLEscape(url) + "&mode=color";

										const size_t MAX_URL_LEN = 4096; // Make sure texture URL is not too long - there are some excessively long URLs in the data.
										if((proxy_url != "") && (proxy_url.size() <= MAX_URL_LEN))
										{
											const bool color = feature.getChildBoolValueWithDefaultVal(parser, "color", true);

											Vec3d rotation, scale, position;
											feature.getChildArray(parser, "rotation").parseDoubleArrayValues(parser, 3, &rotation.x);
											feature.getChildArray(parser, "scale").parseDoubleArrayValues(parser, 3, &scale.x);
											feature.getChildArray(parser, "position").parseDoubleArrayValues(parser, 3, &position.x);

											//printVar(rotation);
											//printVar(scale);
											//printVar(position);

											//Matrix4f cv_to_indigo()
											// Do rotations around 
											// indigo x is -cv x
											// indigo y is -cv z
											// indigo z is -cv y
											// cv x is - indigo x
											// cv y is - indigo z
											// cv z is - indigo y

											Matrix4f rot = Matrix4f::rotationAroundXAxis((float)-rotation.x) * Matrix4f::rotationAroundYAxis((float)-rotation.z) *
												Matrix4f::rotationAroundZAxis((float)-rotation.y);

											Quatf quat = Quatf::fromMatrix(rot);
											Vec4f unit_axis;
											float angle;
											quat.toAxisAndAngle(unit_axis, angle);

											// Make a hypercard object
											WorldObjectRef ob = new WorldObject();
											//ob->uid = UID(next_uid++);
											ob->object_type = WorldObject::ObjectType_Generic;

											ob->model_url = "Quad_obj_13906643289783913481.igmesh"; // This mesh ranges in x and z from -0.5 to 0.5

											// The quad in babylon js ranges in x and y axes from -0.5 to 0.5

											ob->materials.resize(1);
											ob->materials[0] = new WorldMaterial();
											ob->materials[0]->colour_texture_url = proxy_url;

											Vec4f nudge_ws = rot * Vec4f(0.0f, 0.02f, 0, 0); // Nudge vector, to avoid z-fighting with walls, in Substrata world space

											Vec3d offset(0.75, 0.75, 0.25); // An offset vector to put the images in the correct place.  Still not sure why this is needed.

											// Get parcel centre in CV world space.  This is the centre of the parcel bounding box, but with y = 0.
											// See https://github.com/cryptovoxels/cryptovoxels/blob/master/src/parcel.ts#L94
											Vec3d parcel_centre_cvws = Vec3d(((double)x1 + (double)x2) / 2, 0, ((double)z1 + (double)z2) / 2);

											Vec3d pos_cvws = position + parcel_centre_cvws;

											// Convert to substrata/indigo coords (z-up)
											ob->pos = Vec3d(
												-pos_cvws.x, // - CV x
												-pos_cvws.z, // - CV z
												pos_cvws.y) + // - CV y
												offset + Vec3d(nudge_ws[0], nudge_ws[1], nudge_ws[2]) + final_offset_ws;

											// See https://github.com/cryptovoxels/cryptovoxels/blob/master/src/features/feature.ts#L107
											const double SCALE_EPSILON = 0.01;
											if(scale.x == 0) scale.x = SCALE_EPSILON;
											if(scale.y == 0) scale.y = SCALE_EPSILON;
											if(scale.z == 0) scale.z = SCALE_EPSILON;

											const double MAX_SCALE_MAG = 10;
											scale.x = myClamp(scale.x, -MAX_SCALE_MAG, MAX_SCALE_MAG);
											scale.y = myClamp(scale.y, -MAX_SCALE_MAG, MAX_SCALE_MAG);
											scale.z = myClamp(scale.z, -MAX_SCALE_MAG, MAX_SCALE_MAG);

											ob->scale = Vec3f((float)-scale.x, (float)scale.z, (float)scale.y);

											ob->axis = Vec3f(unit_axis[0], unit_axis[1], unit_axis[2]);
											ob->angle = angle;

											ob->created_time = TimeStamp::currentTime();
											ob->creator_id = UserID(0);

											ob->content = feature_prefix + uuid;

											ob->state = WorldObject::State_Alive;

											// Compare to existing feature object, if present.
											auto res = feature_obs.find(uuid);
											if(res == feature_obs.end())
											{
												// This is a new feature:
												ob->uid = UID(next_uid++); // Alloc new id
												ob->state = WorldObject::State_JustCreated;
												//ob->from_remote_other_dirty = true;
												ob->from_local_other_dirty = true;

												//Lock lock(world_state.mutex);
												//world_state.objects[ob->uid] = ob;

												sendCreateObjectMessageToServer(ob, client_thread);

												num_features_added++;
											}
											else
											{
												// We are updating an existing feature.

												// See if the feature has actually changed:
												Lock lock(world_state.mutex);
												WorldObjectRef existing_ob = res->second;

												if(!epsEqual(existing_ob->pos, ob->pos) || FORCE_UPDATE_ALL_CV_OBS)
												{
													//printVar(existing_ob->pos);
													//printVar(ob->pos);
													ob->uid = existing_ob->uid; // Use existing object id.
													//world_state.objects[ob->uid] = ob;

													//ob->state = WorldObject::State_Alive;
													//ob->from_remote_other_dirty = true;

													sendObjectFullUpdateMessageToServer(ob, client_thread);

													num_features_updated++;
												}
												else
													num_features_unchanged++;
											}
										}
									}
									else if(feature_type == "vox-model")
									{
										const std::string url = feature.getChildStringValueWithDefaultVal(parser, "url", "");
										const size_t MAX_URL_LEN = 4096; // Make sure texture URL is too long - there are some excessively long URLs in the data.
										if((url != "") && (url.size() <= MAX_URL_LEN))
										{
											// Process vox-model

											std::string local_path;
											if(resource_manager->isFileForURLPresent(url))
											{
												local_path = resource_manager->pathForURL(url);
											}
											else
											{
												// Try and download it
												try
												{
													// Some hosts just don't send anything on the connection (but don't close it)
													/*if(StringUtils::containsString(url, "file.io"))
														throw glare::Exception("Skipping URL with file.io.");
													if(StringUtils::containsString(url, "filebin.net"))
														throw glare::Exception("Skipping URL with filebin.net.");
													if(StringUtils::containsString(url, "t.co"))
														throw glare::Exception("Skipping URL with t.co.");

													if(StringUtils::containsString(url, "cdn.discordapp.com")) // TEMP
														throw Indigo::Exception("Skipping URL with cdn.discordapp.com.");


													conPrint("Downloading vox model '" + url + "'...");
													try
													{
														HTTPClient client;
														std::string file_data;
														HTTPClient::ResponseInfo response = client.downloadFile(url, file_data);
														if(response.response_code == 200)
														{
															local_path = resource_manager->computeDefaultLocalPathForURL(url);
															FileUtils::writeEntireFile(local_path, file_data);
														
															//conPrint("Saved to " + local_path);
															if(!FormatDecoderVox::isValidVoxFile(local_path))
																throw Indigo::Exception("invalid vox file.");

															conPrint("Successfully downloaded valid VOX file.");
														}
														else
															conPrint("HTTP Download failed: response code was " + toString(response.response_code) + ": " + response.response_message);
													}
													catch(Indigo::Exception& e)
													{
														conPrint("Error while downloading vox: " + e.what());
														local_path = "";
													}*/

													if(local_path == "")
													{
														try
														{

															// Try downloading via CV proxy:

															const std::string proxy_url = "http://cdn.cryptovoxels.com/node/vox?url=" + URLEscape(url);

															conPrint("trying to download from CV proxy, URL: " + proxy_url);

															HTTPClient client;
															std::string file_data;
															HTTPClient::ResponseInfo response = client.downloadFile(proxy_url, file_data);

															if(response.response_code == 200)
															{
																local_path = resource_manager->computeDefaultLocalPathForURL(url);
																FileUtils::writeEntireFile(local_path, file_data);

																// Mark resource as present
																resource_manager->getResourceForURL(url)->setState(Resource::State_Present);

																if(!FormatDecoderVox::isValidVoxFile(local_path))
																	throw glare::Exception("invalid vox file.");

																conPrint("Successfully downloaded valid VOX file.");
															}
															else
															{
																conPrint("HTTP Download from CV proxy failed: response code was " + toString(response.response_code) + ": " + response.response_message);
															}
														}
														catch(glare::Exception& e)
														{
															conPrint("Error while downloading vox: " + e.what());
															local_path = "";
														}
													}
												}
												catch(FileUtils::FileUtilsExcep& e)
												{
													conPrint("Error while downloading vox: " + e.what());
													local_path = "";
												}
											}

											if(!local_path.empty())
											{
												WorldObjectRef ob = new WorldObject();
												ob->object_type = WorldObject::ObjectType_VoxelGroup;

												// Load the vox:
												try
												{
													VoxFileContents vox_contents;
													FormatDecoderVox::loadModel(local_path, vox_contents);

													// Convert voxels
													const VoxModel& model = vox_contents.models[0];
													ob->getDecompressedVoxels().resize(model.voxels.size());
													for(size_t i=0; i<vox_contents.models[0].voxels.size(); ++i)
													{
														ob->getDecompressedVoxels()[i].pos = Vec3<int>(model.voxels[i].x, model.voxels[i].y, model.voxels[i].z);
														ob->getDecompressedVoxels()[i].mat_index = model.voxels[i].mat_index;
													}

													ob->compressVoxels();

													// Convert materials
													ob->materials.resize(vox_contents.used_materials.size());
													for(size_t i=0; i<ob->materials.size(); ++i)
													{
														ob->materials[i] = new WorldMaterial();
														ob->materials[i]->colour_rgb = Colour3f(
															vox_contents.used_materials[i].col_from_palette[0],
															vox_contents.used_materials[i].col_from_palette[1],
															vox_contents.used_materials[i].col_from_palette[2]);
													}

													Vec3d rotation, scale, position;
													feature.getChildArray(parser, "rotation").parseDoubleArrayValues(parser, 3, &rotation.x);
													feature.getChildArray(parser, "scale").parseDoubleArrayValues(parser, 3, &scale.x);
													feature.getChildArray(parser, "position").parseDoubleArrayValues(parser, 3, &position.x); // In CV space

													Matrix4f rot = Matrix4f::rotationAroundXAxis((float)-rotation.x) * Matrix4f::rotationAroundYAxis((float)-rotation.z) *
														Matrix4f::rotationAroundZAxis((float)-rotation.y);

													Quatf quat = Quatf::fromMatrix(rot);
													Vec4f unit_axis;
													float angle;
													quat.toAxisAndAngle(unit_axis, angle);

													/*
													From https://github.com/cryptovoxels/cryptovoxels/blob/master/src/utils/vox-worker.ts :

													// Identity function, use these to nudge the mesh as needed
													const fx = x => 0.02 * (x - originalSize.x / 2)
													const fy = y => 0.02 * (y - originalSize.y / 2)
													const fz = z => 0.02 * (z - originalSize.z * 0)
													*/
													scale *= 0.02;

													// NOTE: not sure scale.x and scale.z are not mixed up here.
													const Vec3d vox_translation_sub_os = Vec3d(-vox_contents.models[0].size_x/2 * scale.x, -vox_contents.models[0].size_y/2 * scale.z, 0);

													const Vec4f vox_translation_sub_ws = rot * vox_translation_sub_os.toVec4fVector();

													const Vec3d offset(0.75, 0.75, 0.25); // An offset vector to put the models in the correct place.  Still not sure why this is needed.

													// Get parcel centre in CV world space.  This is the centre of the parcel bounding box, but with y = 0.
													// See https://github.com/cryptovoxels/cryptovoxels/blob/master/src/parcel.ts#L94
													const Vec3d parcel_centre_cvws = Vec3d(((double)x1 + (double)x2) / 2, 0, ((double)z1 + (double)z2) / 2);

													const Vec3d pos_cvws = position + parcel_centre_cvws;

													// Convert to substrata/indigo coords (z-up)
													ob->pos = Vec3d(
														-pos_cvws.x, // - CV x
														-pos_cvws.z, // - CV z
														pos_cvws.y) + // + CV y
														toVec3d(vox_translation_sub_ws) + offset + final_offset_ws;

													// See https://github.com/cryptovoxels/cryptovoxels/blob/master/src/features/feature.ts#L107
													const double SCALE_EPSILON = 0.01;
													if(scale.x == 0) scale.x = SCALE_EPSILON;
													if(scale.y == 0) scale.y = SCALE_EPSILON;
													if(scale.z == 0) scale.z = SCALE_EPSILON;

													//const double MAX_SCALE_MAG = 10;
													//scale.x = myClamp(scale.x, -MAX_SCALE_MAG, MAX_SCALE_MAG);
													//scale.y = myClamp(scale.y, -MAX_SCALE_MAG, MAX_SCALE_MAG);
													//scale.z = myClamp(scale.z, -MAX_SCALE_MAG, MAX_SCALE_MAG);

													// NOTE: don't have to negate x scale (don't have to mirror object), because .vox models and substrata are both in right-handed coord systems.
													ob->scale = Vec3f((float)scale.x, (float)scale.z, (float)scale.y);

													ob->axis = Vec3f(unit_axis[0], unit_axis[1], unit_axis[2]);
													ob->angle = angle;

													ob->created_time = TimeStamp::currentTime();
													ob->creator_id = UserID(0);

													ob->content = feature_prefix + uuid;

													ob->flags = 0 | WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG; // Make not collidable.

													ob->state = WorldObject::State_Alive;

													// Compare to existing feature object, if present.
													auto res = feature_obs.find(uuid);
													if(res == feature_obs.end())
													{
														// This is a new feature:
														ob->uid = UID(next_uid++); // Alloc new id
														ob->state = WorldObject::State_JustCreated;
														ob->from_remote_other_dirty = true;

														//Lock lock(world_state.mutex);
														//world_state.objects[ob->uid] = ob;

														conPrint("Creating new feature with UID " + ob->uid.toString() + "...");

														sendCreateObjectMessageToServer(ob, client_thread);

														num_features_added++;
													}
													else
													{
														// We are updating an existing feature.

														// See if the feature has actually changed:
														Lock lock(world_state.mutex);
														WorldObjectRef existing_ob = res->second;

														if(existing_ob->getDecompressedVoxels() != ob->getDecompressedVoxels() || // If voxels changed:
															!epsEqual(existing_ob->pos, ob->pos) || (existing_ob->materials.size() != ob->materials.size()) ||
															!epsEqual(existing_ob->scale, ob->scale) || FORCE_UPDATE_ALL_CV_OBS)
														{
															//printVar(existing_ob->pos);
															//printVar(ob->pos);
															ob->uid = existing_ob->uid; // Use existing object id.

															//world_state.objects[ob->uid] = ob;

															//ob->state = WorldObject::State_Alive;
															//ob->from_remote_other_dirty = true;

															conPrint("Updating feature with UID " + ob->uid.toString() + "...");

															sendObjectFullUpdateMessageToServer(ob, client_thread);

															num_features_updated++;
														}
														else
															num_features_unchanged++;
													}
												}
												catch(glare::Exception& e)
												{
													conPrint("Error loading VOX file: " + e.what());
												}
											}
										}
									}
								}
							}
							catch(glare::Exception& e)
							{
								conPrint("Error while loading feature: " + e.what());
							}
						}
					}
				}
			}

			

			const int expected_num_voxels = voxels_x * voxels_y * voxels_z;

			if(!voxel_data.empty())
			{
				//assert(expected_num_voxels == voxel_data.size());
				if(expected_num_voxels != voxel_data.size())
					throw glare::Exception("voxel_data was wrong size");

				// Do a pass over voxels to get list of used mats
				std::map<uint16, size_t> mat_indices;

				for(int x = 0; x<expected_num_voxels; ++x)
				{
					const uint16 v = voxel_data[x];
					if(v != 0)
					{
						auto res = mat_indices.find(v);
						if(res == mat_indices.end()) // If not inserted yet
						{
							const size_t sz = mat_indices.size();
							mat_indices[v] = sz;
						}
					}
				}

				// Make substrata materials for this parcel.
				std::vector<WorldMaterialRef> parcel_mats(mat_indices.size());
				for(auto it = mat_indices.begin(); it != mat_indices.end(); ++it)
				{
					const uint16 mat_key = it->first;
					const size_t mat_index = it->second;

					const int colour_index = (mat_key >> 5) & 0x7;
					const int tex_index = mat_key & 0xF;
					const bool transparent = (((mat_key >> 15) & 0x1) == 0x0) && (colour_index == 0);

					parcel_mats[mat_index] = new WorldMaterial();
					parcel_mats[mat_index]->colour_rgb = parcel_cols[colour_index];

					if(colour_index == 2)
					{
						parcel_mats[mat_index]->colour_texture_url = inverted_paths[tex_index];
						parcel_mats[mat_index]->colour_rgb = Colour3f(1, 1, 1);
					}
					else
						parcel_mats[mat_index]->colour_texture_url = paths[tex_index];

					//if(tex_index == 2) // window texture means transparent.
					//	parcel_mats[mat_index]->opacity = 0.2;

					/*conPrint("---------");
					std::cout << std::bitset<16>(mat_key) << "\n";
					printVar(mat_index);
					printVar(colour_index);
					printVar(tex_index);
					conPrint("colour_texture_url: " + parcel_mats[mat_index]->colour_texture_url);
					conPrint("col: " + parcel_cols[colour_index].toVec3().toString());
					conPrint("transparent: " + boolToString(transparent));
					conPrint("");*/

					if(transparent)
						parcel_mats[mat_index]->opacity = 0.2;
				}

				VoxelGroup voxel_group;
				int read_i = 0;
				for(int x = x1; x<x1 + voxels_x; ++x)
					for(int y = y1; y<y1 + voxels_y; ++y)
						for(int z = z1; z<z1 + voxels_z; ++z)
						{
							const uint16 v = voxel_data[read_i++];
							if(v != 0)
							{
								assert(mat_indices.count(v) == 1);
								const int final_mat_index = (int)mat_indices[v];

								// Get relative xyz in CV coords (y-up, left-handed)
								const int rx = x - x1;
								const int ry = y - y1;
								const int rz = z - z1;

								// Convert to substrata coords (z-up)
								const int use_x = -rx;
								const int use_y = -rz;
								const int use_z = ry;
								voxel_group.voxels.push_back(Voxel(Vec3<int>(use_x, use_y, use_z), final_mat_index));
							}
						}

				assert(read_i == expected_num_voxels);

				if(voxel_group.voxels.size() > 0)
				{
					// Convert to substrata coords (z-up)
					const int use_x = -x1;
					const int use_y = -z1;
					const int use_z = y1;

					// Scale matrix is 0.5 as voxels are 0.5 m wide in CV.
					WorldObjectRef voxels_ob = new WorldObject();
					//voxels_ob->uid = UID(100000000 + q);
					//voxels_ob->uid = UID(next_uid++);
					voxels_ob->object_type = WorldObject::ObjectType_VoxelGroup;
					voxels_ob->materials = parcel_mats;
					voxels_ob->pos = Vec3d(use_x, use_y, use_z) + final_offset_ws;
					voxels_ob->scale = Vec3f(0.5f);
					voxels_ob->axis = Vec3f(0, 0, 1);
					voxels_ob->angle = 0;

					voxels_ob->getDecompressedVoxels() = voxel_group.voxels;
					voxels_ob->compressVoxels();

					voxels_ob->created_time = TimeStamp::currentTime();
					voxels_ob->creator_id = UserID(0);

					voxels_ob->content = parcel_prefix + toString(id);
					voxels_ob->state = WorldObject::State_Alive;

					// Compare to existing parcel object, if present.
					auto res = parcel_obs.find(id);
					if(res == parcel_obs.end())
					{
						// This is a new parcel:
						voxels_ob->uid = UID(next_uid++); // Alloc new id
						voxels_ob->state = WorldObject::State_JustCreated;
						voxels_ob->from_remote_other_dirty = true;

						//Lock lock(world_state.mutex);
						//world_state.objects[voxels_ob->uid] = voxels_ob;

						conPrint("Creating new parcel with UID " + voxels_ob->uid.toString() + "...");

						sendCreateObjectMessageToServer(voxels_ob, client_thread);

						num_parcels_added++;
					}
					else
					{
						// We are updating an existing parcel.

						// See if the voxels have actually changed:

						Lock lock(world_state.mutex);
						WorldObjectRef existing_ob = res->second;

						if(existing_ob->getDecompressedVoxels() != voxels_ob->getDecompressedVoxels() ||   // If voxels changed:
							!epsEqual(existing_ob->pos, voxels_ob->pos) || FORCE_UPDATE_ALL_CV_OBS)
						{
							// Use existing uid
							voxels_ob->uid = existing_ob->uid;

							//world_state.objects[voxels_ob->uid] = voxels_ob;

							//voxels_ob->state = WorldObject::State_Alive;
							//voxels_ob->from_remote_other_dirty = true;

							conPrint("Updating parcel with UID " + voxels_ob->uid.toString() + "...");

							sendObjectFullUpdateMessageToServer(voxels_ob, client_thread);

							num_parcels_updated++;
						}
						else
							num_parcels_unchanged++;
					}

					total_num_voxels += (int)voxel_group.voxels.size();
				}
			}
		}

		if(num_parcels_added > 0 || num_parcels_updated > 0 ||
			num_features_added > 0 || num_features_updated > 0)
		{
			//world_state.markAsChanged();
		}

		conPrint("Loaded all voxel data in " + timer.elapsedString());
		conPrint("total_num_voxels " + toString(total_num_voxels));
		conPrint("");
		conPrint("num_parcels_added: " + toString(num_parcels_added));
		conPrint("num_parcels_updated: " + toString(num_parcels_updated));
		conPrint("num_parcels_unchanged: " + toString(num_parcels_unchanged));
		conPrint("");
		conPrint("num_features_added: " + toString(num_features_added));
		conPrint("num_features_updated: " + toString(num_features_updated));
		conPrint("num_features_unchanged: " + toString(num_features_unchanged));
		conPrint("");
		conPrint("loadCryptoVoxelsData() done.");
		conPrint("---------------loadCryptoVoxelsData() done.-----------------");
	}
	catch(glare::Exception& e)
	{
		conPrint("ERROR: " + e.what());
	}
}
