/*=====================================================================
webclient.js
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/

import { GLTFLoader } from './examples/jsm/loaders/GLTFLoader.js';
import * as THREE from './build/three.module.js';
import { Sky } from './examples/jsm/objects/Sky.js';
import * as voxelloading from './voxelloading.js';

var ws = new WebSocket("ws://localhost", "echo-protocol");
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
const QueryObjects = 3020; // Client wants to query objects in certain grid cells
const ObjectInitialSend = 3021;
const ParcelCreated = 3100;
const TimeSyncMessage = 9000;

const WorldObject_ObjectType_VoxelGroup = 2;

let WORLD_MATERIAL_SERIALISATION_VERSION = 6

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



// Then you can send a message
ws.onopen = function () {
    console.log("onopen()");

    //TEMP:
    //let a = new Uint8Array(10000);
    //for (let i = 0; i < a.length; ++i)
    //    a[i] = i % 256;
    //ws.send(a);

	ws.send(new Uint32Array([CyberspaceHello]));

	
	ws.send(new Uint32Array([CyberspaceProtocolVersion]));

	var ConnectionTypeUpdates = 500;
	ws.send(new Uint32Array([ConnectionTypeUpdates]));

    writeStringToWebSocket(ws, ""); // World to connect to

    sendQueryObjectsMessage();
};


class BufferIn {
    constructor(array_buffer_) {
        this.array_buffer = array_buffer_;
        this.data_view = new DataView(array_buffer_);
        this.read_index = 0;
    }

    endOfStream() {
        return this.read_index >= this.array_buffer.byteLength;
    }

    readData(len) {
        var res = this.array_buffer.slice(this.read_index, this.read_index + len);
        this.read_index += len;
        return res;
    }
}

function readInt32(buffer_in) {
    var x = buffer_in.data_view.getInt32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 4;
    return x;
}

function readUInt32(buffer_in) {
    var x = buffer_in.data_view.getUint32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 4;
    return x;
}

function readUInt64(buffer_in) {
    var x = buffer_in.data_view.getBigUint64(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 8;
    return x;
}

function readFloat(buffer_in) {
    var x = buffer_in.data_view.getFloat32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 4;
    return x;
}

function readDouble(buffer_in) {
    var x = buffer_in.data_view.getFloat64(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
    buffer_in.read_index += 8;
    return x;
}


function readUIDFromStream(buffer_in) {
    return readUInt64(buffer_in);
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
}

class Matrix2f {
    constructor(x_, y_, z_, w_) {
        this.x = x_;
        this.y = y_;
        this.z = z_;
        this.w = w_;
    }
}
class Colour3f {
    constructor(x_, y_, z_) {
        this.r = x_;
        this.g = y_;
        this.b = z_;
    }
}

class Vec3d {
    constructor(x_, y_, z_) {
        this.x = x_;
        this.y = y_;
        this.z = z_;
    }
}

function readVec2dFromStream(buffer_in) {
    var x = readDouble(buffer_in);
    var y = readDouble(buffer_in);
    return new Vec2d(x, y);
}

function readVec3fFromStream(buffer_in) {
    var x = readFloat(buffer_in);
    var y = readFloat(buffer_in);
    var z = readFloat(buffer_in);
    return new Vec3f(x, y, z);
}

function readVec3dFromStream(buffer_in) {
    var x = readDouble(buffer_in);
    var y = readDouble(buffer_in);
    var z = readDouble(buffer_in);
    return new Vec3d(x, y, z);
}

function readColour3fFromStream(buffer_in) {
    var x = readFloat(buffer_in);
    var y = readFloat(buffer_in);
    var z = readFloat(buffer_in);
    return new Colour3f(x, y, z);
}

function readMatrix2fFromStream(buffer_in) {
    var x = readFloat(buffer_in);
    var y = readFloat(buffer_in);
    var z = readFloat(buffer_in);
    var w = readFloat(buffer_in);
    return new Matrix2f(x, y, z, w);
}

function readStringFromStream(buffer_in) {
    var len = readUInt32(buffer_in); // Read length in bytes

    //console.log("readStringFromStream len: " + len)

    var utf8_array = new Int8Array(buffer_in.array_buffer, /*byteoffset=*/buffer_in.read_index, /*length=*/len);

    //console.log("readStringFromStream utf8_array: " + utf8_array)

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
    var version = readUInt32(buffer_in);
    if (version != TIMESTAMP_SERIALISATION_VERSION)
        throw "Unhandled version " + toString(version) + ", expected " + toString(TIMESTAMP_SERIALISATION_VERSION) + ".";

    return readUInt64(buffer_in);
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
}

class ScalarVal {
    constructor(val_, texture_url_) {
        this.val = val_;
        this.texture_url = texture_url_;
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

    //console.log("ob: ", ob)

    ob.object_type = readUInt32(buffer_in);
    ob.model_url = readStringFromStream(buffer_in);
    // Read num_mats
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
        console.log("got voxel ob!");
        

        // Read compressed voxel data
        let voxel_data_size = readUInt32(buffer_in);
        if (voxel_data_size > 1000000)
            throw "Invalid voxel_data_size (too large): " + toString(voxel_data_size);

        console.log("voxel_data_size: " + voxel_data_size)
        if(voxel_data_size > 0)
        {
            // Read voxel data
            ob.compressed_voxels = buffer_in.readData(voxel_data_size);

            // TEMP: decompress

            console.log("ob.compressed_voxels:");
            console.log(ob.compressed_voxels)
            //
            //let decompressed_voxels = fzstd.decompress(new Uint8Array(ob.compressed_voxels));
            //
            //console.log("decompressed_voxels: " + (typeof decompressed_voxels));
            //console.log(decompressed_voxels)
            //
            //console.log("Decompressed voxel data, compressed size: " + voxel_data_size + ", decompressed size: " + decompressed_voxels.length)
        }
    }

    return ob;
}

//function readUInt32(data, read_index) {
//    if (data instanceof ArrayBuffer) {
//        const view = new DataView(data);
//        return view.getInt32(/*byte offset=*/0, /*little endian=*/true);
//    }
//    else
//        throw "data was not an ArrayBuffer";
//}


class BufferOut {
    //constructor() {
    //    this.data = [];//new Uint8Array([]);
    //}

    //writeUInt32(x) {
    //    buf = new ArrayBuffer(4)
    //    const dataView = new DataView(buf);
    //    dataView.setUint32(/*byte offset=*/0, x, /*little endian=*/false);
    //    this.data.push(dataView.getUint8(0));
    //    this.data.push(dataView.getUint8(1));
    //    this.data.push(dataView.getUint8(2));
    //    this.data.push(dataView.getUint8(3));
    //}

    constructor() {
        this.data = new ArrayBuffer(/*length=*/256);//  new Uint8Array(/*length=*/32);
        this.data_view = new DataView(this.data);
        this.size = 0;
    }

    checkForResize(newsize) {
        if (newsize > this.data.byteLength) {
            //console.log("BufferOut: resizing data to size " + this.data.byteLength * 2 + " B")
            // Resize data
            //let newdata = new Uint8Array(/*length=*/this.data.byteLength * 2); // alloc new array
            let olddata = this.data;
            let newdata = new ArrayBuffer(/*length=*/this.data.byteLength * 2); // alloc new array

            // copy old data to new data
            //for (let i = 0; i < this.data.byteLength; ++i)
           //     newdata[i] = this.data[i];

            this.data = newdata;
            this.data_view = new DataView(this.data);

            let old_data_view = new DataView(olddata);

            for (let i = 0; i < olddata.byteLength; ++i)
                this.data_view.setUint8(i, old_data_view.getInt8(i));
        }
    }

    writeInt32(x) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setInt32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeUInt32(x) {
        this.checkForResize(/*newsize=*/this.size + 4);
        
        this.data_view.setUint32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    updateMessageLengthField() {
        this.data_view.setUint32(/*byte offset=*/4, /*value=*/this.size, /*little endian=*/true);
    }

    writeToWebSocket(web_socket) {

        // Trim buffer down to actual used part
        //let newdata = new Uint8Array(/*length=*/this.size); // alloc new array
        //for (let i = 0; i < this.size; ++i)
        //    newdata[i] = this.data[i];


        console.log("writeToWebSocket(): this.size:" + this.size)

        let trimmed = this.data.slice(0, this.size);

       // web_socket.send(newdata);
        web_socket.send(trimmed);
    }
}


function sendQueryObjectsMessage() {

    {
        let buffer_out = new BufferOut();
        buffer_out.writeUInt32(QueryObjects);
        buffer_out.writeUInt32(0); // message length - to be updated.
        let r = 4;
        buffer_out.writeUInt32(2 * (2 * r + 1) * (2 * r + 1)); // Num cells to query

        for (let x = -r; x <= r; ++x)
            for (let y = -r; y <= r; ++y) {
                buffer_out.writeInt32(x); buffer_out.writeInt32(y); buffer_out.writeInt32(0);
                buffer_out.writeInt32(x); buffer_out.writeInt32(y); buffer_out.writeInt32(-1);
            }

        buffer_out.updateMessageLengthField();
        buffer_out.writeToWebSocket(ws);
    }
}


var parcels = {};
var world_objects = {};

//Log the messages that are returned from the server
ws.onmessage = function (event) {
    //console.log("onmessage()");
    //console.log("From Server:" + event.data + ", event.data.byteLength: " + event.data.byteLength);


    //TEMP:
    /*let a = new Uint8Array(event.data);
    console.log("read Uint8Array:", a);
    if (a.length != 10000)
        throw "incorrect a length: " + a.length;
    for (let i = 0; i < a.length; ++i)
        if (a[i] != i % 256)
            throw "a[i] is wrong.";
    console.log("Uint8Array a was correct");
    return;*/


    let z = 0;
    let buffer = new BufferIn(event.data);
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
            var client_avatar_uid = readUIDFromStream(buffer);

            //console.log("Read client_avatar_uid from server: " + client_avatar_uid);
            protocol_state = STATE_READ_CLIENT_AVATAR_UID;
        }
        else if (protocol_state == STATE_READ_CLIENT_AVATAR_UID) {

            var msg_type = readUInt32(buffer);
            var msg_len = readUInt32(buffer);
            //console.log("Read msg_type: " + msg_type + ", len: " + msg_len);

            if (msg_type == TimeSyncMessage) {
                var global_time = readDouble(buffer);
                //console.log("Read TimeSyncMessage with global time: " + global_time);
            }
            else if (msg_type == ParcelCreated) {
                var parcel_id = readParcelIDFromStream(buffer);
                //console.log("parcel_id: ", parcel_id);
                var parcel = readParcelFromNetworkStreamGivenID(buffer);
                parcel.parcel_id = parcel_id;

                parcels[parcel_id] = parcel;

                //console.log("Read ParcelCreated msg, parcel_id: " + parcel_id);

                //addParcelGraphics(parcel)
            }
            else if (msg_type == ObjectInitialSend) {
                // console.log("received ObjectInitialSend...");

                let object_uid = readUIDFromStream(buffer);

                let world_ob = readWorldObjectFromNetworkStreamGivenUID(buffer);
                world_ob.uid = object_uid;

                addWorldObjectGraphics(world_ob);

                world_objects[object_uid] = world_ob;
                //console.log("Read ObjectInitialSend msg, object_uid: " + object_uid);
            }
            else {
                // Unhandled message type, skip over it.
                //console.log("Unhandled message type " + msg_type);
                buffer.read_index += msg_len - 8; // We have already read the type and len (uint32 * 2), skip over remaining data in msg.
            }
        }
        else
            throw "invalid protocol_state";

        //console.log("ping, z:", z);
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

//while (1) {

//	console.log("boop");
//}

//Sending a simple string message
//ws.send("HelloHelloIsThereAnyoneThere");

//const canvas = document.getElementById("renderCanvas"); // Get the canvas element
//const engine = new BABYLON.Engine(canvas, true); // Generate the BABYLON 3D engine


//var shadowGenerator;

// Add your code here matching the playground format
//const createScene = function () {

//    const scene = new BABYLON.Scene(engine);

//    //BABYLON.SceneLoader.ImportMeshAsync("", "https://assets.babylonjs.com/meshes/", "box.babylon");
//    //BABYLON.SceneLoader.ImportMeshAsync("", "D: \\models\\readyplayerme_avatar_animation_18.glb", "box.babylon");

//    const camera = new BABYLON.ArcRotateCamera("camera", -Math.PI / 2, Math.PI / 2.5, 15, new BABYLON.Vector3(0, 0, 0));
//    camera.attachControl(canvas, true);
//   // const light = new BABYLON.HemisphericLight("light", new BABYLON.Vector3(1, 1, 0));
//    var light = new BABYLON.DirectionalLight("dirLight", new BABYLON.Vector3(-1, -1, -1), scene);
//    light.intensity  = 0.6;


//    var ambient_light = new BABYLON.HemisphericLight("HemiLight", new BABYLON.Vector3(0, 1, 0), scene);
//    ambient_light.intensity = 0.5;
//    ambient_light.groundColor = new BABYLON.Color3(0.3, 0.4, 0.5);
//    shadowGenerator = new BABYLON.ShadowGenerator(2048, light);
//    shadowGenerator.useBlurExponentialShadowMap = true;

//    const plane = BABYLON.MeshBuilder.CreatePlane("plane", { width: 2000, height: 2000}, scene);
//    plane.rotation.x = Math.PI / 2;
//    plane.receiveShadows = true;

    

//    return scene;
//};

// Add your code here matching the playground format

//const scene = createScene(); // Call the createScene function


function addParcelGraphics(parcel) {
    //Polygon shape in XZ plane

    let min_y = parcel.zbounds.x; // babylon y = substrata z
    let parcel_depth = parcel.zbounds.y - parcel.zbounds.x;
    //const shape = [
    //    new BABYLON.Vector3(4, min_y, -4),
    //    new BABYLON.Vector3(2, min_y, 0),
    //    new BABYLON.Vector3(5, min_y, 2),
    //    new BABYLON.Vector3(1, min_y, 2),
    //
    //];

    //const extrudedPolygon = BABYLON.MeshBuilder.ExtrudePolygon("polygon", { shape: shape, depth: parcel_depth, sideOrientation: BABYLON.Mesh.DOUBLESIDE });

    vertices = []
    indices = []

    // Sides of parcel
    for (let i = 0; i < 4; ++i) {
        let v = parcel.verts[i];
        let next_v = parcel.verts[(i + 1) % 4];

        let v0 = new BABYLON.Vector3(v.x, v.y, parcel.zbounds.x);
        let v1 = new BABYLON.Vector3(next_v.x, next_v.y, parcel.zbounds.x);
        let v2 = new BABYLON.Vector3(next_v.x, next_v.y, parcel.zbounds.y);
        let v3 = new BABYLON.Vector3(v.x, v.y, parcel.zbounds.y);

        let face = i;
        vertices[face * 4 + 0] = v0;
        vertices[face * 4 + 1] = v1;
        vertices[face * 4 + 2] = v2;
        vertices[face * 4 + 3] = v3;

        indices[face * 6 + 0] = face * 4 + 0;
        indices[face * 6 + 1] = face * 4 + 1;
        indices[face * 6 + 2] = face * 4 + 2;

        indices[face * 6 + 3] = face * 4 + 0;
        indices[face * 6 + 4] = face * 4 + 2;
        indices[face * 6 + 5] = face * 4 + 3;
    }

    // Bottom
    {
        let v0 = new BABYLON.Vector3(parcel.verts[0].x, parcel.verts[0].y, parcel.zbounds.x);
        let v1 = new BABYLON.Vector3(parcel.verts[3].x, parcel.verts[3].y, parcel.zbounds.x);
        let v2 = new BABYLON.Vector3(parcel.verts[2].x, parcel.verts[2].y, parcel.zbounds.x);
        let v3 = new BABYLON.Vector3(parcel.verts[1].x, parcel.verts[1].y, parcel.zbounds.x);

        let face = 4;
        vertices[face * 4 + 0] = v0;
        vertices[face * 4 + 1] = v1;
        vertices[face * 4 + 2] = v2;
        vertices[face * 4 + 3] = v3;

        indices[face * 6 + 0] = face * 4 + 0;
        indices[face * 6 + 1] = face * 4 + 1;
        indices[face * 6 + 2] = face * 4 + 2;

        indices[face * 6 + 3] = face * 4 + 0;
        indices[face * 6 + 4] = face * 4 + 2;
        indices[face * 6 + 5] = face * 4 + 3;
    }

    // Top
    {
        let v0 = new BABYLON.Vector3(parcel.verts[0].x, parcel.verts[0].y, parcel.zbounds.y);
        let v1 = new BABYLON.Vector3(parcel.verts[1].x, parcel.verts[1].y, parcel.zbounds.y);
        let v2 = new BABYLON.Vector3(parcel.verts[2].x, parcel.verts[2].y, parcel.zbounds.y);
        let v3 = new BABYLON.Vector3(parcel.verts[3].x, parcel.verts[3].y, parcel.zbounds.y);

        let face = 5;
        vertices[face * 4 + 0] = v0;
        vertices[face * 4 + 1] = v1;
        vertices[face * 4 + 2] = v2;
        vertices[face * 4 + 3] = v3;

        indices[face * 6 + 0] = face * 4 + 0;
        indices[face * 6 + 1] = face * 4 + 1;
        indices[face * 6 + 2] = face * 4 + 2;

        indices[face * 6 + 3] = face * 4 + 0;
        indices[face * 6 + 4] = face * 4 + 2;
        indices[face * 6 + 5] = face * 4 + 3;
    }

    positions = []
    for (let i = 0; i < vertices.length; ++i) {
        positions.push(vertices[i].x);
        positions.push(vertices[i].y);
        positions.push(vertices[i].z);
    }


   /* var customMesh = new BABYLON.Mesh("custom", scene);

    //var uvs = [0, 1, 0, 0, 1, 0];

    var normals = [];
    BABYLON.VertexData.ComputeNormals(positions, indices, normals);

    var vertexData = new BABYLON.VertexData();

    vertexData.positions = positions;
    vertexData.indices = indices;
    vertexData.normals = normals;
    //vertexData.uvs = uvs;

    vertexData.applyToMesh(customMesh);

    var mat = new BABYLON.StandardMaterial("mat", scene);
    mat.backFaceCulling = false;
    mat.twoSidedLighting = true;
    customMesh.material = mat;


    shadowGenerator.addShadowCaster(customMesh);
    customMesh.receiveShadows = true;*/
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


//function decompressVoxels(compressed_voxels) {
//    return fzstd.decompress(new Uint8Array(compressed_voxels));
//
//    //console.log("decompressed_voxels: " + (typeof decompressed_voxels));
//    //console.log(decompressed_voxels)
//    //
//    //console.log("Decompressed voxel data, compressed size: " + voxel_data_size + ", decompressed size: " + decompressed_voxels.length)
//}


function setThreeJSMaterial(three_mat, world_mat) {
    three_mat.color = new THREE.Color(world_mat.colour_rgb.r, world_mat.colour_rgb.g, world_mat.colour_rgb.b);
    three_mat.metalness = world_mat.metallic_fraction.val;
    three_mat.roughness = world_mat.roughness.val;
}


function addWorldObjectGraphics(world_ob) {

    //if (world_ob.model_url === "")
    //    return;

    //return;
    //if (world_ob.uid == 148313) {
    if (true) {

        console.log("==================addWorldObjectGraphics (ob uid: " + world_ob.uid + ")=========================")

        let ob_lod_level = getLODLevel(world_ob, camera.position);
        let model_lod_level = getModelLODLevel(world_ob, camera.position);
        console.log("model_lod_level: " + model_lod_level);

        console.log("world_ob.compressed_voxels:");
        console.log(world_ob.compressed_voxels);

        if(world_ob.compressed_voxels && (world_ob.compressed_voxels.byteLength > 0)) {
            // This is a voxel object

            let subsample_factor = 1;
            let geometry = voxelloading.makeMeshForVoxelGroup(world_ob.compressed_voxels, subsample_factor); // type THREE.BufferGeometry
            geometry.computeVertexNormals();

            let three_mats = []
            for(let i=0; i<world_ob.mats.length; ++i)
            {
                let three_mat = new THREE.MeshStandardMaterial();
                setThreeJSMaterial(three_mat, world_ob.mats[i]);
                three_mats.push(three_mat);
            }

            const mesh = new THREE.Mesh(geometry, three_mats);
            mesh.position.copy(new THREE.Vector3(world_ob.pos.x, world_ob.pos.y, world_ob.pos.z));
            mesh.scale.copy(new THREE.Vector3(world_ob.scale.x, world_ob.scale.y, world_ob.scale.z));

            let axis = new THREE.Vector3(world_ob.axis.x, world_ob.axis.y, world_ob.axis.z);
            axis.normalize();
            let q = new THREE.Quaternion();
            q.setFromAxisAngle(axis, world_ob.angle);
            mesh.setRotationFromQuaternion(q);

            scene.add(mesh);

            mesh.castShadow = true;
            mesh.receiveShadow = true;

//            let decompressed_voxels_uint8 = decompressVoxels(world_ob.compressed_voxels);
//            console.log("decompressed_voxels_uint8:");
//            console.log(decompressed_voxels_uint8);
//
//            // A voxel is
//            //Vec3<int> pos;
//	        //int mat_index; // Index into materials
//            //let voxel_ints = new Uint32Array(decompressed_voxels))
//
//            let voxel_data = new Int32Array(decompressed_voxels_uint8.buffer);
//
//            let voxels_out = []
//
//            console.log("voxel_data:");
//            console.log(voxel_data);
//
//            let cur_x = 0;
//            let cur_y = 0;
//            let cur_z = 0;
//
//            let read_i = 0;
//	        let num_mats = voxel_data[read_i++];
//            if(num_mats > 2000)
//                throw "Too many voxel materials";
//	        
//            for(let m=0; m<num_mats; ++m)
//	        {
//		        let count = voxel_data[read_i++];
//		        for(let i=0; i<count; ++i)
//		        {
//			        //Vec3<int> relative_pos;
//			       // instream.readData(&relative_pos, sizeof(Vec3<int>));
//                    let rel_x = voxel_data[read_i++];
//                    let rel_y = voxel_data[read_i++];
//                    let rel_z = voxel_data[read_i++];
//
//			        //const Vec3<int> pos = current_pos + relative_pos;
//                    let v_x = cur_x + rel_x;
//                    let v_y = cur_y + rel_y;
//                    let v_z = cur_z + rel_z;
//
//			        //group_out.voxels.push_back(Voxel(pos, m));
//                    voxels_out.push(v_x);
//                    voxels_out.push(v_y);
//                    voxels_out.push(v_z);
//                    voxels_out.push(m);
//
//                    console.log("Added voxel at " + v_x + ", " + v_y + ", " + v_z + " with mat " + m);
//
//			        cur_x = v_x;
//                    cur_y = v_y;
//                    cur_z = v_z;
//		        }
 //           }

        }
        else if(world_ob.model_url !== "") {

            let url = getLODModelURLForLevel(world_ob.model_url, model_lod_level);


            console.log("LOD model URL: " + url);

            if (filenameExtension(url) != "glb")
                url += ".glb";

            /* let encoded_url = encodeURIComponent(url);
 
             var oReq = new XMLHttpRequest();
             oReq.open("GET", "/resource/" + encoded_url, true);
             oReq.responseType = "arraybuffer";
 
 
             oReq.onload = function (oEvent) {
                 var arrayBuffer = oReq.response; // Note: not oReq.responseText
                 if (arrayBuffer) {
 
                     console.log("Downloaded the file: '" + url + "'!");
 
 
                 }
             };
 
             oReq.send(null);*/

            const loader = new GLTFLoader();

            loader.load("/resource/" + url, function (gltf) {

                console.log("GLTF file loaded, adding to scene..");

                gltf.scene.position.copy(new THREE.Vector3(world_ob.pos.x, world_ob.pos.y, world_ob.pos.z));
                gltf.scene.scale.copy(new THREE.Vector3(world_ob.scale.x, world_ob.scale.y, world_ob.scale.z));

                let axis = new THREE.Vector3(world_ob.axis.x, world_ob.axis.y, world_ob.axis.z);
                axis.normalize();
                let q = new THREE.Quaternion();
                q.setFromAxisAngle(axis, world_ob.angle);
                gltf.scene.setRotationFromQuaternion(q);

                //console.log("Traversing...");

                let mat_index = 0;
                gltf.scene.traverse(function (child) { // NOTE: is this traversal in the right order?
                    if (child instanceof THREE.Mesh) {

                        child.castShadow = true;
                        child.receiveShadow = true;

                        //console.log("Found child mesh");
                        let world_mat = world_ob.mats[mat_index];
                        if (world_mat) {

                            child.material.color = new THREE.Color(world_mat.colour_rgb.r, world_mat.colour_rgb.g, world_mat.colour_rgb.b);
                            child.material.metalness = world_mat.metallic_fraction.val;
                            child.material.roughness = world_mat.roughness.val;

                            if(world_mat.colour_texture_url.length > 0) {
                                // function getLODTextureURLForLevel(world_mat, base_texture_url, level, has_alpha)
                                //console.log("world_mat.flags: " + world_mat.flags);
                                //console.log("world_mat.colourTexHasAlpha: " + world_mat.colourTexHasAlpha());
                                let lod_texture_URL = getLODTextureURLForLevel(world_mat, world_mat.colour_texture_url, ob_lod_level, world_mat.colourTexHasAlpha());

                                //console.log("lod_texture_URL: " + lod_texture_URL);
                                //child.material.map = THREE.ImageUtils.loadTexture("resource/" + lod_texture_URL);

                                let texture = new THREE.TextureLoader().load("resource/" + lod_texture_URL);
                                texture.wrapS = THREE.RepeatWrapping;
                                texture.wrapT = THREE.RepeatWrapping;
                                child.material.map = texture;

                                child.material.map.matrixAutoUpdate = false;
                                //console.log("world_mat.tex_matrix: ", world_mat.tex_matrix);
                                child.material.map.matrix.set(
                                    world_mat.tex_matrix.x, world_mat.tex_matrix.y, 0,
                                    world_mat.tex_matrix.z, world_mat.tex_matrix.w, 0,
                                    0, 0, 1
                                );
                                //console.log("child.material.map.matrix: ", child.material.map.matrix);
                            }

                            child.material.needsUpdate = true;
                        }
                   

                        mat_index++;
                    }
                    //if (child instanceof THREE.Mesh) {

                    //    mat = world_ob.mats[mat_index];
                    //    // function getLODTextureURLForLevel(world_mat, base_texture_url, level, has_alpha)


                    //    child.material.map = THREE.ImageUtils.loadTexture("resource/" + world_ob.mats[0].colour_texture_url);
                    //    child.material.needsUpdate = true;

                    //    mat_index++;
                    //}
                });

               

                scene.add(gltf.scene);
                },
                undefined,
                function (error) {

                console.error(error);
            });
        }
    }
    else {

        let xspan = world_ob.aabb_ws_max.x - world_ob.aabb_ws_min.x;
        let yspan = world_ob.aabb_ws_max.y - world_ob.aabb_ws_min.y;
        let zspan = world_ob.aabb_ws_max.z - world_ob.aabb_ws_min.z;

        /*const box = BABYLON.MeshBuilder.CreateBox("box", { height: zspan, width: xspan, depth: yspan }, scene);

        box.position = new BABYLON.Vector3(world_ob.aabb_ws_min.x + xspan / 2, world_ob.aabb_ws_min.y + yspan / 2, world_ob.aabb_ws_min.z + zspan / 2);

        shadowGenerator.addShadowCaster(box);
        box.receiveShadows = true;*/

        const geometry = new THREE.BoxGeometry();
        const material = new THREE.MeshStandardMaterial({ color: 0xaaaaaa });
        const cube = new THREE.Mesh(geometry, material);
        cube.position.copy(new THREE.Vector3(world_ob.aabb_ws_min.x + xspan / 2, world_ob.aabb_ws_min.y + yspan / 2, world_ob.aabb_ws_min.z + zspan / 2));
        cube.scale.copy(new THREE.Vector3(xspan, yspan, zspan));
        //cube.updateMatrix();
        scene.add(cube);
        //cube.updateMatrix();
    }
}


// Register a render loop to repeatedly render the scene
//engine.runRenderLoop(function () {
//	scene.render();
//});

//// Watch for browser/canvas resize events
//window.addEventListener("resize", function () {
//	engine.resize();
//});


THREE.Object3D.DefaultUp.copy(new THREE.Vector3(0, 0, 1));

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

//const geometry = new THREE.BoxGeometry();
//const material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
//const cube = new THREE.Mesh(geometry, material);
//scene.add(cube);



camera.position.set(-4, 12, 3);
camera.up = new THREE.Vector3(0, 0, 1);
camera.position.set(-2, -2, 1);

//let target = ;
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
    const geometry = new THREE.PlaneGeometry(1000, 1000);
    //const material = new THREE.MeshBasicMaterial({ color: 0x999999, side: THREE.DoubleSide });
    const material = new THREE.MeshStandardMaterial();
    material.color = new THREE.Color(0.9, 0.9, 0.9);

    let texture = new THREE.TextureLoader().load("./obstacle.png");

    texture.anisotropy = renderer.getMaxAnisotropy();
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;

    texture.matrixAutoUpdate = false;
    texture.matrix.set(
        1000, 0, 0,
        0, 1000, 0,
        0, 0, 1
    );
   
    material.map = texture;

    const plane = new THREE.Mesh(geometry, material);

    plane.castShadow = true;
    plane.receiveShadow = true;
    
    scene.add(plane);
}



let is_mouse_down = false;
let heading = 0;
let pitch = Math.PI / 2;
let keys_down = new Set();

function camForwardsVec() {
    return new THREE.Vector3(Math.cos(heading) * Math.sin(pitch), Math.sin(heading) * Math.sin(pitch), Math.cos(pitch));
}

function camRightVec() {
    //return new THREE.Vector3(Math.cos(heading - Math.PI / 2), Math.sin(heading - Math.PI / 2), 0);
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



document.addEventListener('mousedown', onDocumentMouseDown, false);
document.addEventListener('mouseup', onDocumentMouseUp, false);
document.addEventListener('mousemove', onDocumentMouseMove, false);
document.addEventListener('keydown', onKeyDown, false);
document.addEventListener('keyup', onKeyUp, false);



function doCamMovement(dt){
    //console.log("doCamMovement()");

    let move_speed = 1.0;

    if(keys_down.has('ShiftLeft')){
        move_speed *= 5;
    }

    if(keys_down.has('KeyW')){
        camera.position.addScaledVector(camForwardsVec(), dt * move_speed);
    }
    if(keys_down.has('KeyS')){
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
}


let cur_time = window.performance.now();

function animate() {
    let dt = Math.min(0.03, window.performance.now() - cur_time);
    cur_time = window.performance.now();
    requestAnimationFrame(animate);

    doCamMovement(dt);

    //camera.position.x += 0.003;
    //cube.rotation.x += 0.01;
   // cube.rotation.y += 0.01;

    //camera.lookAt(0, 15, 2);

    renderer.render(scene, camera);
}

animate();


