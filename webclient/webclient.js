/*=====================================================================
webclient.js
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/

//import { GLTFLoader } from './examples/jsm/loaders/GLTFLoader.js';
import * as THREE from './build/three.module.js';
import { Sky } from './examples/jsm/objects/Sky.js';
import * as voxelloading from './voxelloading.js';
import * as bufferin from './bufferin.js';
import * as bufferout from './bufferout.js';
import { loadBatchedMesh } from './bmeshloading.js';

var ws = new WebSocket("wss://substrata.info", "substrata-protocol");
//var ws = new WebSocket("ws://localhost", "substrata-protocol");
ws.binaryType = "arraybuffer"; // Change binary type from "blob" to "arraybuffer"


const STATE_INITIAL = 0;
const STATE_READ_HELLO_RESPONSE = 1;
const STATE_READ_PROTOCOL_RESPONSE = 2;
const STATE_READ_CLIENT_AVATAR_UID = 3;

var protocol_state = 0;

const CyberspaceProtocolVersion = 31;
const CyberspaceHello = 1357924680;
const ClientProtocolOK = 10000;
const ClientProtocolTooOld = 10001;
const ClientProtocolTooNew = 10002;

const AvatarCreated = 1000;
const AvatarDestroyed = 1001;
const AvatarTransformUpdate = 1002;
const AvatarFullUpdate = 1003;
const CreateAvatar = 1004;
const AvatarIsHere = 1005;

const ChatMessageID = 2000;

const QueryObjects = 3020; // Client wants to query objects in certain grid cells
const ObjectInitialSend = 3021;
const ParcelCreated = 3100;

const LogInMessage = 8000;
const LoggedInMessageID = 8003;

const TimeSyncMessage = 9000;

const WorldObject_ObjectType_VoxelGroup = 2;

let WORLD_MATERIAL_SERIALISATION_VERSION = 6


const MESH_NOT_LOADED = 0;
const MESH_LOADING = 1;
const MESH_LOADED = 2;

// from https://gist.github.com/joni/3760795
function toUTF8Array(str) {
    var utf8 = [];
    for (var i = 0; i < str.length; i++) {
        var charcode = str.charCodeAt(i);
        if (charcode < 0x80) utf8.push(charcode);
        else if (charcode < 0x800) {
            utf8.push(0xc0 | (charcode >> 6),
                0x80 | (charcode & 0x3f));
        }
        else if (charcode < 0xd800 || charcode >= 0xe000) {
            utf8.push(0xe0 | (charcode >> 12),
                0x80 | ((charcode >> 6) & 0x3f),
                0x80 | (charcode & 0x3f));
        }
        // surrogate pair
        else {
            i++;
            // UTF-16 encodes 0x10000-0x10FFFF by
            // subtracting 0x10000 and splitting the
            // 20 bits of 0x0-0xFFFFF into two halves
            charcode = 0x10000 + (((charcode & 0x3ff) << 10)
                | (str.charCodeAt(i) & 0x3ff))
            utf8.push(0xf0 | (charcode >> 18),
                0x80 | ((charcode >> 12) & 0x3f),
                0x80 | ((charcode >> 6) & 0x3f),
                0x80 | (charcode & 0x3f));
        }
    }
    return utf8;
}


function fromUTF8Array(data) { // array of bytes
    var str = '',
        i;

    for (i = 0; i < data.length; i++) {
        var value = data[i];

        if (value < 0x80) {
            str += String.fromCharCode(value);
        } else if (value > 0xBF && value < 0xE0) {
            str += String.fromCharCode((value & 0x1F) << 6 | data[i + 1] & 0x3F);
            i += 1;
        } else if (value > 0xDF && value < 0xF0) {
            str += String.fromCharCode((value & 0x0F) << 12 | (data[i + 1] & 0x3F) << 6 | data[i + 2] & 0x3F);
            i += 2;
        } else {
            // surrogate pair
            var charCode = ((value & 0x07) << 18 | (data[i + 1] & 0x3F) << 12 | (data[i + 2] & 0x3F) << 6 | data[i + 3] & 0x3F) - 0x010000;

            str += String.fromCharCode(charCode >> 10 | 0xD800, charCode & 0x03FF | 0xDC00);
            i += 3;
        }
    }

    return str;
}


function writeStringToWebSocket(ws, str)
{
    let utf8_array = toUTF8Array(str)

    ws.send(new Uint32Array([utf8_array.length]));
    ws.send(new Uint8Array(utf8_array));
}


ws.onopen = function () {
    console.log("onopen()");

	ws.send(new Uint32Array([CyberspaceHello]));

	ws.send(new Uint32Array([CyberspaceProtocolVersion]));

	let ConnectionTypeUpdates = 500;
	ws.send(new Uint32Array([ConnectionTypeUpdates]));

    writeStringToWebSocket(ws, world); // World to connect to

    sendQueryObjectsMessage();
};


function readInt32(buffer_in) {
    let x = buffer_in.data_view.getInt32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 4;
    return x;
}

function readUInt32(buffer_in) {
    let x = buffer_in.data_view.getUint32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 4;
    return x;
}

function readUInt64(buffer_in) {
    let x = buffer_in.data_view.getBigUint64(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 8;
    return x;
}

function readFloat(buffer_in) {
    let x = buffer_in.data_view.getFloat32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 4;
    return x;
}

function readDouble(buffer_in) {
    let x = buffer_in.data_view.getFloat64(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 8;
    return x;
}


function readUIDFromStream(buffer_in) {
    return readUInt64(buffer_in);
}


function writeUID(buffer_out, uid) {
    buffer_out.writeUInt64(uid);
}


class Vec2d {
    constructor(x_, y_) {
        this.x = x_;
        this.y = y_;
    }
}

class Vec3f {
    constructor(x_, y_, z_) {
        this.x = x_;
        this.y = y_;
        this.z = z_;
    }

    writeToStream(buffer_out) {
        buffer_out.writeFloat(this.x);
        buffer_out.writeFloat(this.y);
        buffer_out.writeFloat(this.z);
    }
}

class Matrix2f {
    constructor(x_, y_, z_, w_) {
        this.x = x_;
        this.y = y_;
        this.z = z_;
        this.w = w_;
    }

    writeToStream(buffer_out) {
        buffer_out.writeFloat(this.x);
        buffer_out.writeFloat(this.y);
        buffer_out.writeFloat(this.z);
        buffer_out.writeFloat(this.w);
    }
}

class Colour3f {
    constructor(x_, y_, z_) {
        this.r = x_;
        this.g = y_;
        this.b = z_;
    }

    writeToStream(buffer_out) {
        buffer_out.writeFloat(this.r);
        buffer_out.writeFloat(this.g);
        buffer_out.writeFloat(this.b);
    }
}

class Vec3d {
    constructor(x_, y_, z_) {
        this.x = x_;
        this.y = y_;
        this.z = z_;
    }

    writeToStream(buffer_out) {
        buffer_out.writeDouble(this.x);
        buffer_out.writeDouble(this.y);
        buffer_out.writeDouble(this.z);
    }
}

function readVec2dFromStream(buffer_in) {
    let x = readDouble(buffer_in);
    let y = readDouble(buffer_in);
    return new Vec2d(x, y);
}

function readVec3fFromStream(buffer_in) {
    let x = readFloat(buffer_in);
    let y = readFloat(buffer_in);
    let z = readFloat(buffer_in);
    return new Vec3f(x, y, z);
}

function readVec3dFromStream(buffer_in) {
    let x = readDouble(buffer_in);
    let y = readDouble(buffer_in);
    let z = readDouble(buffer_in);
    return new Vec3d(x, y, z);
}

function readColour3fFromStream(buffer_in) {
    let x = readFloat(buffer_in);
    let y = readFloat(buffer_in);
    let z = readFloat(buffer_in);
    return new Colour3f(x, y, z);
}

function readMatrix2fFromStream(buffer_in) {
    let x = readFloat(buffer_in);
    let y = readFloat(buffer_in);
    let z = readFloat(buffer_in);
    let w = readFloat(buffer_in);
    return new Matrix2f(x, y, z, w);
}

function readStringFromStream(buffer_in) {
    let len = readUInt32(buffer_in); // Read length in bytes

    let utf8_array = new Int8Array(buffer_in.array_buffer, /*byteoffset=*/buffer_in.read_index, /*length=*/len);

    buffer_in.read_index += len;

    return fromUTF8Array(utf8_array);
}

function readParcelIDFromStream(buffer_in) {
    return readUInt32(buffer_in);
}

function readUserIDFromStream(buffer_in) {
    return readUInt32(buffer_in);
}

const TIMESTAMP_SERIALISATION_VERSION = 1;

function readTimeStampFromStream(buffer_in) {
    let version = readUInt32(buffer_in);
    if (version != TIMESTAMP_SERIALISATION_VERSION)
        throw "Unhandled version " + toString(version) + ", expected " + toString(TIMESTAMP_SERIALISATION_VERSION) + ".";

    return readUInt64(buffer_in);
}



class AvatarSettings {
    constructor() {
        this.model_url = "";
        this.materials = [];
        this.pre_ob_to_world_matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0];
    }

    writeToStream(stream) {
        stream.writeStringLengthFirst(this.model_url);

        // Write materials
        stream.writeUInt32(this.materials.length)
        for (let i = 0; i < this.materials.length; ++i)
            this.materials[i].writeToStream(stream)

        for (let i = 0; i < 16; ++i)
            stream.writeFloat(this.pre_ob_to_world_matrix[i])
    }


    readFromStream(stream) {
        this.model_url = stream.readStringLengthFirst();

        // Read materials
        let num_mats = stream.readUInt32();
        if (num_mats > 10000)
            throw "Too many mats: " + num_mats
        this.materials = [];
        for (let i = 0; i < num_mats; ++i) {
            let mat = readWorldMaterialFromStream(stream);
            this.materials.push(mat);
        }

        for (let i = 0; i < 16; ++i)
            this.pre_ob_to_world_matrix[i] = stream.readFloat();
    }
}

class Avatar {
    constructor() {
        this.uid = 0; // uint64
        this.name = "";
        this.pos = new Vec3d(0, 0, 0);
        this.rotation = new Vec3f(0, 0, 0);
        this.avatar_settings = new AvatarSettings();

        this.mesh_state = MESH_NOT_LOADED;
    }

    writeToStream(stream) {
        stream.writeUInt64(this.uid);
        stream.writeStringLengthFirst(this.name);
        this.pos.writeToStream(stream);
        this.rotation.writeToStream(stream);
        this.avatar_settings.writeToStream(stream);
    }

    readFromStream(stream) {
        this.uid = stream.readUInt64();
        this.readFromStreamGivenUID(stream);
    }

    readFromStreamGivenUID(stream) {
        this.name = stream.readStringLengthFirst();
        this.pos = readVec3dFromStream(stream);
        this.rotation = readVec3fFromStream(stream);
        this.avatar_settings.readFromStream(stream);
    }
}


class Parcel {

}


function readParcelFromNetworkStreamGivenID(buffer_in) {
    let parcel = new Parcel();

    parcel.owner_id = readUserIDFromStream(buffer_in);
    parcel.created_time = readTimeStampFromStream(buffer_in);
    parcel.description = readStringFromStream(buffer_in);

    //console.log("parcel: ", parcel)
    //console.log("parcel.description: ", parcel.description)

    // Read admin_ids
    {
        let num = readUInt32(buffer_in);
        if (num > 100000)
            throw "Too many admin_ids: " + toString(num);
        parcel.admin_ids = [];
        for (let i = 0; i < num; ++i)
            parcel.admin_ids.push(readUserIDFromStream(buffer_in));

        //console.log("parcel.admin_ids: ", parcel.admin_ids)
    }

    // Read writer_ids
    {
        let num = readUInt32(buffer_in);
        if (num > 100000)
            throw "Too many writer_ids: " + toString(num);
        parcel.writer_ids = [];
        for (let i = 0; i < num; ++i)
            parcel.writer_ids.push(readUserIDFromStream(buffer_in));
    }

    // Read child_parcel_ids
    {
        let num = readUInt32(buffer_in);
        if (num > 100000)
            throw "Too many child_parcel_ids: " + toString(num);
        parcel.child_parcel_ids = [];
        for (let i = 0; i < num; ++i)
            parcel.child_parcel_ids.push(readUserIDFromStream(buffer_in));
    }

    // Read all_writeable
    {
        let val = readUInt32(buffer_in);
        if (val != 0 && val != 1)
            throw "Invalid boolean value";
        parcel.all_writeable = val != 0;
    }

    parcel.verts = []
    for (let i = 0; i < 4; ++i) {
        parcel.verts.push(readVec2dFromStream(buffer_in));

        //console.log("parcel.verts[i]: ", parcel.verts[i]);
    }
        
    parcel.zbounds = readVec2dFromStream(buffer_in);

    {
        // Read parcel_auction_ids
        let num = readUInt32(buffer_in);
        if (num > 100000)
            throw "Too many parcel_auction_ids: " + toString(num);
        parcel.parcel_auction_ids = []
        for (let i = 0; i < num; ++i)
            parcel.parcel_auction_ids.push(readUInt32(buffer_in));
    }

    //console.log("parcel.parcel_auction_ids: ", parcel.parcel_auction_ids)

    parcel.owner_name = readStringFromStream(buffer_in);

    //console.log("parcel.owner_name: ", parcel.owner_name)

    // Read admin_names
    {
        let num = readUInt32(buffer_in);
        if (num > 100000)
            throw "Too many admin_names: " + toString(num);
        parcel.admin_names = []
        for (let i = 0; i < num; ++i)
            parcel.admin_names.push(readStringFromStream(buffer_in));
    }

    // Read writer_names
    {
        let num = readUInt32(buffer_in);
        if (num > 100000)
            throw "Too many writer_names: " + toString(num);
        parcel.writer_names = []
        for (let i = 0; i < num; ++i)
            parcel.writer_names.push(readStringFromStream(buffer_in));
    }

    //console.log("parcel: ", parcel)

    return parcel;
}


class WorldObject {

    constructor() {
        this.mesh_state = MESH_NOT_LOADED;
    }
}

const COLOUR_TEX_HAS_ALPHA_FLAG = 1;
const MIN_LOD_LEVEL_IS_NEGATIVE_1 = 2;

class WorldMaterial {

    colourTexHasAlpha() {
        return (this.flags & COLOUR_TEX_HAS_ALPHA_FLAG) != 0;
    }

    minLODLevel() {
        return (this.flags & MIN_LOD_LEVEL_IS_NEGATIVE_1) ? -1 : 0;
    }

    writeToStream(stream) {
        stream.writeUInt32(WORLD_MATERIAL_SERIALISATION_VERSION);

        this.colour_rgb.writeToStream(stream);
        stream.writeStringLengthFirst(this.colour_texture_url);

        this.roughness.writeToStream(stream);
        this.metallic_fraction.writeToStream(stream);
        this.opacity.writeToStream(stream);

        this.tex_matrix.writeToStream(stream);

        stream.writeFloat(this.emission_lum_flux);

        stream.writeUInt32(this.flags);
    }
}

class ScalarVal {
    constructor(val_, texture_url_) {
        this.val = val_;
        this.texture_url = texture_url_;
    }

    writeToStream(stream) {
        stream.writeFloat(this.val);
        stream.writeStringLengthFirst(this.texture_url);
    }
}

function readScalarValFromStream(buffer_in) {
    let val = readFloat(buffer_in);
    let texture_url = readStringFromStream(buffer_in);
    return new ScalarVal(val, texture_url);
}

function readWorldMaterialFromStream(buffer_in) {
    let mat = new WorldMaterial();

    let version = readUInt32(buffer_in);
    if (version > WORLD_MATERIAL_SERIALISATION_VERSION)
        throw "Unsupported version " + toString(v) + ", expected " + toString(WORLD_MATERIAL_SERIALISATION_VERSION) + ".";

    mat.colour_rgb = readColour3fFromStream(buffer_in);
    mat.colour_texture_url = readStringFromStream(buffer_in);

    mat.roughness = readScalarValFromStream(buffer_in);
    mat.metallic_fraction = readScalarValFromStream(buffer_in);
    mat.opacity = readScalarValFromStream(buffer_in);

    mat.tex_matrix = readMatrix2fFromStream(buffer_in);

    mat.emission_lum_flux = readFloat(buffer_in);

    mat.flags = readUInt32(buffer_in);

    return mat;
}


function readWorldObjectFromNetworkStreamGivenUID(buffer_in) {
    let ob = new WorldObject();

    ob.object_type = readUInt32(buffer_in);
    ob.model_url = readStringFromStream(buffer_in);
    // Read mats
    {
        let num = readUInt32(buffer_in);
        if (num > 10000)
            throw "Too many mats: " + toString(num);
        ob.mats = []
        for (let i = 0; i < num; ++i)
            ob.mats.push(readWorldMaterialFromStream(buffer_in));
    }

    ob.lightmap_url = readStringFromStream(buffer_in);

    ob.script = readStringFromStream(buffer_in);
    ob.content = readStringFromStream(buffer_in);
    ob.target_url = readStringFromStream(buffer_in);

    ob.audio_source_url = readStringFromStream(buffer_in);
    ob.audio_volume = readFloat(buffer_in);

    ob.pos = readVec3dFromStream(buffer_in);
    ob.axis = readVec3fFromStream(buffer_in);
    ob.angle = readFloat(buffer_in);

    ob.scale = readVec3fFromStream(buffer_in);

    ob.created_time = readTimeStampFromStream(buffer_in);
    ob.creator_id = readUserIDFromStream(buffer_in);

    ob.flags = readUInt32(buffer_in);

    ob.creator_name = readStringFromStream(buffer_in);

    ob.aabb_ws_min = readVec3fFromStream(buffer_in);
    ob.aabb_ws_max = readVec3fFromStream(buffer_in);

    ob.max_model_lod_level = readInt32(buffer_in);

    if (ob.object_type == WorldObject_ObjectType_VoxelGroup)
    {
        // Read compressed voxel data
        let voxel_data_size = readUInt32(buffer_in);
        if (voxel_data_size > 1000000)
            throw "Invalid voxel_data_size (too large): " + toString(voxel_data_size);

        if(voxel_data_size > 0) {
            ob.compressed_voxels = buffer_in.readData(voxel_data_size); // Read voxel data
        }
    }

    return ob;
}


function sendQueryObjectsMessage() {

    let buffer_out = new bufferout.BufferOut();
    buffer_out.writeUInt32(QueryObjects);
    buffer_out.writeUInt32(0); // message length - to be updated.
    let r = 1;
    buffer_out.writeUInt32(2 * (2 * r + 1) * (2 * r + 1)); // Num cells to query

    for (let x = -r; x <= r; ++x)
        for (let y = -r; y <= r; ++y) {
            buffer_out.writeInt32(x); buffer_out.writeInt32(y); buffer_out.writeInt32(0);
            buffer_out.writeInt32(x); buffer_out.writeInt32(y); buffer_out.writeInt32(-1);
        }

    buffer_out.updateMessageLengthField();
    buffer_out.writeToWebSocket(ws);
}


var parcels = new Map();
var world_objects = new Map();
var avatars = new Map();
var client_avatar_uid = null;


//Log the messages that are returned from the server
ws.onmessage = function (event) {
    //console.log("onmessage()");
    //console.log("From Server:" + event.data + ", event.data.byteLength: " + event.data.byteLength);

    let z = 0;
    let buffer = new bufferin.BufferIn(event.data);
    while (!buffer.endOfStream()) {

        //console.log("Reading, buffer.read_index: " + buffer.read_index);

        if (protocol_state == STATE_INITIAL) {
            // Read hello_response
            var hello_response = readUInt32(buffer);
            if (hello_response != CyberspaceHello)
                throw 'hello_response was invalid: ' + hello_response.toString();

            //console.log("Read hello_response from server.");
            protocol_state = STATE_READ_HELLO_RESPONSE;
        }
        else if (protocol_state == STATE_READ_HELLO_RESPONSE) {
            // Read protocol_response
            var protocol_response = readUInt32(buffer);
            if (protocol_response == ClientProtocolOK) { }
            else if (protocol_response == ClientProtocolTooOld) {
                throw 'client protocol version is too old'
            }
            else if (protocol_response == ClientProtocolTooNew) {
                throw 'client protocol version is too new'
            }
            else
                throw 'protocol_response was invalid: ' + protocol_response.toString();

            //console.log("Read protocol_response from server.");
            protocol_state = STATE_READ_PROTOCOL_RESPONSE;
        }
        else if (protocol_state == STATE_READ_PROTOCOL_RESPONSE) {
            // Read client_avatar_uid
            client_avatar_uid = readUIDFromStream(buffer);

            //console.log("Read client_avatar_uid from server: " + client_avatar_uid);
            protocol_state = STATE_READ_CLIENT_AVATAR_UID;
        }
        else if (protocol_state == STATE_READ_CLIENT_AVATAR_UID) {

            var msg_type = readUInt32(buffer);
            var msg_len = readUInt32(buffer);
            //console.log("Read msg_type: " + msg_type + ", len: " + msg_len);

            if (msg_type == TimeSyncMessage) {
                let global_time = readDouble(buffer);
                //console.log("Read TimeSyncMessage with global time: " + global_time);
            }
            else if (msg_type == ParcelCreated) {
                let parcel_id = readParcelIDFromStream(buffer);
                //console.log("parcel_id: ", parcel_id);
                let parcel = readParcelFromNetworkStreamGivenID(buffer);
                parcel.parcel_id = parcel_id;

                parcels.set(parcel_id, parcel);

                //console.log("Read ParcelCreated msg, parcel_id: " + parcel_id);

                //addParcelGraphics(parcel)
            }
            else if (msg_type == ObjectInitialSend) {
                // console.log("received ObjectInitialSend...");

                let object_uid = readUIDFromStream(buffer);

                let world_ob = readWorldObjectFromNetworkStreamGivenUID(buffer);
                world_ob.uid = object_uid;

                addWorldObjectGraphics(world_ob);

                world_objects.set(object_uid, world_ob);
                //console.log("Read ObjectInitialSend msg, object_uid: " + object_uid);
            }
            else if (msg_type == ChatMessageID) {
                let name = readStringFromStream(buffer);
                let msg = readStringFromStream(buffer);

                console.log("Chat message: " + name + ": " + msg);

                appendChatMessage(name + ": " + msg);
            }
            else if (msg_type == AvatarCreated) {

                let avatar = new Avatar();
                avatar.readFromStream(buffer);

                avatars.set(avatar.uid, avatar);

                console.log("Avatar " + avatar.name + " joined");

                appendChatMessage(avatar.name + " joined");

                updateOnlineUsersList();

                if (avatar.uid != client_avatar_uid) {
                    console.log("Loading avatar model: " + avatar.avatar_settings.model_url);
                    let ob_lod_level = 0;
                    let world_axis = new Vec3f(0, 0, 1);
                    let angle = 0;
                    loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
                }
            }
            else if (msg_type == AvatarIsHere) {
            
                let avatar = new Avatar();
                avatar.readFromStream(buffer);
                
                avatars.set(avatar.uid, avatar);

                console.log("Avatar " + avatar.name + " is here");

                appendChatMessage(avatar.name + " is here");

                updateOnlineUsersList();

                if (avatar.uid != client_avatar_uid) {
                    console.log("Loading avatar model: " + avatar.avatar_settings.model_url);
                    let ob_lod_level = 0;
                    let world_axis = new Vec3f(0, 0, 1);
                    let angle = 0;
                    loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
                }
            }
            else if (msg_type == AvatarDestroyed) {
                const avatar_uid = readUIDFromStream(buffer);

                let avatar = avatars.get(avatar_uid);
                avatars.delete(avatar_uid);

                if (avatar) {
                    // Remove avatar mesh
                    scene.remove(avatar.mesh);
                }

                updateOnlineUsersList();
            }
            else if (msg_type == AvatarTransformUpdate) {

                let avatar_uid = readUIDFromStream(buffer);
                let pos = readVec3dFromStream(buffer);
                let rotation = readVec3fFromStream(buffer);
                let anim_state = buffer.readUInt32();


                let avatar = avatars.get(avatar_uid);
                if (avatar) {
                    avatar.pos = pos;
                    avatar.rotation = rotation;
                    avatar.anim_state = anim_state;
                }
            }
            else if (msg_type == AvatarFullUpdate) {

                console.log("AvatarFullUpdate");

                let avatar_uid = readUIDFromStream(buffer);
                let avatar = avatars.get(avatar_uid);
                if (avatar) {
                    avatar.readFromStreamGivenUID(buffer);
                }
                else {
                    let avatar = new Avatar();
                    avatar.readFromStreamGivenUID(buffer);
                }

                //avatars.set(avatar.uid, avatar);

                if (avatar.uid != client_avatar_uid) {// && avatar.mesh_state != MESH_LOADED) {

                    console.log("Loading avatar model: " + avatar.avatar_settings.model_url);
                    let ob_lod_level = 0;
                    let world_axis = new Vec3f(0, 0, 1);
                    let angle = 0;
                    loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
                }
            }
            else if (msg_type == LoggedInMessageID) {

                const logged_in_user_id = readUserIDFromStream(buffer);
                const logged_in_username = buffer.readStringLengthFirst();
                client_avatar.avatar_settings.readFromStream(buffer);

                console.log("Logged in as " + logged_in_username);

                // Send create avatar message now that we have our avatar settings.
                let av_buf = new bufferout.BufferOut();
                av_buf.writeUInt32(CreateAvatar);
                av_buf.writeUInt32(0); // will be updated with length
                client_avatar.writeToStream(av_buf);
                av_buf.updateMessageLengthField();
                av_buf.writeToWebSocket(ws);
            }
            else {
                // Unhandled message type, skip over it.
                //console.log("Unhandled message type " + msg_type);
                buffer.read_index += msg_len - 8; // We have already read the type and len (uint32 * 2), skip over remaining data in msg.
            }
        }
        else
            throw "invalid protocol_state";

        z++;
        if (z > 100000) {
            throw 'oh no, infinite loop!';
        }
    }
};

// Fired when a connection with a WebSocket is closed,
ws.onclose = function (event) {
	console.log("WebSocket onclose()", event);
};

// Fired when a connection with a WebSocket has been closed because of an error,
ws.onerror = function (event) {
	console.error("WebSocket error observed:", event);
};


function appendChatMessage(msg) {
    let node = document.createElement("div");
    node.textContent = msg;
    document.getElementById("chatmessages").appendChild(node);

    document.getElementById("chatmessages").scrollTop = document.getElementById("chatmessages").scrollHeight;
}


function onChatSubmitted(event) {
    console.log("Chat submitted");

    //let msg = event.target.elements.chat_message.value;
    let msg = document.getElementById("chat_message").value;
    console.log("msg: " + msg)


    let buffer_out = new bufferout.BufferOut();
    buffer_out.writeUInt32(ChatMessageID);
    buffer_out.writeUInt32(0); // will be updated with length
    buffer_out.writeStringLengthFirst(msg); // message
    buffer_out.updateMessageLengthField();
    buffer_out.writeToWebSocket(ws);


   // log.textContent = `Form Submitted! Time stamp: ${event.timeStamp}`;
    event.preventDefault();

    //document.getElementById('chat_message').textContent = ""; // Clear chat box
    document.getElementById('chatform').reset(); // Clear chat box
}

const form = document.getElementById('form');
document.getElementById('chatform').addEventListener('submit', onChatSubmitted);


//document.getElementById("chat_message").addEventListener("keyup", function (event) {
//    console.log("keyup");
//    event.preventDefault();
//    if (event.keyCode === 13) {
//        onChatSubmitted(event);
//    }
//});



function updateOnlineUsersList() {

    //document.getElementById("onlineuserslist").textContent = ""; // clear div
    //
    //for (const avatar of avatars.values()) {
    //    let node = document.createElement("div");
    //    node.textContent = avatar.name;
    //    document.getElementById("onlineuserslist").appendChild(node);
    //}
}

// https://stackoverflow.com/questions/190852/how-can-i-get-file-extensions-with-javascript
function filenameExtension(filename) {
    return filename.split('.').pop();
}

function hasExtension(filename, ext) {
    return filenameExtension(filename).toLowerCase() === ext.toLowerCase();
}

function AABBLongestLength(world_ob) {
    return Math.max(
        world_ob.aabb_ws_max.x - world_ob.aabb_ws_min.x,
        world_ob.aabb_ws_max.y - world_ob.aabb_ws_min.y,
        world_ob.aabb_ws_max.z - world_ob.aabb_ws_min.z
    );
}

function getLODLevel(world_ob, campos) {

    let dist = new THREE.Vector3(world_ob.pos.x, world_ob.pos.y, world_ob.pos.z).distanceTo(campos);
    let proj_len = AABBLongestLength(world_ob) / dist;

    if (proj_len > 0.6)
        return -1;
    else if (proj_len > 0.16)
        return 0;
    else if (proj_len > 0.03)
        return 1;
    else
        return 2;
}



function getModelLODLevel(world_ob, campos) { // getLODLevel() clamped to max_model_lod_level, also clamped to >= 0.
    if (world_ob.max_model_lod_level == 0)
        return 0;

    return Math.max(0, getLODLevel(world_ob, campos));
}


// https://stackoverflow.com/questions/4250364/how-to-trim-a-file-extension-from-a-string-in-javascript
function removeDotAndExtension(filename) {
   return filename.split('.').slice(0, -1).join('.')
}

function hasPrefix(s, prefix) {
    return s.startsWith(prefix);
}

function getLODModelURLForLevel(base_model_url, level)
{
    if (level <= 0)
        return base_model_url;
    else {
        if (base_model_url.startsWith("http:") || base_model_url.startsWith("https:"))
            return base_model_url;

        if (level == 1)
            return removeDotAndExtension(base_model_url) + "_lod1.bmesh"; // LOD models are always saved in BatchedMesh (bmesh) format.
        else
            return removeDotAndExtension(base_model_url) + "_lod2.bmesh";
    }
}


//function worldMaterialMinLODLevel(world_mat) {
//    const MIN_LOD_LEVEL_IS_NEGATIVE_1 = 2;
//    return (world_mat.flags & MIN_LOD_LEVEL_IS_NEGATIVE_1) ? -1 : 0;
//}

function getLODTextureURLForLevel(world_mat, base_texture_url, level, has_alpha) {
    let min_lod_level = world_mat.minLODLevel();

    if (level <= min_lod_level)
        return base_texture_url;
    else {
        // Don't do LOD on mp4 (video) textures (for now).
        // Also don't do LOD with http URLs
        if (hasExtension(base_texture_url, "mp4") || hasPrefix(base_texture_url, "http:") || hasPrefix(base_texture_url, "https:"))
         return base_texture_url;

        // Gifs LOD textures are always gifs.
        // Other image formats get converted to jpg if they don't have alpha, and png if they do.
        let is_gif = hasExtension(base_texture_url, "gif");

        if (level == 0)
            return removeDotAndExtension(base_texture_url) + "_lod0." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
        else if (level == 1)
            return removeDotAndExtension(base_texture_url) + "_lod1." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
        else
            return removeDotAndExtension(base_texture_url) + "_lod2." + (is_gif ? "gif" : (has_alpha ? "png" : "jpg"));
    }
}


// three_mat has type THREE.Material and probably THREE.MeshStandardMaterial
function setThreeJSMaterial(three_mat, world_mat, ob_lod_level) {
    three_mat.color = new THREE.Color(world_mat.colour_rgb.r, world_mat.colour_rgb.g, world_mat.colour_rgb.b);
    three_mat.metalness = world_mat.metallic_fraction.val;
    three_mat.roughness = world_mat.roughness.val;

    three_mat.opacity = world_mat.opacity.val;
    three_mat.transparent = world_mat.opacity.val < 1.0;

    //console.log("world_mat.colour_texture_url:" + world_mat.colour_texture_url);
    if (world_mat.colour_texture_url.length > 0) {
        // function getLODTextureURLForLevel(world_mat, base_texture_url, level, has_alpha)
        //console.log("world_mat.flags: " + world_mat.flags);
        //console.log("world_mat.colourTexHasAlpha: " + world_mat.colourTexHasAlpha());
        let lod_texture_URL = getLODTextureURLForLevel(world_mat, world_mat.colour_texture_url, ob_lod_level, world_mat.colourTexHasAlpha());

        //console.log("lod_texture_URL: " + lod_texture_URL);
        //mesh.material.map = THREE.ImageUtils.loadTexture("resource/" + lod_texture_URL);

        let texture = new THREE.TextureLoader().load("resource/" + lod_texture_URL);
        texture.wrapS = THREE.RepeatWrapping;
        texture.wrapT = THREE.RepeatWrapping;
        three_mat.map = texture;

        three_mat.map.matrixAutoUpdate = false;
        three_mat.map.matrix.set(
            world_mat.tex_matrix.x, world_mat.tex_matrix.y, 0,
            world_mat.tex_matrix.z, world_mat.tex_matrix.w, 0,
            0, 0, 1
        );
    }
}


function addWorldObjectGraphics(world_ob) {

    if (true) {

        //console.log("==================addWorldObjectGraphics (ob uid: " + world_ob.uid + ")=========================")

        let ob_lod_level = getLODLevel(world_ob, camera.position);
        let model_lod_level = getModelLODLevel(world_ob, camera.position);
        //console.log("model_lod_level: " + model_lod_level);

        if(world_ob.compressed_voxels && (world_ob.compressed_voxels.byteLength > 0)) {
            // This is a voxel object

            if (true) {

                // let subsample_factor = 4;
                let values = voxelloading.makeMeshForVoxelGroup(world_ob.compressed_voxels, model_lod_level); // type THREE.BufferGeometry
                let geometry = values[0];
                let subsample_factor = values[1];
                geometry.computeVertexNormals();

                let three_mats = []
                for (let i = 0; i < world_ob.mats.length; ++i) {
                    let three_mat = new THREE.MeshStandardMaterial();
                    setThreeJSMaterial(three_mat, world_ob.mats[i], ob_lod_level);
                    three_mats.push(three_mat);
                }

                const mesh = new THREE.Mesh(geometry, three_mats);
                mesh.position.copy(new THREE.Vector3(world_ob.pos.x, world_ob.pos.y, world_ob.pos.z));
                mesh.scale.copy(new THREE.Vector3(world_ob.scale.x * subsample_factor, world_ob.scale.y * subsample_factor, world_ob.scale.z * subsample_factor));

                let axis = new THREE.Vector3(world_ob.axis.x, world_ob.axis.y, world_ob.axis.z);
                axis.normalize();
                let q = new THREE.Quaternion();
                q.setFromAxisAngle(axis, world_ob.angle);
                mesh.setRotationFromQuaternion(q);

                scene.add(mesh);

                mesh.castShadow = true;
                mesh.receiveShadow = true;
            }
        }
        else if(world_ob.model_url !== "") {

            let url = getLODModelURLForLevel(world_ob.model_url, model_lod_level);

            loadModelAndAddToScene(world_ob, url, ob_lod_level, world_ob.mats, world_ob.pos, world_ob.scale, world_ob.axis, world_ob.angle);
        }
    }
    else {

        let xspan = world_ob.aabb_ws_max.x - world_ob.aabb_ws_min.x;
        let yspan = world_ob.aabb_ws_max.y - world_ob.aabb_ws_min.y;
        let zspan = world_ob.aabb_ws_max.z - world_ob.aabb_ws_min.z;

        const geometry = new THREE.BoxGeometry();
        const material = new THREE.MeshStandardMaterial({ color: 0xaaaaaa });
        const cube = new THREE.Mesh(geometry, material);
        cube.position.copy(new THREE.Vector3(world_ob.aabb_ws_min.x + xspan / 2, world_ob.aabb_ws_min.y + yspan / 2, world_ob.aabb_ws_min.z + zspan / 2));
        cube.scale.copy(new THREE.Vector3(xspan, yspan, zspan));
        scene.add(cube);
    }
}


let url_to_geom_map = new Map(); // Map from model_url to 3.js geometry object.

let loading_model_URL_set = new Set(); // set of URLS


// Returns mesh
function makeMeshAndAddToScene(geometry, mats, pos, scale, world_axis, angle, ob_lod_level) {
    let three_mats = []
    for (let i = 0; i < mats.length; ++i) {
        let three_mat = new THREE.MeshStandardMaterial();
        setThreeJSMaterial(three_mat, mats[i], ob_lod_level);
        three_mats.push(three_mat);
    }

    const mesh = new THREE.Mesh(geometry, three_mats);
    mesh.position.copy(new THREE.Vector3(pos.x, pos.y, pos.z));
    mesh.scale.copy(new THREE.Vector3(scale.x, scale.y, scale.z));

    let axis = new THREE.Vector3(world_axis.x, world_axis.y, world_axis.z);
    axis.normalize();
    let q = new THREE.Quaternion();
    q.setFromAxisAngle(axis, angle);
    mesh.setRotationFromQuaternion(q);

    scene.add(mesh);

    mesh.castShadow = true;
    mesh.receiveShadow = true;

    return mesh;
}


// model_url will have lod level in it, e.g. cube_lod2.bmesh
function loadModelAndAddToScene(world_ob_or_avatar, model_url, ob_lod_level, mats, pos, scale, world_axis, angle) {

    console.log("loadModelAndAddToScene(), model_url: " + model_url);

    world_ob_or_avatar.mesh_state = MESH_LOADING;

    let geom = url_to_geom_map.get(model_url);
    if (geom) {
        console.log("Found already loaded geom for " + model_url);

        let mesh = makeMeshAndAddToScene(geom, mats, pos, scale, world_axis, angle, ob_lod_level);

        //console.log("Loaded mesh '" + model_url + "'.");
        world_ob_or_avatar.mesh = mesh;
        world_ob_or_avatar.mesh_state = MESH_LOADED;
        return;
    }
    else {
        if (loading_model_URL_set.has(model_url)) {
            console.log("model is in loading set, returning.");
            return;
        }

        let encoded_url = encodeURIComponent(model_url);

        var request = new XMLHttpRequest();
        request.open("GET", "/resource/" + encoded_url, true);
        request.responseType = "arraybuffer";

        request.onload = function (oEvent) {

            if (request.status >= 200 && request.status < 300) {
                var array_buffer = request.response;
                if (array_buffer) {

                    console.log("Downloaded the file: '" + model_url + "'!");
                    //try {
                    //console.log("request.status: " + request.status);
                    //console.log("Loading batched mesh from '" + url + "'...");
                    //console.log("array_buffer:");
                    //console.log(array_buffer);
                    let geometry = loadBatchedMesh(array_buffer);

                    console.log("Inserting " + model_url + " into url_to_geom_map");
                    url_to_geom_map.set(model_url, geometry); // Add to url_to_geom_map


                    // Iterate over all objects and avatars, assign this model to all of those
                    for (const world_ob of world_objects.values()) {
                        if (world_ob.model_url === model_url) {

                            let use_ob_lod_level = getLODLevel(world_ob, camera.position); // Used for determining which texture LOD level to load
                            //let use_model_lod_level = getModelLODLevel(world_ob, camera.position);
                            let mesh = makeMeshAndAddToScene(geometry, world_ob.mats, world_ob.pos, world_ob.scale, world_ob.axis, world_ob.angle, use_ob_lod_level);

                            console.log("Made mesh with '" + model_url + "', assigning to world_ob " + world_ob.uid);
                            world_ob.mesh = mesh;
                            world_ob.mesh_state = MESH_LOADED;
                        }
                    }

                    for (const avatar of avatars.values()) {
                        if (avatar.avatar_settings.model_url === model_url) {

                            if (avatar.uid != client_avatar_uid) {
                                let mesh = makeMeshAndAddToScene(geometry, avatar.avatar_settings.materials, avatar.pos, /*scale=*/new Vec3f(1, 1, 1), /*axis=*/new Vec3f(0, 0, 1), /*angle=*/0, /*ob_lod_level=*/0);

                                //console.log("Loaded mesh '" + model_url + "'.");
                                avatar.mesh = mesh;
                                avatar.mesh_state = MESH_LOADED;
                            }
                        }
                    }


                    
                }
            }
            else {
                console.log("Request for '" + model_url + "' returned a non-200 error code: " + request.status);
            }
        };

        request.send(null);

        console.log("Inserting " + model_url + " into loading_model_URL_set");
        loading_model_URL_set.add(model_url);
    }
}



// Parse initial camera location from URL
let initial_pos_x = 1;
let initial_pos_y = 1;
let initial_pos_z = 2;

let params = new URLSearchParams(document.location.search);
if(params.get("x"))
    initial_pos_x = parseFloat(params.get("x"));
if(params.get("y"))
    initial_pos_y = parseFloat(params.get("y"));
if(params.get("z"))
    initial_pos_z = parseFloat(params.get("z"));

let world = ""
if (params.get("world"))
    world = params.get("world")


let client_avatar = new Avatar();
client_avatar.pos = new Vec3d(initial_pos_x, initial_pos_y, initial_pos_z);


THREE.Object3D.DefaultUp.copy(new THREE.Vector3(0, 0, 1));

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);

let renderer_canvas_elem = document.getElementById('rendercanvas');
const renderer = new THREE.WebGLRenderer({ canvas: renderer_canvas_elem, antialias: true, logarithmicDepthBuffer: THREE.logDepthBuf });
renderer.setSize(window.innerWidth, window.innerHeight);

renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;



camera.position.set(initial_pos_x, initial_pos_y, initial_pos_z);
camera.up = new THREE.Vector3(0, 0, 1);

camera.lookAt(camera.position.clone().add(new THREE.Vector3(0, 1, 0)));


const hemiLight = new THREE.HemisphereLight();
hemiLight.color = new THREE.Color(0.4, 0.4, 0.45); // sky colour
hemiLight.groundColor = new THREE.Color(0.5, 0.45, 0.4);
hemiLight.intensity = 1.2;

hemiLight.position.set(0, 20, 20);
scene.add(hemiLight);


const sun_phi = 1.0;
const sun_theta = Math.PI / 4;

//===================== Add directional light =====================
const dirLight = new THREE.DirectionalLight();
dirLight.color = new THREE.Color(0.8, 0.8, 0.8);
dirLight.position.setFromSphericalCoords(1, sun_phi, sun_theta);
dirLight.castShadow = true;
dirLight.shadow.mapSize.width = 2048;
dirLight.shadow.mapSize.height = 2048;
dirLight.shadow.camera.top = 20;
dirLight.shadow.camera.bottom = - 20;
dirLight.shadow.camera.left = - 20;
dirLight.shadow.camera.right = 20;
//dirLight.shadow.camera.near = 0.1;
//dirLight.shadow.camera.far = 40;

scene.add(dirLight);


//===================== Add Sky =====================
let sky = new Sky();
sky.scale.setScalar(450000);
scene.add(sky);

const uniforms = sky.material.uniforms;
uniforms['turbidity'].value = 0.1
uniforms['rayleigh'].value = 0.5
uniforms['mieCoefficient'].value = 0.1
uniforms['mieDirectionalG'].value = 0.5
uniforms['up'].value.copy(new THREE.Vector3(0,0,1));

let sun = new THREE.Vector3();
sun.setFromSphericalCoords(1, sun_phi, sun_theta);

uniforms['sunPosition'].value.copy(sun);


//===================== Add ground plane =====================
{
    let plane_w = 1000;
    const geometry = new THREE.PlaneGeometry(plane_w, plane_w);
    const material = new THREE.MeshStandardMaterial();
    material.color = new THREE.Color(0.9, 0.9, 0.9);
    //material.side = THREE.DoubleSide;

    let texture = new THREE.TextureLoader().load("./obstacle.png");

    texture.anisotropy = renderer.capabilities.getMaxAnisotropy();
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;

    texture.matrixAutoUpdate = false;
    texture.matrix.set(
        plane_w, 0, 0,
        0, plane_w, 0,
        0, 0, 1
    );
   
    material.map = texture;

    const plane = new THREE.Mesh(geometry, material);

    plane.castShadow = true;
    plane.receiveShadow = true;
    
    scene.add(plane);
}



let is_mouse_down = false;
let heading = Math.PI / 2;
let pitch = Math.PI / 2;
let keys_down = new Set();

function camForwardsVec() {
    return new THREE.Vector3(Math.cos(heading) * Math.sin(pitch), Math.sin(heading) * Math.sin(pitch), Math.cos(pitch));
}

function camRightVec() {
    return new THREE.Vector3(Math.sin(heading), -Math.cos(heading), 0);
}

function onDocumentMouseDown() {
    //console.log("onDocumentMouseDown()");
    is_mouse_down = true;
}

function onDocumentMouseUp() {
    //console.log("onDocumentMouseUp()");
    is_mouse_down = false;
}

function onDocumentMouseMove(e) {
    //console.log("onDocumentMouseMove()");
    //console.log(e.movementX);

    if(is_mouse_down){
        let rot_factor = 0.003;

        heading += -e.movementX * rot_factor;
        pitch = Math.max(1.0e-3, Math.min(pitch + e.movementY * rot_factor, Math.PI - 1.0e-3));
    }

    camera.lookAt(camera.position.clone().add(camForwardsVec()));
}

function onKeyDown(e) {
    //console.log("onKeyDown()");
    //console.log("e.code: " + e.code);
    
    keys_down.add(e.code);
}

function onKeyUp(e) {
    //console.log("onKeyUp()");
    //console.log("e.code: " + e.code);
    
    keys_down.delete(e.code);
}


// See https://stackoverflow.com/a/20434960
window.addEventListener('resize', onWindowResize, false);

function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
}

renderer_canvas_elem.addEventListener('mousedown', onDocumentMouseDown, false);
window.addEventListener('mouseup', onDocumentMouseUp, false);
window.addEventListener('mousemove', onDocumentMouseMove, false);
renderer_canvas_elem.addEventListener('keydown', onKeyDown, false);
renderer_canvas_elem.addEventListener('keyup', onKeyUp, false);



function doCamMovement(dt){

    let move_speed = 3.0;
    let turn_speed = 1.0;

    if(keys_down.has('ShiftLeft')){
        move_speed *= 5;
        turn_speed *= 5;
    }

    if(keys_down.has('KeyW') || keys_down.has('ArrowUp')){
        camera.position.addScaledVector(camForwardsVec(), dt * move_speed);
    }
    if(keys_down.has('KeyS') || keys_down.has('ArrowDown')){
        camera.position.addScaledVector(camForwardsVec(), -dt * move_speed);
    }
    if(keys_down.has('KeyA')){
        camera.position.addScaledVector(camRightVec(), -dt * move_speed);
    }
    if(keys_down.has('KeyD')){
        camera.position.addScaledVector(camRightVec(), dt * move_speed);
    }
    if(keys_down.has('Space')){
        camera.position.addScaledVector(new THREE.Vector3(0,0,1), dt * move_speed);
    }
    if(keys_down.has('KeyC')){
        camera.position.addScaledVector(new THREE.Vector3(0,0,1), -dt * move_speed);
    }


    if(keys_down.has('ArrowLeft')){
        heading += dt * turn_speed;

        camera.lookAt(camera.position.clone().add(camForwardsVec()));
    }

    if(keys_down.has('ArrowRight')){
        heading -= dt * turn_speed;

        camera.lookAt(camera.position.clone().add(camForwardsVec()));
    }

    client_avatar.pos.x = camera.position.x;
    client_avatar.pos.y = camera.position.y;
    client_avatar.pos.z = camera.position.z;

    // rotation = (roll, pitch, heading)
    client_avatar.rotation.x = 0;
    client_avatar.rotation.y = pitch;
    client_avatar.rotation.z = heading;
}

function curTimeS() {
    return window.performance.now() * 1.0e-3;
}

let cur_time = curTimeS();

let last_update_URL_time = curTimeS();
let last_avatar_update_send_time = curTimeS();

function animate() {
    let dt = Math.min(0.1, curTimeS() - cur_time);
    cur_time = curTimeS();
    requestAnimationFrame(animate);

    doCamMovement(dt);


    renderer.render(scene, camera);

    // Update URL with current camera position
    if(cur_time > last_update_URL_time + 0.1) {
        let url_path = "/webclient?";
        if (world != "") // Append world if != empty string.
            url_path += "world=" + encodeURIComponent(world) + "&";
        url_path += "x=" + camera.position.x.toFixed(1) + "&y=" + camera.position.y.toFixed(1) + "&z=" + camera.position.z.toFixed(1);
        window.history.replaceState("object or string", "Title", url_path);
        last_update_URL_time = cur_time;
    }


    // Send AvatarTransformUpdate message to server
    if ((client_avatar_uid !== null) &&  cur_time > last_avatar_update_send_time + 0.1) {
        let anim_state = 0;

        let buffer_out = new bufferout.BufferOut();
        buffer_out.writeUInt32(AvatarTransformUpdate);
        buffer_out.writeUInt32(0); // will be updated with length
        writeUID(buffer_out, client_avatar_uid);
        client_avatar.pos.writeToStream(buffer_out);
        client_avatar.rotation.writeToStream(buffer_out);
        buffer_out.writeUInt32(anim_state);
        buffer_out.updateMessageLengthField();
        buffer_out.writeToWebSocket(ws);
        //console.log("Sending avatar update");

        last_avatar_update_send_time = cur_time;
    }


    // Update avatar positions
    for (const avatar of avatars.values()) {

        if (avatar.uid != client_avatar_uid) { // If this is our avatar, don't load it

            if (avatar.mesh_state == MESH_NOT_LOADED) {

                //console.log("Loading avatar model: " + avatar.avatar_settings.model_url);
                //let ob_lod_level = 0;
                //let world_axis = new Vec3f(0, 0, 1);
                //let angle = 0;
                //loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
            }
            else if (avatar.mesh_state == MESH_LOADED) {

                let av_z_rot = avatar.rotation.z + Math.PI / 2;

                let mat = new THREE.Matrix4();
                mat.makeRotationZ(av_z_rot);

                // Set translation part of matrix
                mat.elements[12] = avatar.pos.x;
                mat.elements[13] = avatar.pos.y;
                mat.elements[14] = avatar.pos.z;

                //console.log("pos: " + avatar.pos.x + ", " + avatar.pos.y + ", " + avatar.pos.z);

                let pre_ob_to_world_matrix = new THREE.Matrix4();

                /*
                0	4	8	12
                1	5	9	13
                2	6	10	14
                3	7	11	15
                */
                let m = avatar.avatar_settings.pre_ob_to_world_matrix; // pre_ob_to_world_matrix is in column-major order, set() takes row-major order, so transpose.
                pre_ob_to_world_matrix.set(
                    m[0], m[4], m[8], m[12],
                    m[1], m[5], m[9], m[13],
                    m[2], m[6], m[10], m[14],
                    m[3], m[7], m[11], m[15]
                );

                mat.multiply(pre_ob_to_world_matrix);

                avatar.mesh.matrix.copy(mat);
                avatar.mesh.matrixAutoUpdate = false;
            }
        }
    };
}


animate();
