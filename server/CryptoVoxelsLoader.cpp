/*=====================================================================
CryptoVoxelsLoader.cpp
----------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "CryptoVoxelsLoader.h"


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
//#include <iostream>
//#include <bitset>


// Load Ben's world data (CryptoVoxels), data is from https://www.cryptovoxels.com/grid/parcels
void CryptoVoxelsLoader::loadCryptoVoxelsData(ServerWorldState& world_state)
{
	conPrint("loadCryptoVoxelsData()");
	

	const double z_offset = -0.9; // parcels seem to have 2 voxels of stuff 'underground', so offset loaded data downwards a bit, in order to embed intro ground plane at z = 0.
	try
	{
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
		const std::string base_path = "D:\\files\\cryptovoxels_textures";
		for(int i = 0; i < 16; ++i)
		{
			world_state.resource_manager->copyLocalFileToResourceDir(base_path + "/" + paths[i], /*URL=*/paths[i]);

			inverted_paths[i] = std::string(paths[i]) + "-inverted.png";
			if(FileUtils::fileExists(base_path + "/" + inverted_paths[i]))
			{
				world_state.resource_manager->copyLocalFileToResourceDir(base_path + "/" + inverted_paths[i], /*URL=*/inverted_paths[i]);
			}
		}

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
		JSONParser parser;
		parser.parseFile("D:\\new_cyberspace\\parcels.json");

		std::vector<uint16> voxel_data;
		voxel_data.resize(1000000);

		std::vector<unsigned char> data;

		assert(parser.nodes[0].type == JSONNode::Type_Object);

		int total_num_voxels = 0;

		int next_uid = 1000000;

		const JSONNode& parcels_array = parser.nodes[0].getChildArray(parser, "parcels");

		conPrint("Num parcels: " + toString(parcels_array.child_indices.size()));

		for(size_t q = 0; q<parcels_array.child_indices.size(); ++q)
		{
			const JSONNode& parcel_node = parser.nodes[parcels_array.child_indices[q]];

			int x1, y1, z1, x2, y2, z2, id;
			x1 = y1 = z1 = x2 = y2 = z2 = id = 0;

			std::vector<Colour3f> parcel_cols = default_cols;

			for(size_t w = 0; w<parcel_node.name_val_pairs.size(); ++w)
			{
				id = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "id", 0.0);
				//if(id != 177) continue; // 2 Cyber Junction (inverted doom skull)
				//if(id != 863) continue; // 20 Tune Drive (maze)
				//if(id != 50) continue; // house of pepe
				//if(id != 73) continue; NFT gallery
				// 2 = bens parcel in the middle

				if(!(
					id == 177 ||
					id == 863 ||
					id == 50 ||
					id == 24
					))
					continue;

				x1 = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "x1", 0.0);
				y1 = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "y1", 0.0);
				z1 = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "z1", 0.0);
				x2 = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "x2", 0.0);
				y2 = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "y2", 0.0);
				z2 = (int)parcel_node.getChildDoubleValueWithDefaultVal(parser, "z2", 0.0);

				if(parcel_node.name_val_pairs[w].name == "voxels")
				{
					const JSONNode& voxel_node = parser.nodes[parcel_node.name_val_pairs[w].value_node_index];

					assert(voxel_node.type == JSONNode::Type_String);

					Base64::decode(voxel_node.string_v, data);

					// Allocate deflate state
					z_stream stream;
					stream.zalloc = Z_NULL;
					stream.zfree = Z_NULL;
					stream.opaque = Z_NULL;
					stream.next_in = (Bytef*)data.data();
					stream.avail_in = (unsigned int)data.size();

					int ret = inflateInit(&stream);
					if(ret != Z_OK)
						throw Indigo::Exception("inflateInit failed.");

					stream.next_out = (Bytef*)voxel_data.data();
					stream.avail_out = (unsigned int)(voxel_data.size() * sizeof(uint16));

					int result = inflate(&stream, Z_FINISH);
					if(result != Z_STREAM_END)
						throw Indigo::Exception("inflate failed.");

					inflateEnd(&stream);
				}
				else if(parcel_node.name_val_pairs[w].name == "palette")
				{
					// Load custom palette, e.g. "palette":["#ffffff","#888888","#000000","#80ffff","#01cdfe","#0080ff","#008080","#004080"]
					const JSONNode& palette_array = parser.nodes[parcel_node.name_val_pairs[w].value_node_index];

					if(palette_array.type != JSONNode::Type_Null)
					{
						if(palette_array.child_indices.size() != 8)
							throw Indigo::Exception("Invalid number of colours in palette.");

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
				else if(parcel_node.name_val_pairs[w].name == "features")
				{
					const JSONNode& features_array = parser.nodes[parcel_node.name_val_pairs[w].value_node_index];

					if(features_array.type != JSONNode::Type_Array)
						throw Indigo::Exception("features_array.type != JSONNode::Type_Array.");

					for(size_t s = 0; s < features_array.child_indices.size(); ++s)
					{
						const JSONNode& feature = parser.nodes[features_array.child_indices[s]];
						if(feature.type != JSONNode::Type_Object)
							throw Indigo::Exception("feature.type != JSONNode::Type_Object.");

						const std::string feature_type = feature.getChildStringValue(parser, "type");

						if(feature_type == "image")
						{
							const std::string url = feature.getChildStringValueWithDefaultVal(parser, "url", "");
							if(url != "")
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
								//Matrix4f rot = Matrix4f::rotationAroundXAxis(rotation.x) * Matrix4f::rotationAroundXAxis(rotation.y) * Matrix4f::rotationAroundXAxis(rotation.z);
								Quatf quat = Quatf::fromMatrix(rot);
								Vec4f unit_axis;
								float angle;
								quat.toAxisAndAngle(unit_axis, angle);

								// Make a hypercard object
								WorldObjectRef ob = new WorldObject();
								ob->uid = UID(next_uid++);
								ob->object_type = WorldObject::ObjectType_Generic;

								ob->model_url = "Quad_obj_13906643289783913481.igmesh"; // TEMP

								ob->materials.resize(1);
								ob->materials[0] = new WorldMaterial();
								ob->materials[0]->colour_texture_url = url;

								Vec4f nudge = rot * Vec4f(0.0f, -0.02f, 0, 0); // Nudge forwads to avoid z-fighting with walls.

								Vec3d offset(0.75, 1.25, 0.23);

								Vec3d pos_cvws = position + Vec3d((x1 + x2) / 2, y1, (z1 + z2) / 2) + Vec3d(nudge[0], nudge[1], nudge[2]);
								//Vec3d pos_cvws = position + Vec3d((int)((x1 + x2) / 2), y1, (int)((z1 + z2) / 2)) + Vec3d(nudge[0], nudge[1], nudge[2]);
								//Vec3d pos_cvws = position + Vec3d((x1 + x2) / 2 - 0.25, y1 + 0.25, (z1 + z2) / 2 - 0.25) + Vec3d(nudge[0], nudge[1], nudge[2]);

								// Convert to substrata coords (z-up)
								ob->pos = Vec3d(-pos_cvws.x, -pos_cvws.z, pos_cvws.y + z_offset) + offset;

								if(scale.x == 0 || scale.y == 0 || scale.z == 0)
								{
									conPrint("scale elem was zero: " + scale.toString());
									scale = Vec3d(1, 1, 1);
								}
								//else
								{
									ob->scale = Vec3f((float)-scale.x, (float)scale.z, (float)scale.y);

									ob->axis = Vec3f(unit_axis[0], unit_axis[1], unit_axis[2]);
									ob->angle = angle;

									ob->created_time = TimeStamp::currentTime();
									ob->creator_id = UserID(0);

									world_state.objects[ob->uid] = ob;
								}

								ob->state = WorldObject::State_Alive;
							}
						}
					}
				}
			}

			// At this point hopefully we have parsed voxel data and coords
			const int xspan = x2 - x1;
			const int yspan = y2 - y1;
			const int zspan = z2 - z1;

			const int voxels_x = xspan * 2;
			const int voxels_y = yspan * 2;
			const int voxels_z = zspan * 2;

			const int expected_num_voxels = voxels_x * voxels_y * voxels_z;

			//assert(expected_num_voxels == voxel_data.size());

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
				voxels_ob->uid = UID(100000000 + q);
				voxels_ob->object_type = WorldObject::ObjectType_VoxelGroup;
				voxels_ob->materials = parcel_mats;
				voxels_ob->pos = Vec3d(use_x, use_y, use_z + z_offset);
				voxels_ob->scale = Vec3f(0.5f);
				voxels_ob->axis = Vec3f(0, 0, 1);
				voxels_ob->angle = 0;

				voxels_ob->voxel_group = voxel_group;

				voxels_ob->created_time = TimeStamp::currentTime();
				voxels_ob->creator_id = UserID(0);

				voxels_ob->content = "Parcel #" + toString(id);
				voxels_ob->state = WorldObject::State_Alive;

				world_state.objects[voxels_ob->uid] = voxels_ob;

				total_num_voxels += (int)voxel_group.voxels.size();
			}
		}

		conPrint("Loaded all voxel data in " + timer.elapsedString());
		conPrint("total_num_voxels " + toString(total_num_voxels));
		conPrint("loadCryptoVoxelsData() done.");
	}
	catch(Indigo::Exception& e)
	{
		conPrint("ERROR: " + e.what());
	}
}
