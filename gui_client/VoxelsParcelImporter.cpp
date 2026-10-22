/*=====================================================================
VoxelsParcelImporter.cpp
------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelsParcelImporter.h"


#include "GUIClient.h"
#include "ClientThread.h"
#include "WorldState.h"
#include "CreateObjectTracker.h"
#include "../shared/WorldObject.h"
#include "../shared/WorldMaterial.h"
#include "../shared/ResourceManager.h"
#include "../shared/LODGeneration.h"
#include "../shared/MessageUtils.h"
#include "../shared/Protocol.h"
#include <graphics/FormatDecoderVox.h>
#include <graphics/imformatdecoder.h>
#include <graphics/Map2D.h>
#include <graphics/Colour4f.h>
#include <networking/HTTPClient.h>
#include <networking/URL.h>
#include <maths/Matrix4f.h>
#include <maths/Quat.h>
#include <utils/JSONParser.h>
#include <utils/Base64.h>
#include <utils/FileUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include <utils/Timer.h>
#include <utils/Lock.h>
#include <utils/BitUtils.h>
#include <utils/SocketBufferOutStream.h>
#include <utils/Exception.h>
#include <utils/IncludeXXHash.h>
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <map>


/*
Coordinate systems
------------------
Voxels uses Babylon.js conventions: y up, left-handed.  Feature positions are relative to the parcel's horizontal centre, at the
parcel's base height.

A point (x, y, z) in Voxels space is (-x, -z, y) in Substrata space.  That mapping is a reflection, so it also turns Voxels' left-handed
space into Substrata's right-handed one.

The parcel voxel grid is 0.5 m voxels, stored with x outermost and z innermost.  Relative to the parcel centre, voxel (rx, ry, rz)
covers x in [-W/2 + 0.25 + rx/2, -W/2 + 0.75 + rx/2], where W is the parcel width in metres, and likewise for z, and covers y in
[-0.25 + ry/2, 0.25 + ry/2].  These offsets were found by fitting the voxels to the features hung on the walls and resting on the floors.
*/


static const Vec3d voxelsToSubstrataPos(const Vec3d& p)
{
	return Vec3d(-p.x, -p.z, p.y);
}


// The rotation of a feature, in Substrata space.  Babylon applies Euler angles roll (about z) first, then pitch (about x), then yaw
// (about y).  Since the change of space is a reflection, a rotation about a Voxels axis becomes a rotation about the mapped axis by the
// negated angle: Voxels y maps to Substrata +z, x to -x, and z to -y.
static const Matrix4f featureRotation(const Vec3d& r)
{
	return Matrix4f::rotationAroundZAxis((float)-r.y) * Matrix4f::rotationAroundXAxis((float)r.x) * Matrix4f::rotationAroundYAxis((float)r.z);
}


static void setAxisAndAngle(const Matrix4f& rot, WorldObject& ob)
{
	Vec4f axis;
	float angle;
	Quatf::fromMatrix(rot).toAxisAndAngle(axis, angle);
	ob.axis = Vec3f(axis[0], axis[1], axis[2]);
	ob.angle = angle;
}


// Parses a colour like "#ff71ce".  Colour4f::fromHTMLHexString() gives red for invalid hex digits.
static const Colour3f parseHexColour(const std::string& s, const Colour3f& default_col)
{
	if(s.size() != 7 || s[0] != '#')
		return default_col;
	const Colour4f col = Colour4f::fromHTMLHexString(s.substr(1));
	return Colour3f(col[0], col[1], col[2]);
}


static const Vec3d getVec3Field(const JSONParser& parser, const JSONNode& node, const char* name, const Vec3d& default_val)
{
	if(!node.hasChild(name))
		return default_val;
	const JSONNode& array = node.getChildNode(parser, name);
	if(array.type != JSONNode::Type_Array || array.child_indices.size() != 3)
		return default_val;
	Vec3d v;
	for(int i=0; i<3; ++i)
	{
		const JSONNode& elem = parser.nodes[array.child_indices[i]];
		if(elem.type != JSONNode::Type_Number || !isFinite(elem.value.double_v))
			return default_val;
		(&v.x)[i] = elem.value.double_v;
	}
	return v;
}


// Feature fields are loosely typed: 'transparent' for example is false on some images and "AlphaBlend" on others.  Only an explicit
// boolean counts here.
static bool getBoolField(const JSONParser& parser, const JSONNode& node, const char* name, bool default_val)
{
	if(!node.hasChild(name))
		return default_val;
	const JSONNode& child = node.getChildNode(parser, name);
	return (child.type == JSONNode::Type_Boolean) ? child.value.bool_v : default_val;
}


static const std::string getStringField(const JSONParser& parser, const JSONNode& node, const char* name)
{
	if(!node.hasChild(name))
		return std::string();
	const JSONNode& child = node.getChildNode(parser, name);
	return (child.type == JSONNode::Type_String) ? child.string_v : std::string();
}


// A position in the Voxels world, from a teleport link such as https://www.voxels.com/play?coords=E@351W,444S,15U.
struct VoxelsCoords
{
	char facing; // 'N', 'E', 'S' or 'W', or 0 if not given.
	double x; // East is positive.
	double z; // North is positive.
	double height; // Of the feet.  0 if not given.
};


// Parses coords like "E@351W,444S,15U": the letter before the '@' is the direction faced, then come the x (E/W), z (N/S) and optionally the
// height (U).  Throws glare::Exception if invalid.
static VoxelsCoords parseVoxelsCoords(const std::string& coords)
{
	VoxelsCoords res;
	res.facing = 0;
	res.x = res.z = res.height = 0;

	std::string rest = coords;
	const size_t at_pos = coords.find('@');
	if(at_pos != std::string::npos)
	{
		if(at_pos == 1)
			res.facing = coords[0];
		rest = coords.substr(at_pos + 1);
	}

	bool have_x = false, have_z = false;
	const std::vector<std::string> parts = ::split(rest, ',');
	for(size_t i=0; i<parts.size(); ++i)
	{
		const std::string& part = parts[i];
		if(part.size() < 2)
			throw glare::Exception("invalid coords '" + coords + "'");

		double val;
		try
		{
			val = stringToDouble(part.substr(0, part.size() - 1));
		}
		catch(StringUtilsExcep&)
		{
			throw glare::Exception("invalid coords '" + coords + "'");
		}
		if(!isFinite(val))
			throw glare::Exception("invalid coords '" + coords + "'");

		const char dir = part.back();
		if(dir == 'E')      { res.x =  val; have_x = true; }
		else if(dir == 'W') { res.x = -val; have_x = true; }
		else if(dir == 'N') { res.z =  val; have_z = true; }
		else if(dir == 'S') { res.z = -val; have_z = true; }
		else if(dir == 'U') { res.height = val; }
		else
			throw glare::Exception("invalid coords '" + coords + "'");
	}
	if(!have_x || !have_z)
		throw glare::Exception("invalid coords '" + coords + "'");

	return res;
}


static bool isHostOrSubdomain(const std::string& host, const std::string& domain);


// Returns true and sets coords_out if link is a Voxels teleport link.  Throws glare::Exception if it's a Voxels link with invalid coords.
static bool getVoxelsTeleportCoords(const std::string& link, VoxelsCoords& coords_out)
{
	URL url;
	try
	{
		url = URL::parseURL(link);
	}
	catch(glare::Exception&)
	{
		return false; // Not something we can recognise as a Voxels link.
	}

	const std::string host = ::toLowerCase(url.host);
	if(!(isHostOrSubdomain(host, "voxels.com") || isHostOrSubdomain(host, "cryptovoxels.com")))
		return false;

	const std::map<std::string, std::string> query = URL::parseQuery(url.query);
	const auto coords_res = query.find("coords");
	if(coords_res == query.end())
		throw glare::Exception("a Voxels link without coords");

	coords_out = parseVoxelsCoords(coords_res->second);
	return true;
}


static double getNumberField(const JSONParser& parser, const JSONNode& node, const char* name)
{
	const JSONNode& child = node.getChildNode(parser, name);
	if(child.type != JSONNode::Type_Number)
		throw glare::Exception(std::string("'") + name + "' is not a number.");
	return child.value.double_v;
}


// Voxels clamps zero scales to a small value rather than letting objects collapse.
static double nonZeroScale(double s)
{
	return (s == 0) ? 0.01 : s;
}


static void inflateZlibData(const std::vector<unsigned char>& compressed, std::vector<uint16>& data_out)
{
	const size_t MAX_DECOMPRESSED_SIZE = 256 * 1024 * 1024;

	z_stream stream;
	std::memset(&stream, 0, sizeof(stream));
	if(inflateInit(&stream) != Z_OK)
		throw glare::Exception("inflateInit failed.");

	std::vector<unsigned char> out(compressed.size() * 8 + 1024);
	stream.next_in = (Bytef*)compressed.data();
	stream.avail_in = (unsigned int)compressed.size();

	while(1)
	{
		stream.next_out = out.data() + stream.total_out;
		stream.avail_out = (unsigned int)(out.size() - stream.total_out);

		const int result = inflate(&stream, Z_NO_FLUSH);
		if(result == Z_STREAM_END)
			break;
		if(result != Z_OK && result != Z_BUF_ERROR)
		{
			inflateEnd(&stream);
			throw glare::Exception("Failed to decompress voxel data.");
		}
		if(stream.avail_out == 0)
		{
			if(out.size() * 2 > MAX_DECOMPRESSED_SIZE)
			{
				inflateEnd(&stream);
				throw glare::Exception("Voxel data is too large.");
			}
			out.resize(out.size() * 2);
		}
		else if(stream.avail_in == 0)
		{
			inflateEnd(&stream);
			throw glare::Exception("Voxel data is truncated.");
		}
	}

	const size_t num_bytes = stream.total_out;
	inflateEnd(&stream);

	if(num_bytes % 2 != 0)
		throw glare::Exception("Voxel data has an odd number of bytes.");

	data_out.resize(num_bytes / 2);
	if(num_bytes > 0)
		std::memcpy(data_out.data(), out.data(), num_bytes);
}


void VoxelsParcelImporter::inferVoxelDims(const uint16* a, size_t n, int& x_out, int& y_out, int& z_out)
{
	const size_t MIN_DIM = 8;

	double best_score = -1;
	for(size_t X=MIN_DIM; X<=n; X += 2)
	{
		if(n % X != 0)
			continue;
		for(size_t Y=MIN_DIM; Y<=n/X; Y += 2)
		{
			if((n / X) % Y != 0)
				continue;
			const size_t Z = n / (X * Y);
			if(Z < MIN_DIM || Z % 2 != 0)
				continue;

			// Count matching neighbours along each axis.
			size_t num_same = 0;
			for(size_t x=0; x<X; ++x)
			for(size_t y=0; y<Y; ++y)
			{
				const size_t row = (x*Y + y)*Z;
				for(size_t z=0; z<Z; ++z)
				{
					const size_t i = row + z;
					const uint16 v = a[i];
					if(z+1 < Z) num_same += (v == a[i + 1]);
					if(y+1 < Y) num_same += (v == a[i + Z]);
					if(x+1 < X) num_same += (v == a[i + Y*Z]);
				}
			}
			const size_t num_pairs = (X-1)*Y*Z + X*(Y-1)*Z + X*Y*(Z-1);
			const double score = (double)num_same / (double)num_pairs;

			if(score > best_score)
			{
				best_score = score;
				x_out = (int)X;
				y_out = (int)Y;
				z_out = (int)Z;
			}
		}
	}

	if(best_score < 0)
		throw glare::Exception("Couldn't work out the parcel dimensions from the " + toString(n) + " voxels in the voxel data.");
}


static bool isHostOrSubdomain(const std::string& host, const std::string& domain)
{
	return (host == domain) || ::hasSuffix(host, "." + domain);
}


// Returns the URL to download.  Throws glare::Exception, with the reason, if we shouldn't download it.
static const std::string getDownloadURL(const std::string& url_string)
{
	if(url_string.empty())
		throw glare::Exception("no URL");

	URL url;
	try
	{
		url = URL::parseURL(url_string);
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("couldn't parse URL: " + e.what());
	}

	const std::string scheme = ::toLowerCase(url.scheme);
	if(scheme != "http" && scheme != "https")
		throw glare::Exception("not an http(s) URL, so it is hosted by Voxels or isn't reachable");

	const std::string host = ::toLowerCase(url.host);
	if(isHostOrSubdomain(host, "voxels.com") || isHostOrSubdomain(host, "cryptovoxels.com"))
		throw glare::Exception("hosted by Voxels");

	// Dropbox share links serve a web page unless asked for the file itself, with dl=1 (or raw=1) in the query.
	if(isHostOrSubdomain(host, "dropbox.com"))
	{
		// Split the query on '&' rather than using URL::parseQuery(), which unescapes, so the other parameters are passed through unchanged.
		std::vector<std::string> params = ::split(url.query, '&');
		bool have_dl = false;
		for(size_t i=0; i<params.size(); ++i)
		{
			if(params[i] == "dl=0")
				params[i] = "dl=1";
			if(params[i] == "dl=1" || params[i] == "raw=1")
				have_dl = true;
		}
		if(!have_dl)
			params.push_back("dl=1");

		std::string query;
		for(size_t i=0; i<params.size(); ++i)
			if(!params[i].empty())
				query += (query.empty() ? "" : "&") + params[i];

		return url.scheme + "://" + url.host + ((url.port != -1) ? (":" + toString(url.port)) : std::string()) + url.path + "?" + query +
			(url.fragment.empty() ? std::string() : ("#" + url.fragment));
	}

	return url_string;
}


static const std::string sniffExtension(const std::string& data)
{
	if(data.size() >= 8 && std::memcmp(data.data(), "\x89PNG", 4) == 0)
		return "png";
	if(data.size() >= 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8 && (unsigned char)data[2] == 0xFF)
		return "jpg";
	if(data.size() >= 6 && std::memcmp(data.data(), "GIF8", 4) == 0)
		return "gif";
	if(data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WEBP", 4) == 0)
		return "webp";
	if(data.size() >= 8 && std::memcmp(data.data(), "VOX ", 4) == 0)
		return "vox";
	return std::string();
}


// The resource URL for an asset downloaded from url_string (the URL stored in the feature), with the given file type.
// It is a function of url_string rather than of the file contents, so an asset that has already been downloaded can be found without downloading
// it again.  Named after the last path segment of the URL.
static const URLString resourceURLForSourceURL(const std::string& url_string, const std::string& extension)
{
	std::string name;
	try
	{
		const std::string path = URL::parseURL(url_string).path;
		const size_t slash_pos = path.rfind('/');
		name = ::eatExtension((slash_pos != std::string::npos) ? path.substr(slash_pos + 1) : path);
	}
	catch(glare::Exception&)
	{}
	if(!name.empty() && name.back() == '.')
		name.pop_back();
	if(name.size() > 60)
		name = name.substr(0, 60);
	if(name.empty())
		name = "asset";

	return ResourceManager::URLForNameAndExtensionAndHash(name, extension, XXH64(url_string.data(), url_string.size(), /*seed=*/1));
}


namespace
{

struct DownloadedAsset
{
	bool ok;
	std::string local_path; // Path of the file in the resources dir.
	URLString resource_URL; // The file's URL in the resource system.
	std::string skip_reason;

	// For images:
	size_t width, height;
	bool has_alpha;
};


// Checks that a downloaded file is a valid image or .vox file, and for an image, gets its size and whether it has alpha.  Throws glare::Exception if not valid.
static void readAssetInfo(const std::string& path, bool is_image, DownloadedAsset& asset)
{
	if(is_image)
	{
		Map2DRef map = ImFormatDecoder::decodeImage(".", path); // Throws if the image is invalid.
		asset.width  = map->getMapWidth();
		asset.height = map->getMapHeight();
		asset.has_alpha = LODGeneration::textureHasAlphaChannel(path, map);
	}
	else
	{
		if(!FormatDecoderVox::isValidVoxFile(path))
			throw glare::Exception("the .vox file is invalid");
	}
}


// Registers an HTTPClient with the stopper for the lifetime of the scope, so VoxelsParcelImportStopper::stop() can interrupt its download.
struct CurrentHTTPClientScope
{
	CurrentHTTPClientScope(VoxelsParcelImportStopper* stopper_, const Reference<HTTPClient>& client) : stopper(stopper_) { if(stopper) stopper->setCurrentHTTPClient(client); }
	~CurrentHTTPClientScope() { if(stopper) stopper->setCurrentHTTPClient(NULL); }

	VoxelsParcelImportStopper* stopper;
};


// Thrown for things that end the whole import rather than just skipping a feature: being stopped, or a host still rate limiting us after
// all the retries, in which case carrying on would just skip every later asset from that host.
class ImportAbortedExcep : public glare::Exception
{
public:
	ImportAbortedExcep(const std::string& s) : glare::Exception(s) {}
};


struct FeatureTypeCounts
{
	FeatureTypeCounts() : num_imported(0), num_skipped(0) {}
	size_t num_imported;
	size_t num_skipped;
	std::string skip_reason; // Reason for the first skipped feature of this type.
};


class ImportContext
{
public:
	GUIClient* gui_client;
	CreateObjectTracker* tracker;
	VoxelsParcelImportResults* results;

	std::string temp_dir;
	std::map<std::string, DownloadedAsset> assets; // Keyed by the URL stored in the feature.

	UID group_uid;
	uint64 first_create_token;
	uint64 end_create_token;

	// Our own packet buffer: GUIClient::scratch_packet belongs to the main thread.
	SocketBufferOutStream packet;

	VoxelsParcelImportStopper* stopper; // May be null.
	ThreadSafeQueue<std::string>* log_queue; // May be null.

	std::map<std::string, Timer> time_since_download_from_host; // Time since the last download from each host finished, keyed by host.

	// Image URLs for nft-image features, keyed by the token's OpenSea page URL.  Empty if the image URL couldn't be found.
	std::map<std::string, std::string> nft_image_url_for_page;

	std::string progress_prefix; // e.g. "[feature 34/1021] ", put in front of log lines for downloads.

	// For converting Voxels teleport links to Substrata URLs.
	Vec3d origin; // See VoxelsParcelImportOptions::origin.
	bool have_voxels_parcel_base_centre;
	Vec3d voxels_parcel_base_centre; // The parcel's horizontal centre at its base height, in Voxels world coordinates (x, height, z), when have_voxels_parcel_base_centre is set.
	std::string sub_URL_prefix; // e.g. "sub://substrata.info/" for the connected server and world.

	ImportContext() : packet(SocketBufferOutStream::DontUseNetworkByteOrder), first_create_token(0), end_create_token(0), stopper(NULL), log_queue(NULL), origin(0,0,0), have_voxels_parcel_base_centre(false), voxels_parcel_base_centre(0,0,0) {}


	// Writes to the log window, via GUIClient's message queue since we're not on the main thread, and to the console.
	void log(const std::string& msg)
	{
		conPrint(msg);
		gui_client->msg_queue.enqueue(new LogMessage(msg));
		if(log_queue)
			log_queue->enqueue(msg);
	}


	void checkShouldStop()
	{
		if(stopper && stopper->shouldStop())
			throw ImportAbortedExcep("Import stopped.");
	}


	void sleepUnlessStopped(double wait_s)
	{
		Timer timer;
		while(timer.elapsed() < wait_s)
		{
			checkShouldStop();
			PlatformUtils::Sleep(100);
		}
	}


	// Downloads download_url, leaving a gap after the previous download from the same host so as not to hammer it (typically all of a parcel's
	// assets are on one host, such as Dropbox, which throttles clients that download a lot), retrying with increasing waits while the host rate
	// limits us, and retrying a couple of times on network errors.
	// Throws ImportAbortedExcep if the host is still rate limiting us after all the retries.
	// transfer_time_out is set to how long the successful request took, not counting the waits before it.
	void downloadWithRetries(const std::string& download_url, std::string& data_out, double& transfer_time_out)
	{
		const double MIN_GAP_BETWEEN_DOWNLOADS_FROM_HOST_S = 0.0; // Disabled for now: it slows down downloading many small files, and doesn't seem to stop Dropbox throttling large ones.
		const int MAX_RATE_LIMITED_RETRIES = 8; // Waits of 5, 10, 20, 40, 80, 120, 120, 120 s, about 8.5 minutes in all, unless the host gives a Retry-After.
		const int MAX_NETWORK_ERROR_RETRIES = 2;

		const std::string host = ::toLowerCase(URL::parseURL(download_url).host);

		int num_rate_limited_retries = 0;
		int num_network_error_retries = 0;
		while(1)
		{
			auto last_download = time_since_download_from_host.find(host);
			if(last_download != time_since_download_from_host.end())
			{
				const double wait_s = MIN_GAP_BETWEEN_DOWNLOADS_FROM_HOST_S - last_download->second.elapsed();
				if(wait_s > 0)
				{
					log("    Waiting " + doubleToStringNDecimalPlaces(wait_s, 1) + " s before starting download...");
					sleepUnlessStopped(wait_s);
				}
			}
			checkShouldStop();

			HTTPClient::ResponseInfo response;
			try
			{
				Reference<HTTPClient> client = new HTTPClient();
				client->max_data_size = 256 * 1024 * 1024; // Some GIFs are over 80 MB.
				client->max_socket_buffer_size = 256 * 1024 * 1024;

				// Let the stopper interrupt the download.  Check for a stop requested just before the client was registered, which it wouldn't have interrupted.
				CurrentHTTPClientScope client_scope(stopper, client);
				checkShouldStop();

				data_out.clear();
				try
				{
					Timer transfer_timer;
					response = client->downloadFile(download_url, data_out);
					transfer_time_out = transfer_timer.elapsed();
				}
				catch(glare::Exception&)
				{
					time_since_download_from_host[host].reset(); // A failed download counts too.
					throw;
				}
				time_since_download_from_host[host].reset();
			}
			catch(ImportAbortedExcep&)
			{
				throw;
			}
			catch(glare::Exception& e)
			{
				checkShouldStop(); // An interrupted download fails with a socket error, which isn't worth retrying if we were stopped.

				if(num_network_error_retries >= MAX_NETWORK_ERROR_RETRIES)
					throw glare::Exception("download failed: " + e.what());
				num_network_error_retries++;
				log("    Download failed (" + e.what() + "), retrying in 2 s...");
				sleepUnlessStopped(2.0);
				continue;
			}

			// 429 is Too Many Requests.  503 is Service Unavailable, which some hosts send when overloaded.
			if(response.response_code == 429 || response.response_code == 503)
			{
				if(num_rate_limited_retries >= MAX_RATE_LIMITED_RETRIES)
					throw ImportAbortedExcep("The host is still sending HTTP " + toString(response.response_code) + " " + response.response_message + " for " + download_url +
						" after " + toString(MAX_RATE_LIMITED_RETRIES) + " retries.  Try the import again later.");

				double wait_s = myMin(5.0 * (1 << num_rate_limited_retries), 120.0);

				// Use the host's Retry-After if it gives a number of seconds.  A wait of hours, as for a daily bandwidth limit, isn't worth sitting through.
				if(!response.retry_after.empty())
				{
					try
					{
						const uint64 retry_after_s = stringToUInt64(response.retry_after);
						const uint64 MAX_RETRY_AFTER_S = 600;
						if(retry_after_s > MAX_RETRY_AFTER_S)
							throw ImportAbortedExcep("The host is rate limiting downloads (HTTP " + toString(response.response_code) + ") and asks us to wait " +
								toString(retry_after_s) + " s before retrying " + download_url + ".  Try the import again later.");
						wait_s = myMax(1.0, (double)retry_after_s);
					}
					catch(StringUtilsExcep&)
					{} // An HTTP date rather than a number of seconds: keep our own wait.
				}

				num_rate_limited_retries++;
				log("    Rate limited (HTTP " + toString(response.response_code) + "), waiting " + toString((int)wait_s) + " s before retrying...");
				sleepUnlessStopped(wait_s);
				continue;
			}

			if(response.response_code != 200)
				throw glare::Exception("download failed with HTTP " + toString(response.response_code) + " " + response.response_message);

			return;
		}
	}


	// Returns the asset for a URL stored in a feature, downloading it and putting it in the resource system the first time it is asked for.
	// A URL that can't be used is remembered as such, with the reason, and isn't tried again.
	const DownloadedAsset& getOrDownloadAsset(const std::string& url, bool want_image)
	{
		auto res = assets.find(url);
		if(res != assets.end())
			return res->second;

		DownloadedAsset asset;
		asset.ok = false;
		asset.width = asset.height = 0;
		asset.has_alpha = false;

		try
		{
			const std::string download_url = getDownloadURL(url); // Throws for URLs we don't download, such as ones hosted by Voxels.

			ResourceManager& resource_manager = *gui_client->resource_manager;

			// Look for the asset from an earlier download, under each resource URL it could have.  See resourceURLForSourceURL().
			const std::vector<std::string> possible_extensions = want_image ? std::vector<std::string>({"png", "jpg", "gif", "webp"}) : std::vector<std::string>({"vox"});
			for(size_t i=0; i<possible_extensions.size(); ++i)
			{
				const URLString resource_URL = resourceURLForSourceURL(url, possible_extensions[i]);
				if(resource_manager.isFileForURLPresent(resource_URL))
				{
					asset.resource_URL = resource_URL;
					asset.local_path = resource_manager.pathForURLForPresentResource(resource_URL);
					break;
				}
			}

			if(!asset.resource_URL.empty())
			{
				readAssetInfo(asset.local_path, want_image, asset);
				log("Already downloaded " + url + " -> " + asset.local_path);
			}
			else
			{
				log(progress_prefix + "Downloading " + download_url + " ...");

				std::string data;
				double download_time;
				downloadWithRetries(download_url, data, download_time);

				const std::string extension = sniffExtension(data);
				if(want_image && !(extension == "png" || extension == "jpg" || extension == "gif" || extension == "webp"))
					throw glare::Exception("the download isn't a PNG, JPEG, GIF or WebP image");
				if(!want_image && extension != "vox")
					throw glare::Exception("the download isn't a .vox file");

				asset.resource_URL = resourceURLForSourceURL(url, extension);

				// Check the file before putting it in the resource system, so a bad download isn't found and used by later imports.
				const std::string temp_path = temp_dir + "/" + toStdString(asset.resource_URL);
				FileUtils::writeEntireFile(temp_path, data);
				readAssetInfo(temp_path, want_image, asset);

				resource_manager.copyLocalFileToResourceDir(temp_path, asset.resource_URL);
				asset.local_path = resource_manager.pathForURLForPresentResource(asset.resource_URL);
				results->num_assets_downloaded++;

				log("    Got " + toString(data.size() / 1024) + " KB in " + doubleToStringNSigFigs(download_time, 3) + " s (" +
					toString((int)(data.size() / 1024 / myMax(download_time, 0.001))) + " KB/s)" + (want_image ? (", " + toString(asset.width) + " x " + toString(asset.height)) : std::string()) +
					" -> " + asset.local_path);
			}

			asset.ok = true;
		}
		catch(ImportAbortedExcep&)
		{
			throw;
		}
		catch(glare::Exception& e)
		{
			asset.skip_reason = e.what();
			results->skipped_assets.push_back(url + ": " + asset.skip_reason);
			log("Not using " + (url.empty() ? std::string("(no URL)") : url) + ": " + asset.skip_reason);
		}

		return assets.insert(std::make_pair(url, asset)).first->second;
	}


	// Gets the image URL of the NFT whose OpenSea page is page_url, such as https://opensea.io/assets/ethereum/<contract>/<token id>, from OpenSea's
	// metadata for the token, which doesn't need an API key.  Throws glare::Exception on failure.
	const std::string getNFTImageURL(const std::string& page_url)
	{
		auto res = nft_image_url_for_page.find(page_url);
		if(res != nft_image_url_for_page.end())
		{
			if(res->second.empty())
				throw glare::Exception("couldn't get the NFT's image URL");
			return res->second;
		}

		try
		{
			const URL url = URL::parseURL(page_url);
			const std::string host = ::toLowerCase(url.host);
			if(host != "opensea.io" && host != "www.opensea.io")
				throw glare::Exception("not an OpenSea URL");

			// Paths are /assets/<chain>/<contract>/<token id> or /item/<chain>/<contract>/<token id>, or the older /assets/<contract>/<token id>, which is on Ethereum.
			const std::vector<std::string> parts = ::split(url.path, '/'); // parts[0] is the empty string before the leading slash.
			std::string chain, contract, token_id;
			if(parts.size() == 5 && (parts[1] == "assets" || parts[1] == "item"))
			{
				chain = parts[2];
				contract = parts[3];
				token_id = parts[4];
			}
			else if(parts.size() == 4 && parts[1] == "assets")
			{
				chain = "ethereum";
				contract = parts[2];
				token_id = parts[3];
			}
			else
				throw glare::Exception("couldn't find the contract and token ID in the URL");

			// Check the parts before putting them in the metadata URL.
			if(chain.empty())
				throw glare::Exception("invalid chain");
			for(size_t i=0; i<chain.size(); ++i)
				if(!(::isAlphaNumeric(chain[i]) || chain[i] == '_' || chain[i] == '-'))
					throw glare::Exception("invalid chain");
			if(contract.size() != 42 || !::hasPrefix(contract, "0x"))
				throw glare::Exception("invalid contract address");
			try
			{
				for(size_t i=2; i<contract.size(); ++i)
					::hexCharToUInt(contract[i]); // Throws StringUtilsExcep if not a hex digit.
			}
			catch(StringUtilsExcep&)
			{
				throw glare::Exception("invalid contract address");
			}
			if(token_id.empty() || token_id.size() > 80)
				throw glare::Exception("invalid token ID");
			for(size_t i=0; i<token_id.size(); ++i)
				if(!::isNumeric(token_id[i]))
					throw glare::Exception("invalid token ID");

			const std::string metadata_url = "https://api.opensea.io/api/v2/metadata/" + chain + "/" + contract + "/" + token_id;
			log(progress_prefix + "Getting NFT metadata " + metadata_url + " ...");

			std::string data;
			double transfer_time;
			downloadWithRetries(metadata_url, data, transfer_time);

			JSONParser parser;
			parser.parseBuffer(data.data(), data.size());
			if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
				throw glare::Exception("the metadata isn't a JSON object");
			const std::string image_url = getStringField(parser, parser.nodes[0], "image");
			if(image_url.empty())
				throw glare::Exception("the metadata has no image");

			nft_image_url_for_page[page_url] = image_url;
			return image_url;
		}
		catch(ImportAbortedExcep&)
		{
			throw;
		}
		catch(glare::Exception& e)
		{
			nft_image_url_for_page[page_url] = std::string();
			log("Couldn't get the image for NFT " + page_url + ": " + e.what());
			throw;
		}
	}


	// Makes the object link to the page in the feature's 'link' field, if it has one, so users can visit it.  A Voxels teleport link becomes a
	// Substrata URL for the same place in the imported parcel, or is left out if the parcel's position in the Voxels world isn't known.
	void setTargetURLFromLink(const JSONParser& parser, const JSONNode& feature, WorldObject& ob)
	{
		const std::string link = getStringField(parser, feature, "link");
		if(link.empty())
			return;

		std::string target_url = link;
		try
		{
			VoxelsCoords coords;
			if(getVoxelsTeleportCoords(link, coords))
			{
				if(!have_voxels_parcel_base_centre)
					return; // Logged once, when working out the parcel position.

				// Relative to the parcel's base centre, then to Substrata space.  URL positions are eye positions, so add the eye height to the feet height.
				const double EYE_HEIGHT = 1.67;
				const Vec3d pos = origin + voxelsToSubstrataPos(Vec3d(coords.x, coords.height, coords.z) - voxels_parcel_base_centre) + Vec3d(0, 0, EYE_HEIGHT);

				// Voxels x and z map to Substrata -x and -y, so facing East (+x) is a heading of 180 degrees, and North (+z) 270.
				double heading = 0;
				if(coords.facing == 'E')      heading = 180;
				else if(coords.facing == 'N') heading = 270;
				else if(coords.facing == 'S') heading = 90;

				target_url = sub_URL_prefix + "?x=" + doubleToStringNDecimalPlaces(pos.x, 1) + "&y=" + doubleToStringNDecimalPlaces(pos.y, 1) +
					"&z=" + doubleToStringNDecimalPlaces(pos.z, 2) + "&heading=" + doubleToStringNDecimalPlaces(heading, 1);
			}
		}
		catch(glare::Exception& e)
		{
			log("Leaving out link '" + link + "': " + e.what());
			return;
		}

		if(target_url.size() <= WorldObject::MAX_URL_SIZE)
			ob.target_url = target_url;
	}


	void sendCreateObject(WorldObject& ob)
	{
		ob.group_id = group_uid;

		// The server assigns the UID, and echoes back the token we put in the UID field.  See Protocol::CreateObjectResponse.
		const uint64 create_token = tracker->makeToken();
		ob.uid = UID(create_token);

		MessageUtils::initPacket(packet, Protocol::CreateObject);
		ob.writeToNetworkStream(packet);
		MessageUtils::updatePacketLengthField(packet);
		gui_client->client_thread->enqueueDataToSend(packet.buf);

		if(first_create_token == 0)
			first_create_token = create_token;
		end_create_token = create_token + 1;

		results->num_objects_sent++;
	}
};

} // end anonymous namespace


// Makes a voxel group object from voxels given in a local frame where each voxel (i, j, k) covers local_origin + voxel_size * [i, i+1] x [j, j+1] x [k, k+1],
// then rotated by rot about the local frame's origin, which is at centre_ws.
static WorldObjectRef makeVoxelObject(const std::vector<Voxel>& voxels, const std::vector<WorldMaterialRef>& materials, const Vec3d& centre_ws, const Matrix4f& rot,
	const Vec3f& local_origin, const Vec3f& voxel_size)
{
	WorldObjectRef ob = new WorldObject();
	ob->object_type = WorldObject::ObjectType_VoxelGroup;
	ob->materials = materials;

	ob->getDecompressedVoxels().resize(voxels.size());
	for(size_t i=0; i<voxels.size(); ++i)
		ob->getDecompressedVoxels()[i] = voxels[i];
	ob->compressVoxels();
	ob->max_model_lod_level = (voxels.size() > 256) ? 2 : 0;

	ob->scale = voxel_size;
	setAxisAndAngle(rot, *ob);
	const Vec4f origin_offset_ws = rot * Vec4f(local_origin.x, local_origin.y, local_origin.z, 0);
	ob->pos = centre_ws + Vec3d(origin_offset_ws[0], origin_offset_ws[1], origin_offset_ws[2]);

	ob->setAABBOS(ob->getDecompressedVoxelGroup().getAABB());
	return ob;
}


static void makeParcelVoxelsObject(ImportContext& context, const std::vector<uint16>& voxel_data, int X, int Y, int Z, const std::vector<Colour3f>& palette,
	const Vec3d& origin)
{
	// Voxel values: bits 0-3 are the tileset texture index, bits 5-7 the palette colour index, and bit 15 is set for opaque voxels.
	// The tileset isn't imported, so each combination of tile, colour and opacity gets its own material, for the user to put the tile's texture on.
	// Materials are ordered by tile, then colour, then opacity, and the log lists which is which, since material names aren't saved.
	struct MatKey
	{
		int tile_index, colour_index;
		bool transparent;
		bool operator < (const MatKey& b) const
		{
			if(tile_index != b.tile_index) return tile_index < b.tile_index;
			if(colour_index != b.colour_index) return colour_index < b.colour_index;
			return transparent < b.transparent;
		}
	};
	const auto keyForVoxel = [](uint16 v)
	{
		MatKey key;
		key.tile_index = v & 0xF;
		key.colour_index = (v >> 5) & 0x7;
		key.transparent = (((v >> 15) & 0x1) == 0) && (key.colour_index == 0);
		return key;
	};

	// First pass: find the combinations used, so their materials can be put in order.
	std::map<MatKey, int> mat_index_for_key;
	for(size_t i=0; i<voxel_data.size(); ++i)
		if(voxel_data[i] != 0)
			mat_index_for_key[keyForVoxel(voxel_data[i])] = 0;

	std::vector<WorldMaterialRef> materials;
	for(auto it = mat_index_for_key.begin(); it != mat_index_for_key.end(); ++it)
	{
		WorldMaterialRef mat = new WorldMaterial();
		mat->colour_rgb = palette[it->first.colour_index];
		if(it->first.transparent)
			mat->opacity.val = 0.2f;

		it->second = (int)materials.size();
		materials.push_back(mat);
	}

	std::vector<Voxel> voxels;
	size_t read_i = 0;
	for(int rx=0; rx<X; ++rx)
	for(int ry=0; ry<Y; ++ry)
	for(int rz=0; rz<Z; ++rz)
	{
		const uint16 v = voxel_data[read_i++];
		if(v == 0)
			continue;

		// Voxels x and z map to Substrata -x and -y, so voxel rx covers Substrata local x in [-rx-1, -rx] (in voxel units).
		voxels.push_back(Voxel(Vec3<int>(-rx - 1, -rz - 1, ry), mat_index_for_key[keyForVoxel(v)]));
	}

	if(voxels.empty())
		return;

	context.log("Parcel voxel materials (put the textures of the parcel's tiles on these in the object editor):");
	for(auto it = mat_index_for_key.begin(); it != mat_index_for_key.end(); ++it)
		context.log("    Material " + toString(it->second) + ": tile " + toString(it->first.tile_index) + ", palette colour " + toString(it->first.colour_index) +
			(it->first.transparent ? ", transparent" : ""));

	// See the coordinate system notes at the top of the file.
	const Vec3d grid_pos = origin + Vec3d(X * 0.25 - 0.25, Z * 0.25 - 0.25, -0.25);

	WorldObjectRef ob = makeVoxelObject(voxels, materials, grid_pos, Matrix4f::identity(), Vec3f(0.f), Vec3f(0.5f));
	ob->content = "Voxels parcel voxels";
	context.sendCreateObject(*ob);

	context.log("Parcel voxels: " + toString(voxels.size()) + " voxels, " + toString(materials.size()) + " materials.");
}


static void makeCubeObject(ImportContext& context, const JSONParser& parser, const JSONNode& feature, const Vec3d& origin)
{
	const Vec3d position = getVec3Field(parser, feature, "position", Vec3d(0.0));
	const Vec3d rotation = getVec3Field(parser, feature, "rotation", Vec3d(0.0));
	const Vec3d scale    = getVec3Field(parser, feature, "scale",    Vec3d(1.0));

	WorldMaterialRef mat = new WorldMaterial();
	mat->colour_rgb = parseHexColour(getStringField(parser, feature, "color"), Colour3f(1.f));

	const std::string url = getStringField(parser, feature, "url");
	if(!url.empty())
	{
		const DownloadedAsset& asset = context.getOrDownloadAsset(url, /*want_image=*/true);
		if(asset.ok)
		{
			mat->colour_texture_url = asset.resource_URL;
			BitUtils::setOrZeroBit(mat->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, asset.has_alpha);
			BitUtils::setOrZeroBit(mat->flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1, asset.width > 1024 || asset.height > 1024);
		}
	}

	// The built-in unit cube, which covers [-0.5, 0.5]^3, so the scale is the size of the box.  Voxels x, y and z extents map to Substrata x, z and y.
	WorldObjectRef ob = new WorldObject();
	ob->object_type = WorldObject::ObjectType_Generic;
	ob->model_url = "Cube_obj_12971581758459554602.bmesh";
	ob->max_model_lod_level = 0;
	ob->materials.push_back(mat);
	ob->pos = origin + voxelsToSubstrataPos(position);
	ob->scale = Vec3f((float)std::fabs(nonZeroScale(scale.x)), (float)std::fabs(nonZeroScale(scale.z)), (float)std::fabs(nonZeroScale(scale.y)));
	setAxisAndAngle(featureRotation(rotation), *ob);
	ob->setAABBOS(js::AABBox(Vec4f(-0.5f, -0.5f, -0.5f, 1), Vec4f(0.5f, 0.5f, 0.5f, 1)));

	ob->setCollidable(getBoolField(parser, feature, "collidable", true));
	context.setTargetURLFromLink(parser, feature, *ob);
	checkTransformOK(ob.ptr());
	context.sendCreateObject(*ob);
}


// For image and nft-image features.  target_url, if not empty, is the page the object links to.
static void makeImageObject(ImportContext& context, const JSONParser& parser, const JSONNode& feature, const Vec3d& origin, const std::string& image_url, const std::string& target_url)
{
	const DownloadedAsset& asset = context.getOrDownloadAsset(image_url, /*want_image=*/true);
	if(!asset.ok)
		throw glare::Exception(asset.skip_reason);

	const Vec3d position = getVec3Field(parser, feature, "position", Vec3d(0.0));
	const Vec3d rotation = getVec3Field(parser, feature, "rotation", Vec3d(0.0));
	const Vec3d scale    = getVec3Field(parser, feature, "scale",    Vec3d(1.0));

	// The image fills a plane of scale.x by scale.y in the feature's local x-y plane, seen from its -z side.  Unless stretched, it keeps its
	// aspect ratio and is fitted inside that.
	float w = (float)std::fabs(nonZeroScale(scale.x));
	float h = (float)std::fabs(nonZeroScale(scale.y));
	if(!getBoolField(parser, feature, "stretch", false) && asset.width > 0 && asset.height > 0)
	{
		const float image_aspect = (float)asset.width / (float)asset.height;
		if(image_aspect > w / h)
			h = w / image_aspect;
		else
			w = h * image_aspect;
	}
	const float depth = 0.01f;

	// The image cube mesh covers [0,1]^3, with the image on its y = 0 face, seen from -y.  Voxels local -z, which the image is seen from, is
	// Substrata local +y, so turn the cube around z to face it that way.  The face is put slightly in front of the plane so it isn't lost
	// in a wall the plane is on.
	const Matrix4f rot = featureRotation(rotation) * Matrix4f::rotationAroundZAxis(Maths::pi<float>());
	const Vec4f offset_ws = rot * Vec4f(-w / 2, -0.01f, -h / 2, 0);

	WorldObjectRef ob = new WorldObject();
	ob->object_type = WorldObject::ObjectType_Generic;
	ob->model_url = "image_cube_5438347426447337425.bmesh";
	ob->max_model_lod_level = 0;
	ob->pos = origin + voxelsToSubstrataPos(position) + Vec3d(offset_ws[0], offset_ws[1], offset_ws[2]);
	ob->scale = Vec3f(w, depth, h);
	setAxisAndAngle(rot, *ob);
	ob->setAABBOS(js::AABBox(Vec4f(0,0,0,1), Vec4f(1,1,1,1)));
	ob->setCollidable(getBoolField(parser, feature, "collidable", false));
	if(target_url.size() <= WorldObject::MAX_URL_SIZE)
		ob->target_url = target_url;
	context.setTargetURLFromLink(parser, feature, *ob); // A link the builder set takes precedence.

	ob->materials.resize(2);
	ob->materials[0] = new WorldMaterial(); // Front and back faces
	ob->materials[0]->colour_rgb = Colour3f(1.f);
	ob->materials[0]->colour_texture_url = asset.resource_URL;
	const bool transparent = getBoolField(parser, feature, "transparent", true); // Only an explicit false turns transparency off.
	BitUtils::setOrZeroBit(ob->materials[0]->flags, WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG, asset.has_alpha && transparent);
	BitUtils::setOrZeroBit(ob->materials[0]->flags, WorldMaterial::MIN_LOD_LEVEL_IS_NEGATIVE_1, asset.width > 1024 || asset.height > 1024);
	ob->materials[1] = new WorldMaterial(); // Edges
	ob->materials[1]->colour_rgb = Colour3f(0.7f);

	checkTransformOK(ob.ptr());
	context.sendCreateObject(*ob);
}


// An nft-image feature's url is the token's OpenSea page, such as https://opensea.io/assets/ethereum/<contract>/<token id>.  The image is found
// from OpenSea's metadata for the token, and the object links to the OpenSea page.
static void makeNFTImageObject(ImportContext& context, const JSONParser& parser, const JSONNode& feature, const Vec3d& origin)
{
	const std::string page_url = getStringField(parser, feature, "url");
	const std::string image_url = context.getNFTImageURL(page_url);

	makeImageObject(context, parser, feature, origin, image_url, /*target_url=*/page_url);
}


// For vox-model and megavox features, which differ in how large a model Voxels allows, not in how it is placed.
static void makeVoxModelObject(ImportContext& context, const JSONParser& parser, const JSONNode& feature, const Vec3d& origin)
{
	const DownloadedAsset& asset = context.getOrDownloadAsset(getStringField(parser, feature, "url"), /*want_image=*/false);
	if(!asset.ok)
		throw glare::Exception(asset.skip_reason);

	VoxFileContents vox_contents;
	FormatDecoderVox::loadModel(asset.local_path, vox_contents);
	if(vox_contents.models.empty())
		throw glare::Exception("the .vox file has no model in it");
	const VoxModel& model = vox_contents.models[0];

	const Vec3d position = getVec3Field(parser, feature, "position", Vec3d(0.0));
	const Vec3d rotation = getVec3Field(parser, feature, "rotation", Vec3d(0.0));
	const Vec3d scale    = getVec3Field(parser, feature, "scale",    Vec3d(1.0));
	const bool flip_x    = getBoolField(parser, feature, "flipX", false);

	// A voxel is 0.02 m at scale 1.  The model is centred horizontally on the feature position, with its base at the position.
	//
	// .vox files are z-up.  In Voxels local space a .vox voxel (vx, vy, vz) goes to x = vx, y = vz, z = -vy, each scaled, with x negated for
	// flipX, so in Substrata local space it is at (-x, -z, y).  For each Substrata axis, ex etc. give the extent of a voxel, negative where
	// the axis runs opposite to the .vox axis it comes from.
	// Without flipX, that mapping mirrors the model, which flipX undoes, making the mapping a plain rotation.
	const double voxel_size = 0.02;
	const double ex = -voxel_size * nonZeroScale(scale.x) * (flip_x ? -1 : 1);
	const double ey =  voxel_size * nonZeroScale(scale.z);
	const double ez =  voxel_size * nonZeroScale(scale.y);

	// Each voxel goes at the index along each axis that gives the same extent with a positive voxel size.  Mirroring this way, rather than with a
	// negative object scale, keeps the faces the right way out.
	std::vector<Voxel> voxels(model.voxels.size());
	for(size_t i=0; i<model.voxels.size(); ++i)
	{
		const VoxVoxel& v = model.voxels[i];
		voxels[i] = Voxel(Vec3<int>(
			(ex > 0) ? v.x : (model.size_x - 1 - v.x),
			(ey > 0) ? v.y : (model.size_y - 1 - v.y),
			(ez > 0) ? v.z : (model.size_z - 1 - v.z)), v.mat_index);
	}

	// The model is centred on x and y, and sits on z = 0, or hangs below it for a negative y scale.
	const Vec3f local_origin(
		(float)(-std::fabs(ex) * model.size_x / 2),
		(float)(-std::fabs(ey) * model.size_y / 2),
		(ez > 0) ? 0.f : (float)(-std::fabs(ez) * model.size_z));

	std::vector<WorldMaterialRef> materials(vox_contents.used_materials.size());
	for(size_t i=0; i<materials.size(); ++i)
	{
		const VoxMaterial& vox_mat = vox_contents.used_materials[i];
		materials[i] = new WorldMaterial();
		materials[i]->colour_rgb = Colour3f(vox_mat.col_from_palette[0], vox_mat.col_from_palette[1], vox_mat.col_from_palette[2]);
	}

	WorldObjectRef ob = makeVoxelObject(voxels, materials, origin + voxelsToSubstrataPos(position), featureRotation(rotation), local_origin,
		Vec3f((float)std::fabs(ex), (float)std::fabs(ey), (float)std::fabs(ez)));

	ob->setCollidable(getBoolField(parser, feature, "collidable", false));
	context.setTargetURLFromLink(parser, feature, *ob);
	checkTransformOK(ob.ptr());
	context.sendCreateObject(*ob);
}


// Finds this user's group with the given name, empties it, and moves it to pos, or makes a new group there if there isn't one.
static void getEmptyGroup(ImportContext& context, const std::string& name, const Vec3d& pos)
{
	GUIClient* gui_client = context.gui_client;

	// Lowest UID wins, as in getOrCreateGroup() in LuaBuildScript.cpp.
	UID group_uid = UID::invalidUID();
	std::vector<UID> uids_to_delete;
	{
		Lock lock(gui_client->world_state->mutex);

		for(auto it = gui_client->world_state->objects.valuesBegin(); it != gui_client->world_state->objects.valuesEnd(); ++it)
		{
			const WorldObject* ob = it.getValue().ptr();
			if((ob->object_type == WorldObject::ObjectType_Group) && (ob->creator_id == gui_client->logged_in_user_id) && (ob->content == name))
				if(ob->uid < group_uid)
					group_uid = ob->uid;
		}

		// Collect the members, including the members of member groups.
		if(group_uid.valid())
		{
			std::vector<UID> groups_to_scan(1, group_uid);
			for(size_t g=0; g<groups_to_scan.size(); ++g) // NOTE: groups_to_scan grows as sub-groups are found.
			{
				for(auto it = gui_client->world_state->objects.valuesBegin(); it != gui_client->world_state->objects.valuesEnd(); ++it)
				{
					const WorldObject* ob = it.getValue().ptr();
					if((ob->group_id == groups_to_scan[g]) && (ob->creator_id == gui_client->logged_in_user_id))
					{
						uids_to_delete.push_back(ob->uid);
						if((ob->object_type == WorldObject::ObjectType_Group) && (std::find(groups_to_scan.begin(), groups_to_scan.end(), ob->uid) == groups_to_scan.end()))
							groups_to_scan.push_back(ob->uid);
					}
				}
			}
		}
	}

	if(group_uid.valid())
	{
		for(size_t i=0; i<uids_to_delete.size(); ++i)
		{
			MessageUtils::initPacket(context.packet, Protocol::DestroyObject);
			writeToStream(uids_to_delete[i], context.packet);
			MessageUtils::updatePacketLengthField(context.packet);
			gui_client->client_thread->enqueueDataToSend(context.packet.buf);
		}
		context.results->num_objects_deleted = uids_to_delete.size();
		context.log("Reusing group '" + name + "', removed the " + toString(uids_to_delete.size()) + " objects from the previous import.");

		MessageUtils::initPacket(context.packet, Protocol::ObjectTransformUpdate);
		writeToStream(group_uid, context.packet);
		writeToStream(pos, context.packet);
		writeToStream(Vec3f(0,0,1), context.packet);
		context.packet.writeFloat(0.f);
		writeToStream(Vec3f(1,1,1), context.packet);
		MessageUtils::updatePacketLengthField(context.packet);
		gui_client->client_thread->enqueueDataToSend(context.packet.buf);

		context.group_uid = group_uid;
		return;
	}

	// Make a new group object.  It has no mesh, so it gets a zero-size AABB at its position, and isn't collidable.
	WorldObjectRef ob = new WorldObject();
	ob->object_type = WorldObject::ObjectType_Group;
	ob->content = name;
	ob->pos = pos;
	ob->axis = Vec3f(0,0,1);
	ob->angle = 0;
	ob->scale = Vec3f(1,1,1);
	ob->setAABBOS(js::AABBox(Vec4f(0,0,0,1), Vec4f(0,0,0,1)));
	ob->setCollidable(false);

	context.group_uid = UID::invalidUID(); // Not in a group itself.
	context.sendCreateObject(*ob);

	// The members need the UID the server assigns, so wait for the group object to arrive in the world state.  There was no group of ours
	// with this name above, so the one that turns up is the one we just made.
	const double timeout_s = 10;
	Timer wait_timer;
	while(1)
	{
		{
			Lock lock(gui_client->world_state->mutex);

			for(auto it = gui_client->world_state->objects.valuesBegin(); it != gui_client->world_state->objects.valuesEnd(); ++it)
			{
				const WorldObject* world_ob = it.getValue().ptr();
				if((world_ob->object_type == WorldObject::ObjectType_Group) && (world_ob->creator_id == gui_client->logged_in_user_id) && (world_ob->content == name))
				{
					context.group_uid = world_ob->uid;
					break;
				}
			}
		}

		if(context.group_uid.valid())
			break;
		if(wait_timer.elapsed() > timeout_s)
			throw glare::Exception("The group object for '" + name + "' didn't arrive from the server within " + toString((int)timeout_s) + " s.  The server may have refused it.");
		context.checkShouldStop();
		PlatformUtils::Sleep(10);
	}

	context.log("Made group '" + name + "'.");
}


void VoxelsParcelImporter::importParcel(GUIClient* gui_client, const std::string& json_path, const VoxelsParcelImportOptions& options,
	VoxelsParcelImportResults& results_out)
{
	Timer timer;
	results_out = VoxelsParcelImportResults();

	// Voxels builds usually have their bottom layer of voxels below the ground, so put the parcel one voxel (0.5 m) lower than options.origin.
	const Vec3d origin = options.origin - Vec3d(0, 0, 0.5);

	if(!gui_client || gui_client->client_thread.isNull() || gui_client->world_state.isNull())
		throw glare::Exception("Not connected to a server.");
	if(!gui_client->logged_in_user_id.valid())
		throw glare::Exception("You need to be logged in to import a parcel.");

	ImportContext context;
	context.gui_client = gui_client;
	context.tracker = gui_client->create_object_tracker.ptr();
	context.stopper = options.stopper;
	context.log_queue = options.log_queue;
	context.results = &results_out;
	context.temp_dir = PlatformUtils::getTempDirPath() + "/voxels_parcel_import";
	FileUtils::createDirIfDoesNotExist(context.temp_dir);

	context.log("Importing Voxels parcel from " + json_path + " ...");

	JSONParser parser;
	parser.parseFile(json_path);
	if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
		throw glare::Exception("The parcel file isn't a JSON object.");

	// A /api/parcels/N.json response has the parcel in a 'parcel' object, with its bounds.  A snapshot has the content at the top level and no bounds.
	const JSONNode& root = parser.nodes[0];
	const bool is_api_response = root.hasChild("parcel");
	const JSONNode& parcel_node = is_api_response ? root.getChildObject(parser, "parcel") : root;
	const JSONNode& content_node = parcel_node.getChildObject(parser, "content");

	const int parcel_id = is_api_response ? (int)getNumberField(parser, parcel_node, "id") :
		(parcel_node.hasChild("parcel_id") ? (int)getNumberField(parser, parcel_node, "parcel_id") : 0);
	const std::string parcel_name = getStringField(parser, parcel_node, "name");

	results_out.group_name = "Voxels parcel " + toString(parcel_id) + (parcel_name.empty() ? std::string() : (": " + parcel_name));
	if(results_out.group_name.size() > WorldObject::MAX_CONTENT_SIZE)
		results_out.group_name = results_out.group_name.substr(0, WorldObject::MAX_CONTENT_SIZE);

	// Decode the voxels
	std::vector<uint16> voxel_data;
	const std::string voxels_b64 = getStringField(parser, content_node, "voxels");
	if(!voxels_b64.empty())
	{
		std::vector<unsigned char> compressed;
		Base64::decode(voxels_b64, compressed);
		inflateZlibData(compressed, voxel_data);
	}

	// Work out the grid dimensions
	int X = options.voxels_x, Y = options.voxels_y, Z = options.voxels_z;
	if(X == 0 && Y == 0 && Z == 0)
	{
		if(parcel_node.hasChild("x1") && parcel_node.getChildNode(parser, "x1").type == JSONNode::Type_Number)
		{
			X = (int)((getNumberField(parser, parcel_node, "x2") - getNumberField(parser, parcel_node, "x1")) * 2);
			Y = (int)((getNumberField(parser, parcel_node, "y2") - getNumberField(parser, parcel_node, "y1")) * 2);
			Z = (int)((getNumberField(parser, parcel_node, "z2") - getNumberField(parser, parcel_node, "z1")) * 2);
		}
		else if(!voxel_data.empty())
		{
			inferVoxelDims(voxel_data.data(), voxel_data.size(), X, Y, Z);
			results_out.dims_were_inferred = true;
		}
	}
	if(!voxel_data.empty() && ((size_t)X * (size_t)Y * (size_t)Z != voxel_data.size()))
		throw glare::Exception("The parcel's voxel grid is " + toString(X) + " x " + toString(Y) + " x " + toString(Z) + " voxels, but the voxel data has " +
			toString(voxel_data.size()) + " voxels.");
	results_out.voxels_x = X;
	results_out.voxels_y = Y;
	results_out.voxels_z = Z;

	context.log("Parcel " + toString(parcel_id) + " '" + parcel_name + "' (" + (is_api_response ? "parcel API response" : "snapshot") + "), voxel grid " +
		toString(X) + " x " + toString(Y) + " x " + toString(Z) + (results_out.dims_were_inferred ? " (worked out from the voxel data)" : "") + ".");

	// The palette: 8 colours, Voxels' defaults unless the parcel has its own.
	const char* default_palette[8] = { "#ffffff", "#888888", "#000000", "#ff71ce", "#01cdfe", "#05ffa1", "#b967ff", "#fffb96" };
	std::vector<Colour3f> palette(8);
	for(int i=0; i<8; ++i)
		palette[i] = parseHexColour(default_palette[i], Colour3f(1.f));
	if(content_node.hasChild("palette"))
	{
		const JSONNode& palette_node = content_node.getChildNode(parser, "palette");
		if(palette_node.type == JSONNode::Type_Array)
			for(size_t i=0; i<palette_node.child_indices.size() && i<8; ++i)
			{
				const JSONNode& col_node = parser.nodes[palette_node.child_indices[i]];
				if(col_node.type == JSONNode::Type_String)
					palette[i] = parseHexColour(col_node.string_v, palette[i]);
			}
	}

	// Work out where the parcel is in the Voxels world, for converting Voxels teleport links to Substrata URLs.
	context.origin = origin;
	context.sub_URL_prefix = "sub://" + options.server_hostname + "/" + options.server_worldname;
	if(parcel_node.hasChild("x1") && parcel_node.getChildNode(parser, "x1").type == JSONNode::Type_Number)
	{
		context.voxels_parcel_base_centre = Vec3d(
			(getNumberField(parser, parcel_node, "x1") + getNumberField(parser, parcel_node, "x2")) / 2,
			getNumberField(parser, parcel_node, "y1"),
			(getNumberField(parser, parcel_node, "z1") + getNumberField(parser, parcel_node, "z2")) / 2);
		context.have_voxels_parcel_base_centre = true;
	}
	else
	{
		// A snapshot doesn't say where the parcel is, so estimate it from the teleport links in the parcel, assuming each takes you to near the
		// feature it's on, as with the buttons of an elevator.  Uses the median offset between a link's target and its feature's position, so a
		// few links to elsewhere don't throw it off.  The parcel's base is assumed to be at height 0, as most are.
		std::vector<double> x_offsets, z_offsets;
		if(content_node.hasChild("features") && content_node.getChildNode(parser, "features").type == JSONNode::Type_Array)
		{
			const JSONNode& features_node = content_node.getChildNode(parser, "features");
			for(size_t i=0; i<features_node.child_indices.size(); ++i)
			{
				const JSONNode& feature = parser.nodes[features_node.child_indices[i]];
				if(feature.type != JSONNode::Type_Object)
					continue;
				try
				{
					VoxelsCoords coords;
					if(getVoxelsTeleportCoords(getStringField(parser, feature, "link"), coords))
					{
						const Vec3d position = getVec3Field(parser, feature, "position", Vec3d(0.0));
						x_offsets.push_back(coords.x - position.x);
						z_offsets.push_back(coords.z - position.z);
					}
				}
				catch(glare::Exception&)
				{} // An invalid link is reported when the feature is made.
			}
		}

		if(!x_offsets.empty())
		{
			std::sort(x_offsets.begin(), x_offsets.end());
			std::sort(z_offsets.begin(), z_offsets.end());
			context.voxels_parcel_base_centre = Vec3d(x_offsets[x_offsets.size() / 2], 0, z_offsets[z_offsets.size() / 2]);
			context.have_voxels_parcel_base_centre = true;

			context.log("Estimated the parcel's centre in the Voxels world as x = " + doubleToStringNDecimalPlaces(context.voxels_parcel_base_centre.x, 1) +
				", z = " + doubleToStringNDecimalPlaces(context.voxels_parcel_base_centre.z, 1) + " from " + toString(x_offsets.size()) + " teleport link(s), for converting them to Substrata URLs.");
		}
	}

	getEmptyGroup(context, results_out.group_name, origin);

	if(!voxel_data.empty())
		makeParcelVoxelsObject(context, voxel_data, X, Y, Z, palette, origin);

	// Features
	std::map<std::string, FeatureTypeCounts> type_counts;
	if(content_node.hasChild("features"))
	{
		const JSONNode& features_node = content_node.getChildNode(parser, "features");
		if(features_node.type == JSONNode::Type_Array)
		{
			for(size_t i=0; i<features_node.child_indices.size(); ++i)
			{
				if(options.stopper && options.stopper->shouldStop())
					throw glare::Exception("Import stopped after " + toString(i) + " of " + toString(features_node.child_indices.size()) + " features.");

				context.progress_prefix = "[feature " + toString(i + 1) + "/" + toString(features_node.child_indices.size()) + "] ";

				const JSONNode& feature = parser.nodes[features_node.child_indices[i]];
				if(feature.type != JSONNode::Type_Object)
					continue;

				const std::string type = getStringField(parser, feature, "type");
				FeatureTypeCounts& counts = type_counts[type];
				try
				{
					if(type == "cube")
						makeCubeObject(context, parser, feature, origin);
					else if(type == "image")
						makeImageObject(context, parser, feature, origin, getStringField(parser, feature, "url"), /*target_url=*/std::string());
					else if(type == "nft-image")
						makeNFTImageObject(context, parser, feature, origin);
					else if(type == "vox-model" || type == "megavox")
						makeVoxModelObject(context, parser, feature, origin);
					else
						throw glare::Exception("not supported");

					counts.num_imported++;
				}
				catch(ImportAbortedExcep&)
				{
					throw;
				}
				catch(glare::Exception& e)
				{
					counts.num_skipped++;
					if(counts.skip_reason.empty())
						counts.skip_reason = e.what();

					// Unsupported types are only counted, in the summary, as there are a lot of them.
					if(e.what() != "not supported")
						context.log("Skipped " + type + " feature " + getStringField(parser, feature, "uuid") + ": " + e.what());
				}
			}
		}
	}

	for(auto it = type_counts.begin(); it != type_counts.end(); ++it)
	{
		std::string s = it->first + ": " + toString(it->second.num_imported) + " imported";
		if(it->second.num_skipped > 0)
			s += ", " + toString(it->second.num_skipped) + " skipped (first reason: " + it->second.skip_reason + ")";
		results_out.feature_type_counts.push_back(s);
	}

	// Wait for the server to say what it did with the objects, then for them to arrive in the world state, so that an import straight after
	// this one finds the group and empties it, rather than making a second one.
	if(context.end_create_token > context.first_create_token)
	{
		const size_t num_sent = (size_t)(context.end_create_token - context.first_create_token);
		const double wait_timeout_s = myMin(5.0 + num_sent * 0.01, 60.0);

		context.log("Sent " + toString(num_sent) + " objects, waiting for the server...");

		Timer wait_timer;
		while(1)
		{
			context.tracker->getOutcomesForRange(context.first_create_token, context.end_create_token,
				results_out.num_creates_answered, results_out.num_creates_refused, results_out.first_refusal_msg);

			if((results_out.num_creates_answered >= num_sent) || (wait_timer.elapsed() > wait_timeout_s))
				break;

			context.checkShouldStop();
			PlatformUtils::Sleep(10);
		}

		std::vector<UID> created_uids;
		context.tracker->getCreatedUIDsForRange(context.first_create_token, context.end_create_token, created_uids);
		context.tracker->addBuiltObjectUIDs(created_uids);

		context.log("Server created " + toString(created_uids.size()) + " objects and refused " + toString(results_out.num_creates_refused) +
			((results_out.num_creates_answered < num_sent) ? (", " + toString(num_sent - results_out.num_creates_answered) + " not answered in time") : std::string()) + ".");

		while(wait_timer.elapsed() <= wait_timeout_s)
		{
			bool all_present = true;
			{
				Lock lock(gui_client->world_state->mutex);
				for(size_t i=0; i<created_uids.size(); ++i)
					if(gui_client->world_state->objects.find(created_uids[i]) == gui_client->world_state->objects.end())
					{
						all_present = false;
						break;
					}
			}
			if(all_present)
				break;
			context.checkShouldStop();
			PlatformUtils::Sleep(10);
		}

		context.tracker->forgetRange(context.first_create_token, context.end_create_token);
	}

	results_out.elapsed_s = timer.elapsed();
}


std::string VoxelsParcelImportResults::summary() const
{
	std::string s = "Imported '" + group_name + "' in " + doubleToStringNSigFigs(elapsed_s, 3) + " s.\n";
	s += "Voxel grid: " + toString(voxels_x) + " x " + toString(voxels_y) + " x " + toString(voxels_z) + " voxels" + (dims_were_inferred ? " (worked out from the voxel data)" : "") + ".\n";
	if(num_objects_deleted > 0)
		s += "Removed " + toString(num_objects_deleted) + " object(s) from the previous import.\n";
	s += "Sent " + toString(num_objects_sent) + " object(s); the server answered " + toString(num_creates_answered) + " and refused " + toString(num_creates_refused) +
		(first_refusal_msg.empty() ? std::string() : (" (first refusal: " + first_refusal_msg + ")")) + ".\n";
	s += "Downloaded " + toString(num_assets_downloaded) + " asset(s).\n";
	s += "Features:\n";
	for(size_t i=0; i<feature_type_counts.size(); ++i)
		s += "  " + feature_type_counts[i] + "\n";
	if(!skipped_assets.empty())
	{
		s += "Assets not used (" + toString(skipped_assets.size()) + "):\n";
		for(size_t i=0; i<skipped_assets.size(); ++i)
			s += "  " + skipped_assets[i] + "\n";
	}
	return s;
}


void VoxelsParcelImportStopper::stop()
{
	should_stop = true;

	Lock lock(mutex);
	if(current_http_client.nonNull())
		current_http_client->kill();
}


void VoxelsParcelImportStopper::setCurrentHTTPClient(const Reference<HTTPClient>& client)
{
	Lock lock(mutex);
	current_http_client = client;
}


void VoxelsParcelImportThread::doRun()
{
	PlatformUtils::setCurrentThreadName("VoxelsParcelImportThread");

	try
	{
		VoxelsParcelImportOptions use_options = options;
		use_options.stopper = stopper.ptr();

		VoxelsParcelImportResults results;
		VoxelsParcelImporter::importParcel(gui_client, json_path, use_options, results);

		const std::string summary = results.summary();
		conPrint(summary);
		gui_client->msg_queue.enqueue(new LogMessage(summary));
		if(options.log_queue)
			options.log_queue->enqueue(summary);

		std::string notification = "Imported " + results.group_name + ": " + toString(results.num_objects_sent) + " objects";
		if(results.num_creates_refused > 0)
			notification += ", " + toString(results.num_creates_refused) + " refused by the server (" + results.first_refusal_msg + ")";
		notification += ".";
		gui_client->msg_queue.enqueue(new InfoMessage(notification));
	}
	catch(glare::Exception& e)
	{
		const std::string msg = "Voxels parcel import failed: " + e.what();
		conPrint(msg);
		gui_client->msg_queue.enqueue(new ErrorMessage(msg));
		if(options.log_queue)
			options.log_queue->enqueue(msg);
	}

	if(options.log_queue)
		options.log_queue->enqueue(finishedMarker());
}


#if BUILD_TESTS


#include <utils/TestUtils.h>
#include <utils/TestExceptionUtils.h>


void VoxelsParcelImporter::test()
{
	conPrint("VoxelsParcelImporter::test()");

	// inferVoxelDims: a 16 x 10 x 12 grid with a floor and two walls should be recovered from the 1920 voxels.
	{
		const int X = 16, Y = 10, Z = 12;
		std::vector<uint16> a(X * Y * Z, 0);
		for(int x=0; x<X; ++x)
		for(int y=0; y<Y; ++y)
		for(int z=0; z<Z; ++z)
			if(y == 0 || x == 0 || z == Z-1)
				a[(x*Y + y)*Z + z] = 32771;

		int rx, ry, rz;
		inferVoxelDims(a.data(), a.size(), rx, ry, rz);
		testAssert(rx == X && ry == Y && rz == Z);
	}

	// No dimensions fit
	{
		std::vector<uint16> a(14, 0);
		int rx, ry, rz;
		testExceptionExpected([&]() { inferVoxelDims(a.data(), a.size(), rx, ry, rz); });
	}

	// Download URLs
	{
		testExceptionExpected([&]() { getDownloadURL(""); });
		testExceptionExpected([&]() { getDownloadURL("ugc://parcel/2/W2bfRe.vox"); });
		testExceptionExpected([&]() { getDownloadURL("/uploads/atlas/x.png"); });
		testExceptionExpected([&]() { getDownloadURL("https://www.voxels.com/a.png"); });
		testExceptionExpected([&]() { getDownloadURL("https://cdn.cryptovoxels.com/node/img?url=x"); });
		testAssert(getDownloadURL("https://notvoxels.com/a.png") == "https://notvoxels.com/a.png");
		testAssert(getDownloadURL("https://www.dropbox.com/s/abc/x.vox?dl=0") == "https://www.dropbox.com/s/abc/x.vox?dl=1");
		testAssert(getDownloadURL("https://www.dropbox.com/scl/fi/abc/x.png?rlkey=k&dl=0") == "https://www.dropbox.com/scl/fi/abc/x.png?rlkey=k&dl=1");
		testAssert(getDownloadURL("https://www.dropbox.com/s/abc/x.png") == "https://www.dropbox.com/s/abc/x.png?dl=1");
	}

	// Voxels teleport coords
	{
		const VoxelsCoords c = parseVoxelsCoords("E@351W,444S,15U");
		testAssert(c.facing == 'E' && c.x == -351 && c.z == -444 && c.height == 15);

		const VoxelsCoords c2 = parseVoxelsCoords("12.5E,3N");
		testAssert(c2.facing == 0 && c2.x == 12.5 && c2.z == 3 && c2.height == 0);

		testExceptionExpected([&]() { parseVoxelsCoords("351W"); }); // No z
		testExceptionExpected([&]() { parseVoxelsCoords("E@351Q,444S"); });
		testExceptionExpected([&]() { parseVoxelsCoords("E@abcW,444S"); });

		VoxelsCoords c3;
		testAssert(getVoxelsTeleportCoords("https://www.voxels.com/play?coords=E@351W,444S,15U", c3) && c3.x == -351);
		testAssert(!getVoxelsTeleportCoords("https://superrare.com/artwork/1", c3));
		testAssert(!getVoxelsTeleportCoords("", c3));
	}

	// Rotation conversion: a yaw of +90 degrees turns Voxels +z (Substrata -y) towards Voxels +x (Substrata -x).
	{
		const Matrix4f rot = featureRotation(Vec3d(0, Maths::pi_2<double>(), 0));
		const Vec4f v = rot * Vec4f(0, -1, 0, 0);
		testAssert(epsEqual(v, Vec4f(-1, 0, 0, 0)));
	}

	conPrint("VoxelsParcelImporter::test() done.");
}


#endif // BUILD_TESTS
