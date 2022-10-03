/*=====================================================================
webclient.ts
------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

//import { GLTFLoader } from './examples/jsm/loaders/GLTFLoader.js';
import * as THREE from './build/three.module.js';
import { Sky } from './examples/jsm/objects/Sky.js';
import * as voxelloading from './voxelloading.js';
import * as bufferin from './bufferin.js';
import * as bufferout from './bufferout.js';
import { loadBatchedMesh } from './bmeshloading.js';
import * as downloadqueue from './downloadqueue.js';
import BVH, { Triangles } from './physics/bvh.js';
import Caster from './physics/caster.js';
import PhysicsWorld from './physics/world.js';
import { createAABB, transformAABB } from './maths/geometry.js';
import { eq3, fromVector3 } from './maths/vec3.js';

//import { CSM } from './examples/jsm/csm/CSM.js';
//import { CSMHelper } from './examples/jsm/csm/CSMHelper.js';


var ws = new WebSocket("wss://" + window.location.host, "substrata-protocol");
ws.binaryType = "arraybuffer"; // Change binary type from "blob" to "arraybuffer"

// PHYSICS-RELATED
const DEBUG_PHYSICS = true;
const DEBUG_MATERIAL = DEBUG_PHYSICS && true;
const physics_world = new PhysicsWorld()
// END PHYSICS-RELATED

const STATE_INITIAL = 0;
const STATE_READ_HELLO_RESPONSE = 1;
const STATE_READ_PROTOCOL_RESPONSE = 2;
const STATE_READ_CLIENT_AVATAR_UID = 3;

var protocol_state = 0;

const CyberspaceProtocolVersion = 35;
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

const WORLD_MATERIAL_SERIALISATION_VERSION = 7;


const MESH_NOT_LOADED = 0;
const MESH_LOADING = 1;
const MESH_LOADED = 2;

function toString(x) {
    return x.toString();
}

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

function readUInt64(buffer_in): bigint {
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


function readUIDFromStream(buffer_in): bigint {
    return readUInt64(buffer_in);
}


function writeUID(buffer_out, uid) {
    buffer_out.writeUInt64(uid);
}


class Vec2d {
    x: number;
    y: number;

    constructor(x_, y_) {
        this.x = x_;
        this.y = y_;
    }
}

class Vec3f {
    x: number;
    y: number;
    z: number;

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
    x: number;
    y: number;
    z: number;
    w: number;

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
    r: number;
    g: number;
    b: number;

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
    x: number;
    y: number;
    z: number;

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

function readTimeStampFromStream(buffer_in): bigint {
    let version = readUInt32(buffer_in);
    if (version != TIMESTAMP_SERIALISATION_VERSION)
        throw "Unhandled version " + toString(version) + ", expected " + toString(TIMESTAMP_SERIALISATION_VERSION) + ".";

    return readUInt64(buffer_in);
}



class AvatarSettings {
    model_url: string;
    materials: Array<WorldMaterial>;
    pre_ob_to_world_matrix: Array<number>;

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
    uid: bigint;
    name: string;
    pos: Vec3d;
    rotation: Vec3f;
    avatar_settings: AvatarSettings;

    anim_state: number;


    mesh_state: number;
    mesh: THREE.Mesh;

    constructor() {
        //this.uid = BigInt(0); // uint64   // TEMP
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
    parcel_id: number;
    owner_id: number;
    created_time: bigint;
    description: string;
    admin_ids: Array<Number>;
    writer_ids: Array<Number>;
    child_parcel_ids: Array<Number>;
    all_writeable: boolean;
    verts: Array<Vec2d>;
    zbounds: Vec2d;
    flags: number;
    parcel_auction_ids: Array<Number>;
    spawn_point: Vec3d;
    owner_name: string;
    admin_names: Array<string>;
    writer_names: Array<string>;
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

    // Read flags
    parcel.flags = readUInt32(buffer_in)

    // Read spawn_point
    parcel.spawn_point = readVec3dFromStream(buffer_in);

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


export class WorldObject {
    uid: bigint;
    object_type: number;
    model_url: string;
    mats: Array<WorldMaterial>;
    lightmap_url: string;

    script: string;
    content: string;
    target_url: string;

    audio_source_url: string;
    audio_volume: number;

    pos: Vec3d;
    axis: Vec3f;
    angle: number;

    scale: Vec3f;

    created_time: bigint;
    creator_id: number;

    flags: number;

    creator_name: string;

    aabb_ws_min: Vec3f;
    aabb_ws_max: Vec3f;

    max_model_lod_level: number;

    compressed_voxels: Array<number>;

    bvh: BVH
    inv_world: THREE.Matrix4 // We should only calculate the inverse when the world matrix actually changes
    // TODO: Check if we can combine aabb_ws_min/aabb_ws_max
    world_aabb: Float32Array // This is the root bvh AABB node converted to an AABB in world space (will be larger if rotated)


    mesh_state: number;
    mesh: THREE.Mesh;

    constructor() {
        this.mesh_state = MESH_NOT_LOADED;
    }
}

const COLOUR_TEX_HAS_ALPHA_FLAG = 1;
const MIN_LOD_LEVEL_IS_NEGATIVE_1 = 2;

class WorldMaterial {

    colour_texture_url: string;
    emission_texture_url: string;
    colour_rgb: Colour3f;
    emission_rgb: Colour3f;
    roughness: ScalarVal;
    metallic_fraction: ScalarVal;
    opacity: ScalarVal;
    tex_matrix: Matrix2f;
    emission_lum_flux: number;
    flags: number;

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

        this.emission_rgb.writeToStream(stream);
        stream.writeStringLengthFirst(this.emission_texture_url);

        this.roughness.writeToStream(stream);
        this.metallic_fraction.writeToStream(stream);
        this.opacity.writeToStream(stream);

        this.tex_matrix.writeToStream(stream);

        stream.writeFloat(this.emission_lum_flux);

        stream.writeUInt32(this.flags);
    }
}

class ScalarVal {
    val: number;
    texture_url: string;

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
        throw "Unsupported version " + toString(version) + ", expected " + toString(WORLD_MATERIAL_SERIALISATION_VERSION) + ".";

    mat.colour_rgb = readColour3fFromStream(buffer_in);
    mat.colour_texture_url = readStringFromStream(buffer_in);

    mat.emission_rgb = readColour3fFromStream(buffer_in);
    mat.emission_texture_url = readStringFromStream(buffer_in);

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


const MAX_OB_LOAD_DISTANCE_FROM_CAM = 200;


function sendQueryObjectsMessage() {

    const CELL_WIDTH = 200.0;
    const load_distance = MAX_OB_LOAD_DISTANCE_FROM_CAM;

    let begin_x = Math.floor((camera.position.x - load_distance) / CELL_WIDTH);
    let begin_y = Math.floor((camera.position.y - load_distance) / CELL_WIDTH);
    let begin_z = Math.floor((camera.position.z - load_distance) / CELL_WIDTH);
    let end_x   = Math.floor((camera.position.x + load_distance) / CELL_WIDTH);
    let end_y   = Math.floor((camera.position.y + load_distance) / CELL_WIDTH);
    let end_z = Math.floor((camera.position.z + load_distance) / CELL_WIDTH);

    // console.log("begin_x: " + begin_x);
    // console.log("begin_y: " + begin_y);
    // console.log("begin_z: " + begin_z);
    // console.log("end_x: " + end_x);
    // console.log("end_y: " + end_y);
    // console.log("end_z: " + end_z);

    let buffer_out = new bufferout.BufferOut();
    buffer_out.writeUInt32(QueryObjects);
    buffer_out.writeUInt32(0); // message length - to be updated.

    let coords = []
    for (let x = begin_x; x <= end_x; ++x)
        for (let y = begin_y; y <= end_y; ++y)
            for (let z = begin_z; z <= end_z; ++z) {
                coords.push(x);
                coords.push(y);
                coords.push(z);
            }

    buffer_out.writeUInt32(coords.length / 3); // Num cells to query
    for (let i = 0; i < coords.length; ++i)
        buffer_out.writeInt32(coords[i]);

    buffer_out.updateMessageLengthField();
    buffer_out.writeToWebSocket(ws);
}


var parcels = new Map<number, Parcel>();
var world_objects = new Map<bigint, WorldObject>();
var avatars = new Map<bigint, Avatar>();
var client_avatar_uid: bigint = null;

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

                let world_ob: WorldObject = readWorldObjectFromNetworkStreamGivenUID(buffer);
                world_ob.uid = object_uid;

                let dist_from_cam = toThreeVector3(world_ob.pos).distanceTo(camera.position);
                if (dist_from_cam < MAX_OB_LOAD_DISTANCE_FROM_CAM) {
                    addWorldObjectGraphics(world_ob);
                }

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
                    let ob_aabb_longest_len = 2;
                    loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_aabb_longest_len, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
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
                    let ob_aabb_longest_len = 2;
                    loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_aabb_longest_len, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
                }
            }
            else if (msg_type == AvatarDestroyed) {
                const avatar_uid = readUIDFromStream(buffer);

                let avatar = avatars.get(avatar_uid);
                avatars.delete(avatar_uid);

                if (avatar) {

                    appendChatMessage(avatar.name + " left.");
                    console.log("Avatar " + avatar.name + " left");

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

                    // Load some spheres to represent the avatar.



                    console.log("Loading avatar model: " + avatar.avatar_settings.model_url);
                    let ob_lod_level = 0;
                    let world_axis = new Vec3f(0, 0, 1);
                    let angle = 0;
                    let ob_aabb_longest_len = 2;
                    loadModelAndAddToScene(avatar, avatar.avatar_settings.model_url, ob_aabb_longest_len, ob_lod_level, avatar.avatar_settings.materials, avatar.pos, new Vec3f(1, 1, 1), world_axis, angle);
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
    let msg = (<HTMLInputElement>document.getElementById("chat_message")).value;
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
    (<HTMLFormElement>document.getElementById('chatform')).reset(); // Clear chat box
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

function toThreeVector3(v) {
    return new THREE.Vector3(v.x, v.y, v.z);
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



let url_to_geom_map = new Map(); // Map from model_url to 3.js geometry object.

let loading_model_URL_set = new Set(); // set of URLS


let url_to_texture_map = new Map(); // Map from texture url to 3.js Texture object

let loading_texture_URL_set = new Set(); // Set of URL for textures that are being loaded.


let loading_model_URL_to_world_ob_map = new Map(); // Map from a URL of a loading model to a list of WorldObjects using that model.

let loading_texture_URL_to_materials_map = new Map(); // Map from a URL of a loading texture to a list of materials using that texture.


// three_mat has type THREE.Material and probably THREE.MeshStandardMaterial
function setThreeJSMaterial(three_mat, world_mat, ob_pos, ob_aabb_longest_len, ob_lod_level) {
    three_mat.color = new THREE.Color(world_mat.colour_rgb.r, world_mat.colour_rgb.g, world_mat.colour_rgb.b);
    three_mat.metalness = world_mat.metallic_fraction.val;
    three_mat.roughness = world_mat.roughness.val;

    three_mat.side = THREE.DoubleSide; // Enable backface rendering as well.

    three_mat.opacity = (world_mat.opacity.val < 1.0) ? 0.3 : 1.0;
    three_mat.transparent = world_mat.opacity.val < 1.0;

    if (world_mat.opacity.val < 1.0) {
        // Try and make this look vaguely like the native engine transparent shader, which has quite desaturated colours for transparent mats.
        three_mat.color.convertGammaToLinear(2.2);
        three_mat.color.r = 0.6 + three_mat.color.r * 0.4;
        three_mat.color.g = 0.6 + three_mat.color.g * 0.4;
        three_mat.color.b = 0.6 + three_mat.color.b * 0.4;
    }

    //console.log("world_mat.colour_texture_url:" + world_mat.colour_texture_url);
    if (world_mat.colour_texture_url.length > 0) {
        // function getLODTextureURLForLevel(world_mat, base_texture_url, level, has_alpha)
        //console.log("world_mat.flags: " + world_mat.flags);
        //console.log("world_mat.colourTexHasAlpha: " + world_mat.colourTexHasAlpha());
        let color_tex_has_alpha = world_mat.colourTexHasAlpha();
        let lod_texture_URL = getLODTextureURLForLevel(world_mat, world_mat.colour_texture_url, ob_lod_level, color_tex_has_alpha);

        if (color_tex_has_alpha)
            three_mat.alphaTest = 0.5;

        //console.log("lod_texture_URL: " + lod_texture_URL);

        let texture = null;
        if (url_to_texture_map.has(lod_texture_URL)) {

            texture = new THREE.Texture();
            texture.image = url_to_texture_map.get(lod_texture_URL).image; // This texture has already been loaded, use it
        }
        else { // Else texture has not been loaded:

            // Add this material to the list of materials waiting for the texture
            if (!loading_texture_URL_to_materials_map.has(lod_texture_URL)) // Initialise with empty list if needed
                loading_texture_URL_to_materials_map.set(lod_texture_URL, []);
            loading_texture_URL_to_materials_map.get(lod_texture_URL).push(three_mat);

            if (loading_texture_URL_set.has(lod_texture_URL)) { // Are we already loading the texture?
                // Just wait for the texture to load
            }
            else {
                // We are not currently loading the texture, so start loading it:
                // Enqueue downloading of the texture
                let size_factor = downloadqueue.sizeFactorForAABBWSLongestLen(ob_aabb_longest_len);
                download_queue.enqueueItem(new downloadqueue.DownloadQueueItem(toThreeVector3(ob_pos), size_factor, lod_texture_URL, /*is_texture=*/true));

                loading_texture_URL_set.add(lod_texture_URL); // Add to set of loading textures.
            }

            //texture = new THREE.TextureLoader().load("./obstacle.png"); // Use obstacle texture as a loading placeholder.
            texture = new THREE.Texture();
            texture.image = placeholder_texture.image;
        }

        texture.needsUpdate = true; // Seems to be needed to get the texture to show.

        texture.wrapS = THREE.RepeatWrapping;
        texture.wrapT = THREE.RepeatWrapping;

        texture.matrixAutoUpdate = false;
        texture.matrix.set(
            world_mat.tex_matrix.x, world_mat.tex_matrix.y, 0,
            world_mat.tex_matrix.z, world_mat.tex_matrix.w, 0,
            0, 0, 1
        );

        three_mat.map = texture;
    }
}


function addWorldObjectGraphics(world_ob) {

    if (true) {

        //console.log("==================addWorldObjectGraphics (ob uid: " + world_ob.uid + ")=========================")

        let ob_lod_level = getLODLevel(world_ob, camera.position);
        let model_lod_level = getModelLODLevel(world_ob, camera.position);
        let aabb_longest_len = AABBLongestLength(world_ob);
        //console.log("model_lod_level: " + model_lod_level);

        if(world_ob.compressed_voxels && (world_ob.compressed_voxels.byteLength > 0)) {
            // This is a voxel object

            if (true) {

                let values = voxelloading.makeMeshForVoxelGroup(world_ob.compressed_voxels, model_lod_level); // type THREE.BufferGeometry
                let geometry = values[0];
                let subsample_factor = values[1];
                geometry.computeVertexNormals();

                let three_mats = []
                for (let i = 0; i < world_ob.mats.length; ++i) {
                    let three_mat = new THREE.MeshStandardMaterial();
                    setThreeJSMaterial(three_mat, world_ob.mats[i], world_ob.pos, aabb_longest_len, ob_lod_level);
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

            loadModelAndAddToScene(world_ob, url, aabb_longest_len, ob_lod_level, world_ob.mats, world_ob.pos, world_ob.scale, world_ob.axis, world_ob.angle);
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


// Make a THREE.Mesh object, assign it the geometry, and make some three.js materials for it, based on WorldMaterials passed in.
// Returns mesh (THREE.Mesh)
function makeMeshAndAddToScene(geometry/*: THREE.BufferGeometry*/, mats, pos, scale, world_axis, angle, ob_aabb_longest_len, ob_lod_level): THREE.Mesh {

    let use_vert_colours = (geometry.getAttribute('color') !== undefined);

    let three_mats = []

    if(!DEBUG_MATERIAL) {
        for (let i = 0; i < mats.length; ++i) {
            let three_mat = new THREE.MeshStandardMaterial({ vertexColors: use_vert_colours });
            //csm.setupMaterial(three_mat); // TEMP
            setThreeJSMaterial(three_mat, mats[i], pos, ob_aabb_longest_len, ob_lod_level);
            three_mats.push(three_mat);
        }
    } else {
        for (let i = 0; i < mats.length; ++i) {
            const mat = new THREE.MeshLambertMaterial({ color: 'white' })
            // mat.wireframe = false
            mat.flatShading = true
            three_mats.push(mat)
        }
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


let num_resources_downloading = 0; // Total number of resources (models + textures) currently being downloaded.


function startDownloadingResource(download_queue_item) {

    num_resources_downloading++;

    if (download_queue_item.is_texture) {

        const loader = new THREE.TextureLoader();

        loader.load("resource/" + download_queue_item.URL,

            function (texture) { // onLoad callback
                num_resources_downloading--;

                //console.log("Loaded texture '" + download_queue_item.URL + "'.");

                // There should be 1 or more materials that use this texture.
                let waiting_mats = loading_texture_URL_to_materials_map.get(download_queue_item.URL);
                if (!waiting_mats) {
                    console.log("Error: waiting mats was null or false.");
                }
                else {
                    // Assign this texture to all materials waiting for it.
                    for (let z = 0; z < waiting_mats.length; ++z) {
                        let mat = waiting_mats[z];

                        //console.log("Assigning texture '" + download_queue_item.URL + "' to waiting material: " + mat);

                        mat.map.image = texture.image; // Assign the texture image, but not the whole texture, because we want to keep the existing tex matrix etc..
                        mat.map.needsUpdate = true; // Seems to be needed to get the texture to show.
                    }

                    loading_texture_URL_to_materials_map.delete(download_queue_item.URL); // Now that this texture has been downloaded, remove from map
                }

                // Add to our loaded texture map
                url_to_texture_map.set(download_queue_item.URL, texture);
            },

            undefined, // onProgress callback currently not supported

            function (err) { // onError callback
                //console.error('An error happened.');
                num_resources_downloading--;
            }
        );

    }
    else { // Else it's a model to download:
        let model_url = download_queue_item.URL;
        let encoded_url = encodeURIComponent(model_url);

        var request = new XMLHttpRequest();
        request.open("GET", "/resource/" + encoded_url, /*async=*/true);
        request.responseType = "arraybuffer";

        request.onload = function (oEvent) {
            num_resources_downloading--;

            if (request.status >= 200 && request.status < 300) {
                var array_buffer = request.response;
                if (array_buffer) {

                    //console.log("Downloaded the file: '" + model_url + "'!");
                    try {
                        let [geometry, triangles] = loadBatchedMesh(array_buffer);

                        //console.log("Inserting " + model_url + " into url_to_geom_map");
                        url_to_geom_map.set(model_url, geometry); // Add to url_to_geom_map

                        // Assign to any waiting world obs or avatars
                        let waiting_obs = loading_model_URL_to_world_ob_map.get(download_queue_item.URL);
                        if (!waiting_obs) {
                            console.log("Error: waiting obs was null or false:");
                            console.log(waiting_obs);
                        }
                        else {

                            for (let z = 0; z < waiting_obs.length; ++z) {
                                let world_ob_or_avatar = waiting_obs[z];

                                if (world_ob_or_avatar instanceof WorldObject) {

                                    let world_ob = world_ob_or_avatar;
                                    //console.log("Assigning model '" + download_queue_item.URL + "' to world object: " + world_ob);

                                    let use_ob_lod_level = getLODLevel(world_ob, camera.position); // Used for determining which texture LOD level to load
                                    let ob_aabb_longest_len = AABBLongestLength(world_ob);

                                    let mesh: THREE.Mesh = makeMeshAndAddToScene(geometry, world_ob.mats, world_ob.pos, world_ob.scale, world_ob.axis, world_ob.angle, ob_aabb_longest_len, use_ob_lod_level);

                                    if(DEBUG_PHYSICS && triangles != null) {
                                        // For now, build the BVH in the main thread while testing performance
                                        registerPhysicsObject(world_ob, triangles, mesh)
                                    }

                                    world_ob_or_avatar.mesh = mesh;
                                    world_ob_or_avatar.mesh_state = MESH_LOADED;
                                }
                                else if (world_ob_or_avatar instanceof Avatar) {

                                    let avatar = world_ob_or_avatar;
                                    if (avatar.uid != client_avatar_uid) {
                                        console.log('Avatar:', avatar)
                                        let mesh: THREE.Mesh = makeMeshAndAddToScene(geometry, avatar.avatar_settings.materials, avatar.pos, /*scale=*/new Vec3f(1, 1, 1), /*axis=*/new Vec3f(0, 0, 1),
                                            /*angle=*/0, /*ob_aabb_longest_len=*/1.0, /*ob_lod_level=*/0);

                                        //console.log("Loaded mesh '" + model_url + "'.");
                                        avatar.mesh = mesh;
                                        avatar.mesh_state = MESH_LOADED;
                                    }
                                }
                            }

                            loading_model_URL_to_world_ob_map.delete(download_queue_item.URL); // Now that this model has been downloaded, remove from map
                        }
                    }
                    catch (error) { // There was an exception loading/parsing the geometry
                        console.log("exception occurred while loading/parsing the geometry: " + error);
                    }
                }
            }
            else {
                console.log("Request for '" + model_url + "' returned a non-200 error code: " + request.status);
            }
        };

        request.onerror = function (oEvent) {
            num_resources_downloading--;
            console.log("Request for '" + model_url + "' encountered an error: " + request.status);
        };

        request.send(/*body=*/null);
    }
}

// Build the BVH for a new (unregistered) mesh
function registerPhysicsObject (obj: WorldObject, triangles: Triangles, mesh: THREE.Mesh) {
    const model_loaded = physics_world.hasModelBVH(obj.model_url);
    if(!model_loaded) {
       physics_world.addModelBVH(obj.model_url, triangles);
       console.log('creating new bvh:', obj.model_url);
    }

    // Move this to a web worker, initialise the BVH and trigger this code on callback
    obj.bvh = physics_world.getModelBVH(obj.model_url)

    // TODO: Split into two sets of objects, static and dynamic - for now, only static
    obj.inv_world = new THREE.Matrix4();
    mesh.updateMatrixWorld(true);
    obj.inv_world.copy(mesh.matrixWorld);
    obj.inv_world.invert();

    obj.world_aabb = createAABB(obj.aabb_ws_min, obj.aabb_ws_max);
    obj.mesh = mesh
    physics_world.addWorldObject(obj, true);
}

// model_url will have lod level in it, e.g. cube_lod2.bmesh
function loadModelAndAddToScene(world_ob_or_avatar, model_url, ob_aabb_longest_len, ob_lod_level, mats, pos, scale, world_axis, angle) {

    //console.log("loadModelAndAddToScene(), model_url: " + model_url);

    world_ob_or_avatar.mesh_state = MESH_LOADING;

    let geom = url_to_geom_map.get(model_url);
    if (geom) {
        //console.log("Found already loaded geom for " + model_url);

        let mesh: THREE.Mesh = makeMeshAndAddToScene(geom, mats, pos, scale, world_axis, angle, ob_aabb_longest_len, ob_lod_level);

        //console.log("Loaded mesh '" + model_url + "'.");
        world_ob_or_avatar.mesh = mesh;
        world_ob_or_avatar.mesh_state = MESH_LOADED;
        return;
    }
    else {
        if (loading_model_URL_set.has(model_url)) {
            //console.log("model is in loading set.");
        }
        else {
            // Else we were not already loading model.
            // Enqueue downloading of the model
            let size_factor = downloadqueue.sizeFactorForAABBWSLongestLen(ob_aabb_longest_len);
            download_queue.enqueueItem(new downloadqueue.DownloadQueueItem(toThreeVector3(pos), size_factor, model_url, /*is_texture=*/false));

            //console.log("Inserting " + model_url + " into loading_model_URL_set");
            loading_model_URL_set.add(model_url);
        }

        // Add this world ob or avatar to the list of objects waiting for the model
        if (!loading_model_URL_to_world_ob_map.has(model_url)) // Initialise with empty list if needed
            loading_model_URL_to_world_ob_map.set(model_url, []);

        loading_model_URL_to_world_ob_map.get(model_url).push(world_ob_or_avatar);
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
const DEFAULT_FOV = 75
const DEFAULT_AR = window.innerWidth / window.innerHeight
const DEFAULT_NEAR = 0.1
const DEFAULT_FAR = 1000.0
const camera = new THREE.PerspectiveCamera(DEFAULT_FOV, DEFAULT_AR, DEFAULT_NEAR, DEFAULT_FAR);

if(DEBUG_PHYSICS) {
    physics_world.scene = scene
}


let renderer_canvas_elem = document.getElementById('rendercanvas');
const renderer = new THREE.WebGLRenderer({ canvas: renderer_canvas_elem, antialias: true, logarithmicDepthBuffer: THREE.logDepthBuf });
renderer.setSize(window.innerWidth, window.innerHeight);

// Use linear tone mapping with a scale less than 1, which seems to be needed to make the sky model look good.
renderer.toneMapping = THREE.LinearToneMapping;
renderer.toneMappingExposure = 0.5;

renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;



camera.position.set(initial_pos_x, initial_pos_y, initial_pos_z);
camera.up = new THREE.Vector3(0, 0, 1);

camera.lookAt(camera.position.clone().add(new THREE.Vector3(0, 1, 0)));


const hemiLight = new THREE.HemisphereLight();
hemiLight.color = new THREE.Color(0.4, 0.4, 0.45); // sky colour
hemiLight.groundColor = new THREE.Color(0.5, 0.45, 0.4);
hemiLight.intensity = 1.2 * 2;

hemiLight.position.set(0, 20, 20);
scene.add(hemiLight);


const sun_phi = 1.0;
const sun_theta = Math.PI / 4;

//===================== Add directional light =====================
const sundir = new THREE.Vector3();
sundir.setFromSphericalCoords(1, sun_phi, sun_theta);
const dirLight = new THREE.DirectionalLight();
dirLight.color = new THREE.Color(0.8, 0.8, 0.8);
dirLight.intensity = 2;
dirLight.position.copy(sundir);
dirLight.castShadow = true;
dirLight.shadow.mapSize.width = 2048;
dirLight.shadow.mapSize.height = 2048;
dirLight.shadow.camera.top = 20;
dirLight.shadow.camera.bottom = - 20;
dirLight.shadow.camera.left = - 20;
dirLight.shadow.camera.right = 20;

dirLight.shadow.bias = -0.001; // Needed when backfaces are drawn.
//dirLight.shadow.camera.near = 0.1;
//dirLight.shadow.camera.far = 40;

scene.add(dirLight);

// Create a helper to visualise the shadow camera.
//const camera_helper = new THREE.CameraHelper(dirLight.shadow.camera);
//scene.add(camera_helper);


//let from_sun_dir = new THREE.Vector3();
//from_sun_dir.copy(sundir);
//from_sun_dir.negate();

// cascaded shadow maps
//const csm = new CSM({
//    //fade: true,
//    //near: 1,
//    //far: 100,//camera.far,
//    //far: camera.far,
//    //maxFar: 60,
//    //cascades: 4,
//    shadowMapSize: 2048,
//    lightDirection: from_sun_dir,
//    camera: camera,
//    parent: scene,
//    lightIntensity: 0.5
//});

//const csmHelper = new CSMHelper(csm);
//csmHelper.displayFrustum = true;
//csmHelper.displayPlanes = true;
//csmHelper.displayShadowBounds = true;
//scene.add(csmHelper);


//for (let i = 0; i < csm.lights.length; i++) {
//    //csm.lights[i].shadow.camera.near = 1;
//    //csm.lights[i].shadow.camera.far = 2000;
//   // csm.lights[i].shadow.camera.updateProjectionMatrix();
//
//   // csm.lights[i].color = new THREE.Color(1.0, 0.8, 0.8);
//}



//===================== Add Sky =====================
let sky = new Sky();
sky.scale.setScalar(450000); // No idea what this does, seems to be needed to show the sky tho.
scene.add(sky);

const uniforms = sky.material.uniforms;
uniforms['turbidity'].value =  0.4;
//uniforms['rayleigh'].value = 0.5;
//uniforms['mieCoefficient'].value = 0.1;
//uniforms['mieDirectionalG'].value = 0.5;
uniforms['up'].value.copy(new THREE.Vector3(0,0,1));

let sun = new THREE.Vector3();
sun.setFromSphericalCoords(1, sun_phi, sun_theta);

uniforms['sunPosition'].value.copy(sun);


let placeholder_texture = new THREE.TextureLoader().load("./obstacle.png");

//===================== Add ground plane quads =====================
// Use multiple quads to improve z fighting.
{
    let half_res = 5;
    for (let x = -half_res; x <= half_res; ++x)
        for (let y = -half_res; y <= half_res; ++y) {

            let plane_w = 200;
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
            plane.position.x = x * plane_w;
            plane.position.y = y * plane_w;

            plane.castShadow = true;
            plane.receiveShadow = true;

            scene.add(plane);
        }
}




let download_queue = new downloadqueue.DownloadQueue();



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

// Setup the ray caster if we're debugging the physics implementation
const caster = DEBUG_PHYSICS ? new Caster(renderer, {
    debugRay: false,
    near: DEFAULT_NEAR,
    far: DEFAULT_FAR,
    fov: DEFAULT_FOV,
    camera: camera,
    camRightFnc: camRightVec,
    camDirFnc: camForwardsVec
}) : null

if(DEBUG_PHYSICS && caster) physics_world.caster = caster;

function onDocumentMouseDown(ev: MouseEvent) {
    is_mouse_down = true;
}

function onDocumentMouseUp() {
    //console.log("onDocumentMouseUp()");
    is_mouse_down = false;
}

function onDocumentMouseMove(e) {
    //console.log("onDocumentMouseMove()");
    //console.log(e.movementX);

    if(DEBUG_PHYSICS && caster) {
        const ray = caster.getPickRay(e.offsetX, e.offsetY);
        if(ray != null) {
            const [origin, dir] = ray;
            physics_world.traceRay(origin, dir);
            const sphere = new Float32Array([origin.x, origin.y, origin.z, .1]);
            physics_world.traceSphere(sphere, fromVector3(dir))
            // const result = physics_world.traceSphereWorld(sphere, fromVector3(dir))
            // console.log('result:', result)
        }
    }

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
let last_queue_sort_time = curTimeS() - 100;

function animate() {
    let dt = Math.min(0.1, curTimeS() - cur_time);
    cur_time = curTimeS();
    requestAnimationFrame(animate);

    doCamMovement(dt);


    {
        // Sort download queue (by distance from camera)
        if (cur_time - last_queue_sort_time > 0.5)
        {
            //let sort_start_time = curTimeS();
            download_queue.sortQueue(camera.position);
            //let sort_duration = curTimeS() - sort_start_time;
            //console.log("Sorting download queue took " + (sort_duration * 1.0e3) + " ms (num obs in queue: " + download_queue.items.length + ")");

            last_queue_sort_time = cur_time;
        }

        // If there are less than N resources currently downloading, and there are items in the to-download queue, start downloading some of them.
        const MAX_CONCURRENT_DOWNLOADS = 10;
        if (num_resources_downloading < MAX_CONCURRENT_DOWNLOADS && download_queue.items.length > 0) {

            let num_to_dequeue = Math.min(MAX_CONCURRENT_DOWNLOADS - num_resources_downloading, download_queue.items.length);
            for (let z = 0; z < num_to_dequeue; ++z) {
                let item = download_queue.dequeueItem();
                startDownloadingResource(item);
            }
        }
    }



    // Update shadow map 'camera' so that the shadow map volume is positioned around the camera.
    {
        let sun_right = new THREE.Vector3();
        sun_right.crossVectors(new THREE.Vector3(0, 0, 1), sundir);
        sun_right.normalize();
        let sun_up = new THREE.Vector3();
        sun_up.crossVectors(sundir, sun_right);

        let cam_dot_sun_right = sun_right.dot(camera.position);
        let cam_dot_sun_up = sun_up.dot(camera.position);
        let cam_dot_sun = -sundir.dot(camera.position);

        //console.log("cam_dot_sun_up: " + cam_dot_sun_up);
        //console.log("cam_dot_sun_right: " + cam_dot_sun_right);

        let shadow_half_w = 80;
        dirLight.shadow.camera.top    = cam_dot_sun_up + shadow_half_w;
        dirLight.shadow.camera.bottom = cam_dot_sun_up - shadow_half_w;
        dirLight.shadow.camera.left  = cam_dot_sun_right - shadow_half_w;
        dirLight.shadow.camera.right = cam_dot_sun_right + shadow_half_w;
        dirLight.shadow.camera.near = cam_dot_sun - 100;
        dirLight.shadow.camera.far = cam_dot_sun + 100;
        dirLight.shadow.camera.updateProjectionMatrix();

        //if (camera_helper)
        //    camera_helper.update();

        //csm.update()
        //csm.updateFrustums();
        //csm.update(camera.matrix);
        //csmHelper.update()
    }


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
