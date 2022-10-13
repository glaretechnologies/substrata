/*=====================================================================
bmeshloading.ts
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

import * as fzstd from './fzstd.js';
import * as THREE from './build/three.module.js';
import * as bufferin from './bufferin.js';
import { Triangles } from './physics/bvh.js';


class Vec3f {
	x: number;
	y: number;
	z: number;
	constructor(x_, y_, z_) {
		this.x = x_;
		this.y = y_;
		this.z = z_;
	}
}

function readVec3fFromStream(buffer_in) {
	var x = buffer_in.readFloat();
	var y = buffer_in.readFloat();
	var z = buffer_in.readFloat();
	return new Vec3f(x, y, z);
}


const ComponentType_Float = 0;
const ComponentType_Half = 1;
const ComponentType_UInt8 = 2;
const ComponentType_UInt16 = 3;
const ComponentType_UInt32 = 4;
const ComponentType_PackedNormal = 5; // GL_INT_2_10_10_10_REV
const MAX_COMPONENT_TYPE_VALUE = 5;

const VertAttribute_Position = 0;
const VertAttribute_Normal = 1;
const VertAttribute_Colour = 2;
const VertAttribute_UV_0 = 3;
const VertAttribute_UV_1 = 4;
const VertAttribute_Joints = 5; // Indices of joint nodes for skinning
const VertAttribute_Weights = 6; // weights for skinning
const MAX_VERT_ATTRIBUTE_TYPE_VALUE = 6;

function componentTypeSize(t)
{
	switch (t) {
		case ComponentType_Float: return 4;
		case ComponentType_Half: return 2;
		case ComponentType_UInt8: return 1;
		case ComponentType_UInt16: return 2;
		case ComponentType_UInt32: return 4;
		case ComponentType_PackedNormal: return 4; // GL_INT_2_10_10_10_REV
	};
	return 1;
}


function vertAttributeTypeNumComponents(t)
{
	switch (t) {
		case VertAttribute_Position: return 3;
		case VertAttribute_Normal: return 3;
		case VertAttribute_Colour: return 3;
		case VertAttribute_UV_0: return 2;
		case VertAttribute_UV_1: return 2;
		case VertAttribute_Joints: return 4; // 4 joints per vert, following GLTF
		case VertAttribute_Weights: return 4; // 4 weights per vert, following GLTF
	};
	return 1;
}


function vertAttributeSize(attr)
{
	if (attr.component_type == ComponentType_PackedNormal) // Special case, has 3 components packed into 4 bytes.
		return 4;
	else
		return vertAttributeTypeNumComponents(attr.type) * componentTypeSize(attr.component_type);
}


function vertexSize(vert_attributes) // in bytes
{
	let sum = 0;
	for (let i = 0; i < vert_attributes.length; ++i)
		sum += vertAttributeSize(vert_attributes[i]);
	return sum;
}


const MAX_NUM_VERT_ATTRIBUTES = 100;
const MAX_NUM_BATCHES = 1000000;

const MAGIC_NUMBER = 12456751;
const FORMAT_VERSION = 1;

const ANIMATION_DATA_CHUNK = 10000;

const FLAG_USE_COMPRESSION = 1;


class VertAttribute {
	type : number;
	component_type : number;
	offset_B : number;
	//VertAttributeType type;
	//ComponentType component_type;
	//size_t offset_B; // Offset of attribute in vertex data, in bytes.
}


function convertToSigned(x) // x is uint32
{
	// Treat the rightmost 10 bits of x as a signed number, sign extend
	if ((x & 512) != 0) {
		// If sign bit was set:
		// want to map all 11_1111_1111 (1023) to -1.
		// Want to map 10_0000_0000 (512) to -512
		// So can do this by subtracing 1024.
		return x - 1024;
	}
	else {
		// Sign bit (left bit) was 0
		return x;
	}
}


function batchedMeshUnpackNormal(packed_normal) // packed_normal is uint32
{
	const x_bits = (packed_normal >> 0) & 1023;
	const y_bits = (packed_normal >> 10) & 1023;
	const z_bits = (packed_normal >> 20) & 1023;

	const x = convertToSigned(x_bits);
	const y = convertToSigned(y_bits);
	const z = convertToSigned(z_bits);

	return new Vec3f(x * (1. / 511.), y * (1. / 511.), z * (1. / 511.));
}


// data is an ArrayBuffer
export function loadBatchedMesh(data): [THREE.BufferGeometry, Triangles] {

	let buff = new bufferin.BufferIn(data);

	const geometry = new THREE.BufferGeometry();

	//console.log("---------------------------loadBatchedMesh()-----------------------------");

	// Read header
	let magic_number = buff.readUInt32();
	let format_version = buff.readUInt32();
	let header_size = buff.readUInt32();
	let flags = buff.readUInt32();

	let num_vert_attributes = buff.readUInt32();
	let num_batches = buff.readUInt32();
	let index_type = buff.readUInt32();
	let index_data_size_B = buff.readUInt32();
	let vertex_data_size_B = buff.readUInt32();

	let aabb_min = readVec3fFromStream(buff);
	let aabb_max = readVec3fFromStream(buff);

	if (magic_number != MAGIC_NUMBER)
		throw "Invalid magic number.";

	if (format_version < FORMAT_VERSION)
		throw "Unsupported format version " + format_version + ".";


	// Skip past rest of header
	if (header_size > 10000 || header_size > buff.length())
		throw "Header size too large.";
	buff.setReadIndex(header_size);


	// Read vert attributes
	if (num_vert_attributes == 0)
		throw "Zero vert attributes.";
	if (num_vert_attributes > MAX_NUM_VERT_ATTRIBUTES)
		throw "Too many vert attributes.";

	let cur_offset = 0;
	let vert_attributes = []
	for (let i = 0; i < num_vert_attributes; ++i) {
		let attr = new VertAttribute();

		let type = buff.readUInt32();
		if (type > MAX_VERT_ATTRIBUTE_TYPE_VALUE)
			throw "Invalid vert attribute type value.";
		attr.type = type;

		let component_type = buff.readUInt32();
		if (component_type > MAX_COMPONENT_TYPE_VALUE)
			throw "Invalid vert attribute component type value.";
		attr.component_type = component_type;

		attr.offset_B = cur_offset;
		cur_offset += vertAttributeSize(attr);

		vert_attributes.push(attr);
	}


	// Read batches
	if (num_batches > MAX_NUM_BATCHES)
		throw "Too many batches.";

	for (let i = 0; i < num_batches; ++i) {
		let indices_start = buff.readUInt32();
		let num_indices = buff.readUInt32();
		let material_index = buff.readUInt32();

		geometry.addGroup(/*start index=*/indices_start, /*count=*/num_indices, material_index);
	}

	
	// Check header index type
	if (index_type > MAX_COMPONENT_TYPE_VALUE)
		throw "Invalid index type value.";

	// Check total index data size is a multiple of each index size.
	if (index_data_size_B % componentTypeSize(index_type) != 0)
		throw "Invalid index_data_size_B.";

	// Check total vert data size is a multiple of each vertex size.  Note that vertexSize() should be > 0 since we have set mesh_out.vert_attributes and checked there is at least one attribute.
	if (vertex_data_size_B % vertexSize(vert_attributes) != 0)
		throw "Invalid vertex_data_size_B.";

	//mesh_out.vertex_data.resize(header.vertex_data_size_B); // TODO: size check? 32-bit limit of vertex_data_size_B may be enough.
	let vertex_data = new Uint8Array(vertex_data_size_B);

	let vert_size = vertexSize(vert_attributes); // in bytes
	let num_verts = vertex_data_size_B / vert_size;

	let index_data = null;

	let compression = (flags & FLAG_USE_COMPRESSION) != 0;
	if (compression) {
		{
			let index_data_compressed_size = Number(buff.readUInt64());

			if ((index_data_compressed_size >= buff.length()) || (buff.getReadIndex() + index_data_compressed_size > buff.length())) // Check index_data_compressed_size is valid, while taking care with wraparound
				throw "index_data_compressed_size was invalid.";

			// Decompress index data into plaintext buffer.
			let compressed_text = buff.readData(index_data_compressed_size);
			let plaintext = fzstd.decompress(new Uint8Array(compressed_text));

			// Unfilter indices, place in mesh_out.index_data.
			let num_indices = index_data_size_B / componentTypeSize(index_type);
			if (index_type == ComponentType_UInt8) {
				let last_index = 0;
				let filtered_index_data_int8 = new Int8Array(plaintext.buffer, plaintext.byteOffset, plaintext.length);
				index_data = new Uint8Array(index_data_size_B);
				for (let i = 0; i < num_indices; ++i) {
					let index = last_index + filtered_index_data_int8[i];
					index_data[i] = index;
					last_index = index;
				}
			}
			else if (index_type == ComponentType_UInt16) {
				let last_index = 0;
				let filtered_index_data_int16 = new Int16Array(plaintext.buffer, plaintext.byteOffset, plaintext.length / 2);
				index_data = new Uint16Array(index_data_size_B / 2);
				for (let i = 0; i < num_indices; ++i) {
					let index = last_index + filtered_index_data_int16[i];
					index_data[i] = index;
					last_index = index;
				}
			}
			else if (index_type == ComponentType_UInt32) {
				let last_index = 0;
				let filtered_index_data_int32 = new Int32Array(plaintext.buffer, plaintext.byteOffset, plaintext.length / 4);
				index_data = new Uint32Array(index_data_size_B / 4);
				for (let i = 0; i < num_indices; ++i) {
					let index = last_index + filtered_index_data_int32[i];
					index_data[i] = index;
					last_index = index;
				}
			}
			else
				throw "Invalid index component type " + index_type;
		}

		// Decompress and de-filter vertex data.
		{
			let vertex_data_compressed_size = Number(buff.readUInt64());
			if (vertex_data_compressed_size >= buff.length() || (buff.getReadIndex() + vertex_data_compressed_size > buff.length())) // Check vertex_data_compressed_size is valid, while taking care with wraparound
				throw "vertex_data_compressed_size was invalid.";

			// Decompress data into plaintext buffer.
			let compressed_text = buff.readData(vertex_data_compressed_size);
			let plaintext = fzstd.decompress(new Uint8Array(compressed_text));
			
			/*
			Read de-interleaved vertex data, and interleave it.
		
			p0 p1 p2 p3 ... pN n0 n1 n2 n3 ... nN c0 c1 c2 c3 ... cN
			=>
			p0 n0 c0 p1 n1 c1 p2 n2 c2 p3 n3 c3 ... pN nN cN
			*/
			let src = new Uint32Array(plaintext.buffer, plaintext.byteOffset, plaintext.length / 4);
			let dst = new Uint32Array(vertex_data.buffer, vertex_data.byteOffset, vertex_data.length / 4);

			let src_i = 0;
			
			let attr_offset_B = 0;
			for (let b = 0; b < vert_attributes.length; ++b)
			{
				let attr_size_B = vertAttributeSize(vert_attributes[b]);
				let attr_size_uint32s = attr_size_B / 4;
				let dst_i = attr_offset_B / 4;

				let vert_size_uint32s = vert_size / 4;

				for (let i = 0; i < num_verts; ++i) // For each vertex
				{
					// Copy data for this attribute, for this vertex, to filtered_data
					for (let z = 0; z < attr_size_uint32s; ++z)
						dst[dst_i + z] = src[src_i + z];

					src_i += attr_size_uint32s;
					dst_i += vert_size_uint32s;
				}

				attr_offset_B += attr_size_B;
			}
		}
	}
	else // else if !compression:
	{
		throw "bmeshes without compression not currently supported.";
	}

	geometry.setIndex(new THREE.BufferAttribute(index_data, 1));

	// Convert and expand vertex data to something 3.js can handle

	// Do a pass over the attributes to get the expanded attribute size and total expanded vertex size.
	let expanded_vert_size_B = 0;
	let expanded_attr_sizes = []
	for (let i = 0; i < vert_attributes.length; ++i) {

		let attr = vert_attributes[i];

		let num_components = vertAttributeTypeNumComponents(attr.type);

		// Compute size, in bytes, of the attribute component type expanded to something 3.js can handle.
		let expanded_comp_size = null;
		if (attr.component_type == ComponentType_Float)
			expanded_comp_size = 4;
		else if (attr.component_type == ComponentType_Half)
			expanded_comp_size = 4;
		else if (attr.component_type == ComponentType_UInt8)
			expanded_comp_size = 1;
		else if (attr.component_type == ComponentType_UInt16)
			expanded_comp_size = 2;
		else if (attr.component_type == ComponentType_UInt32)
			expanded_comp_size = 4;
		else if (attr.component_type == ComponentType_PackedNormal)
			expanded_comp_size = 4;

		let attr_size = num_components * expanded_comp_size;
		expanded_attr_sizes.push(attr_size);
		expanded_vert_size_B += attr_size;
	}

	let expanded = new Float32Array(num_verts * expanded_vert_size_B / 4);

	// Views on vertex_data
	let deinterleaved_uint32 = new Uint32Array(vertex_data.buffer, vertex_data.byteOffset, vertex_data.length / 4);
	let deinterleaved_float = new Float32Array(vertex_data.buffer, vertex_data.byteOffset, vertex_data.length / 4);

	// Do a pass over the attributes and vertex data to compute the expanded vertex data
	let expanded_attr_offset_B = 0;
	for (let i = 0; i < vert_attributes.length; ++i) {

		let attr = vert_attributes[i];

		let attr_size_uint32s = expanded_attr_sizes[i] / 4;

		let vert_size_uint32s = vert_size / 4;
		let expanded_vert_size_uint32s = expanded_vert_size_B / 4;

		let expanded_attr_offset_uint32s = expanded_attr_offset_B / 4;
		let src_attr_offset_uint32s = attr.offset_B / 4;

		if (attr.component_type == ComponentType_Float || attr.component_type == ComponentType_UInt16) {
			for (let v = 0; v < num_verts; ++v) {
				for (let q = 0; q < attr_size_uint32s; ++q) {
					expanded[v * expanded_vert_size_uint32s + expanded_attr_offset_uint32s + q] = deinterleaved_float[v * vert_size_uint32s + src_attr_offset_uint32s + q];
				}
			}
		}
		else if (attr.component_type == ComponentType_PackedNormal) {
			for (let v = 0; v < num_verts; ++v) {
				let packed_normal = deinterleaved_uint32[v * vert_size_uint32s + src_attr_offset_uint32s]; // Read as a uint32
				let unpacked_normal = batchedMeshUnpackNormal(packed_normal); // Unpack to a Vec3f
				expanded[v * expanded_vert_size_uint32s + expanded_attr_offset_uint32s + 0] = unpacked_normal.x; // Write expanded components
				expanded[v * expanded_vert_size_uint32s + expanded_attr_offset_uint32s + 1] = unpacked_normal.y;
				expanded[v * expanded_vert_size_uint32s + expanded_attr_offset_uint32s + 2] = unpacked_normal.z;
			}
		}
		else
			throw "Unhandled attribute component_type while expanding: " + attr.component_type + ", attr.type: " + attr.type;

		expanded_attr_offset_B += expanded_attr_sizes[i];
	}

	let interleaved_buffer = new THREE.InterleavedBuffer(expanded, /*vert stride in elems=*/expanded_vert_size_B / 4);

	// Set the 3.js geometry vertex attributes
	expanded_attr_offset_B = 0;
	let added_normals = false;
	for (let i = 0; i < vert_attributes.length; ++i) {

		let attr = vert_attributes[i];
		let expanded_attr_offset_uint32s = expanded_attr_offset_B / 4;
		
		let name = null;

		if (attr.type == VertAttribute_Position) {
			name = 'position';
		}

		else if (attr.type == VertAttribute_Normal) {
			name = 'normal';
			added_normals = true;
		}
		else if (attr.type == VertAttribute_Colour)
			name = 'color';
		else if (attr.type == VertAttribute_UV_0)
			name = 'uv';
		else if (attr.type == VertAttribute_UV_1)
			name = 'uv2';
		else {
			// console.log("Note: ignoring attribute type " + attr.type);
		}

		if (name !== null) {
			let num_components = vertAttributeTypeNumComponents(attr.type);

			// console.log("Adding attribute " + name + "...");

			geometry.setAttribute(name, new THREE.InterleavedBufferAttribute(interleaved_buffer, /*itemSize=*/num_components, /*offset=*/expanded_attr_offset_uint32s));
		}
		expanded_attr_offset_B += expanded_attr_sizes[i];
	}

	let triangles = new Triangles(expanded, index_data, 0, expanded_vert_size_B / 4);

	if (!added_normals)
		geometry.computeVertexNormals();

	return [geometry, triangles];
}
