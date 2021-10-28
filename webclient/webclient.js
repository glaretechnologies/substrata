var ws = new WebSocket("ws://localhost", "echo-protocol");
ws.binaryType = "arraybuffer"; // Change binary type from "blob" to "arraybuffer"


const STATE_INITIAL = 0;
const STATE_READ_HELLO_RESPONSE = 1;
const STATE_READ_PROTOCOL_RESPONSE = 2;
const STATE_READ_CLIENT_AVATAR_UID = 3;

var protocol_state = 0;

const CyberspaceHello = 1357924680;
const ClientProtocolOK = 10000;
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
    utf8_array = toUTF8Array(str)

    ws.send(new Uint32Array([utf8_array.length]));
    ws.send(new Uint8Array(utf8_array));
}



// Then you can send a message
ws.onopen = function () {
	console.log("onopen()");

	ws.send(new Uint32Array([CyberspaceHello]));

	var CyberspaceProtocolVersion = 30;
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

class WorldMaterial {

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
        // Read compressed voxel data
        let voxel_data_size = readUInt32(buffer_in);
        if (voxel_data_size > 1000000)
            throw "Invalid voxel_data_size (too large): " + toString(voxel_data_size);

        // Read voxel data
        ob.compressed_voxels = buffer_in.readData(voxel_data_size);
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
        let r = 4;
        buffer_out.writeUInt32(2 * (2 * r + 1) * (2 * r + 1)); // Num cells to query

        for (let x = -r; x <= r; ++x)
            for (let y = -r; y <= r; ++y) {
                buffer_out.writeInt32(x); buffer_out.writeInt32(y); buffer_out.writeInt32(0);
                buffer_out.writeInt32(x); buffer_out.writeInt32(y); buffer_out.writeInt32(-1);
            }

        buffer_out.writeToWebSocket(ws);
    }
}


var parcels = {};
var world_objects = {};

//Log the messages that are returned from the server
ws.onmessage = function (event) {
    //console.log("onmessage()");
    //console.log("From Server:" + event.data + ", event.data.byteLength: " + event.data.byteLength);

    let z = 0;
    buffer = new BufferIn(event.data);
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
            if (protocol_response != ClientProtocolOK)
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
            //console.log("Read msg_type: " + msg_type);

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

                addWorldObjectGraphics(world_ob);

                world_objects[object_uid] = world_ob;
                //console.log("Read ObjectInitialSend msg, object_uid: " + object_uid);
            }  
            else
                throw "Unhandled message type " + msg_type;
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


var shadowGenerator;

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


function toYUp(v) {
    return new THREE.Vector3(v.x, v.z, -v.y);
   // return v;
}


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

        let v0 = toYUp(new BABYLON.Vector3(v.x, v.y, parcel.zbounds.x));
        let v1 = toYUp(new BABYLON.Vector3(next_v.x, next_v.y, parcel.zbounds.x));
        let v2 = toYUp(new BABYLON.Vector3(next_v.x, next_v.y, parcel.zbounds.y));
        let v3 = toYUp(new BABYLON.Vector3(v.x, v.y, parcel.zbounds.y));

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
        let v0 = toYUp(new BABYLON.Vector3(parcel.verts[0].x, parcel.verts[0].y, parcel.zbounds.x));
        let v1 = toYUp(new BABYLON.Vector3(parcel.verts[3].x, parcel.verts[3].y, parcel.zbounds.x));
        let v2 = toYUp(new BABYLON.Vector3(parcel.verts[2].x, parcel.verts[2].y, parcel.zbounds.x));
        let v3 = toYUp(new BABYLON.Vector3(parcel.verts[1].x, parcel.verts[1].y, parcel.zbounds.x));

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
        let v0 = toYUp(new BABYLON.Vector3(parcel.verts[0].x, parcel.verts[0].y, parcel.zbounds.y));
        let v1 = toYUp(new BABYLON.Vector3(parcel.verts[1].x, parcel.verts[1].y, parcel.zbounds.y));
        let v2 = toYUp(new BABYLON.Vector3(parcel.verts[2].x, parcel.verts[2].y, parcel.zbounds.y));
        let v3 = toYUp(new BABYLON.Vector3(parcel.verts[3].x, parcel.verts[3].y, parcel.zbounds.y));

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


function addWorldObjectGraphics(world_ob) {

    let xspan = world_ob.aabb_ws_max.x - world_ob.aabb_ws_min.x;
    let yspan = world_ob.aabb_ws_max.y - world_ob.aabb_ws_min.y;
    let zspan = world_ob.aabb_ws_max.z - world_ob.aabb_ws_min.z;

    /*const box = BABYLON.MeshBuilder.CreateBox("box", { height: zspan, width: xspan, depth: yspan }, scene);

    box.position = toYUp(new BABYLON.Vector3(world_ob.aabb_ws_min.x + xspan / 2, world_ob.aabb_ws_min.y + yspan / 2, world_ob.aabb_ws_min.z + zspan / 2));

    shadowGenerator.addShadowCaster(box);
    box.receiveShadows = true;*/

    const geometry = new THREE.BoxGeometry();
    const material = new THREE.MeshStandardMaterial({ color: 0xaaaaaa });
    const cube = new THREE.Mesh(geometry, material);
    cube.position.copy(toYUp(new THREE.Vector3(world_ob.aabb_ws_min.x + xspan / 2, world_ob.aabb_ws_min.y + yspan / 2, world_ob.aabb_ws_min.z + zspan / 2)));
    cube.scale.copy(toYUp(new THREE.Vector3(xspan, yspan, zspan)));
    //cube.updateMatrix();
    scene.add(cube);
    //cube.updateMatrix();
}



// Register a render loop to repeatedly render the scene
//engine.runRenderLoop(function () {
//	scene.render();
//});

//// Watch for browser/canvas resize events
//window.addEventListener("resize", function () {
//	engine.resize();
//});


const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

//const geometry = new THREE.BoxGeometry();
//const material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
//const cube = new THREE.Mesh(geometry, material);
//scene.add(cube);

camera.position.set(100, 100, 200);
camera.lookAt(0, 0, 0);


const hemiLight = new THREE.HemisphereLight(0xaaffff, 0x444444);
hemiLight.position.set(0, 20, 0);
scene.add(hemiLight);

const dirLight = new THREE.DirectionalLight(0xffaaff, 0.5);
dirLight.position.set(3, 10, 10);
//dirLight.castShadow = true;
//dirLight.shadow.camera.top = 2;
//dirLight.shadow.camera.bottom = - 2;
//dirLight.shadow.camera.left = - 2;
//dirLight.shadow.camera.right = 2;
//dirLight.shadow.camera.near = 0.1;
//dirLight.shadow.camera.far = 40;
scene.add(dirLight);

//const controls = new OrbitControls(camera, renderer.domElement);
//controls.enablePan = false;
//controls.enableZoom = false;
//controls.target.set(0, 1, 0);
//controls.update();

function animate() {
    requestAnimationFrame(animate);

    //cube.rotation.x += 0.01;
   // cube.rotation.y += 0.01;

    renderer.render(scene, camera);
}
animate();
