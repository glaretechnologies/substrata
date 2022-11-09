/*
Texture loading, mipmapping, and interfaces to stb_dxt
*/

import * as THREE from '../build/three.module.js';
import { clamp } from '../maths/functions.js';

// We transmit the entire CompressionTask to the worker for processing.

enum WorkerTaskType {
	INIT,
	COMPRESS,		// Small block compress (mostly JS)
	WASM_COMPRESS, // Level compress in WASM
	MIPMAP_COMPRESS, // Mipmap generation + Level compress (Mostly WASM)
	CLEANUP
}

// An interface compatible with the Three.js CompressedTexture class
export interface CompressedImage {
	width: number
	height: number
	channels: number
	data: Uint8Array
}

export interface CompressionTask {
	url: string;
	width: number;
	height: number;
	channels: number;
	data: Uint8ClampedArray; // Input pixel data
	compressedData?: Uint8Array; // Output compressed block
	mipmaps?: CompressedImage[]

	/*
	dataBuffer: ArrayBuffer // Contains the input image (transferred in)
	compressedData?: ArrayBuffer; // Contains the output compressed mipmaps (packed as per mipmapLevels)
	mipmaps?: MipmapLevel[] // Used to reconstruct the compressed data at the caller
  */
}

export type loadCB = (tex: THREE.CompressedTexture) => void
export type failureCB = (err: Error) => void

function findMin(arr: Int32Array): number {
	let m = Number.MAX_VALUE;
	let idx = 0;
	for(let i = 0; i !== arr.length; ++i) {
		if(arr[i] < m) {
			idx = i;
			m = arr[i];
		}
	}
	return idx;
}

export default class TextureLoader {
	private readonly workers_: Worker[];
	private readonly cbList_: Record<string, 	[loadCB, failureCB | undefined][]>;

	private readonly taskCount_: Int32Array;

	private readonly canvas_: HTMLCanvasElement;
	private readonly ctx2D_: CanvasRenderingContext2D;

	public constructor (workerCount: number) {
		this.workers_ = [];
		this.cbList_ = {};
		this.taskCount_ = new Int32Array(workerCount);

		console.log('Loading', workerCount, 'workers');

		for(let i = 0; i !== workerCount; ++i) {
			this.workers_[i] = new Worker('/webclient/graphics/loaderWorker.js');
			this.workers_[i].onmessage = ev => this.processResponse(i, ev.data as CompressionTask);
			this.taskCount_[i] = 0;
		}

		this.canvas_ = document.createElement('canvas');
		this.ctx2D_ = this.canvas_.getContext('2d', { willReadFrequently: true });
	}

	public get workerCount (): number { return this.workers_.length; }
	public get availableWorker (): number { return clamp(findMin(this.taskCount_), 0, this.workerCount); }
	public load(url: string, successCB: (tex: THREE.CompressedTexture) => void, failureCB?: (err: Error) => void): void {
		this.readPixels(url).then((task: CompressionTask) => {
			//console.log('task:', task);
			if(url in this.cbList_) {
				this.cbList_[url].push([successCB, failureCB]);
			} else {
				this.cbList_[url] = [[successCB, failureCB]];
				this.compressTexture(task, this.availableWorker);
			}
		});
	}

	public processResponse (id: number, task: CompressionTask): void {
		//console.log(`Worker ${id} returned:`, task);
		this.taskCount_[id] = Math.max(0, this.taskCount_[id]-1);
		//console.log('taskCount:', this.taskCount_);

		if(task.url in this.cbList_) {
			const callbacks = this.cbList_[task.url];

			if(task.mipmaps == null || task.mipmaps.length === 0) {
				const err = new Error('Compression Task Failed');
				for(let i = 0; i !== callbacks.length; ++i) callbacks[i][1](err);
			} else {
				const format = task.channels === 3 ? THREE.RGB_S3TC_DXT1_Format : THREE.RGBA_S3TC_DXT5_Format;
				const tex = new THREE.CompressedTexture(task.mipmaps, task.width, task.height, format);
				//tex.flipY = true; // This has no effect - try to do this in the canvas when copying...
				tex.wrapS = THREE.RepeatWrapping;
				tex.wrapT = THREE.RepeatWrapping;
				tex.minFilter = THREE.LinearMipmapLinearFilter;
				tex.magFilter = THREE.LinearFilter;
				tex.needsUpdate = true;
				for(let i = 0; i !== callbacks.length; ++i) callbacks[i][0](tex);
			}
		}

		delete this.cbList_[task.url];
	}

	/*
	Ensure the image read back is padded to be multiples of 4 in each dimension
	*/
	public async readPixels (url: string): Promise<CompressionTask> {
		const blob = await fetch(url).then((resp: Response) => resp.blob());

		const img = await createImageBitmap(blob);
		const width = img.width % 4 === 0 ? img.width : (Math.floor(img.width / 4) + 1) * 4;
		const height = img.height % 4 === 0 ? img.height : (Math.floor(img.height / 4) + 1) * 4;

		this.canvas_.width = width; this.canvas_.height = height;
		this.ctx2D_.drawImage(img, 0, 0);
		// This has the unfortunate side effect of always adding a 4th channel - should we remove it?
		// TODO: Check whether or not reading from a canvas without an alpha channel results in a 3-channel texture?
		const imgData = this.ctx2D_.getImageData(0, 0, width, height);

		return {
			url: url,
			width: width,
			height: height,
			channels: imgData.data.length / (width * height),
			data: imgData.data
		};
	}

	public compressTexture (task: CompressionTask, worker: number): void {
		if(worker >= this.workers_.length) return;

		this.taskCount_[worker] += 1;
		this.workers_[worker].postMessage({
			taskType: WorkerTaskType.MIPMAP_COMPRESS,
			task
		});
	}
}

// Test Compression Data
const TEST_INPUT_BLOCK_RGBA = [8, 4, 1, 0, 8, 4, 1, 0, 9, 4, 1, 0, 9, 4, 1, 0, 8, 4, 1, 0, 8, 4, 1, 0, 9, 4, 1, 0, 9, 4,
	1, 0, 8, 4, 1, 0, 8, 4, 1, 0, 9, 4, 1, 0, 10, 5, 2, 0, 7, 3, 0, 0, 7, 3, 0, 0, 8, 3, 0, 0, 8, 3, 0, 0
];
const TEST_INPUT_BLOCK_RGB = TEST_INPUT_BLOCK_RGBA.filter((v, i) => i % 4 !== 3);

function buildChecker(dims: number, channels: number): Uint8ClampedArray {
	const total = channels * dims * dims;
	const buffer = new Uint8ClampedArray(total);

	let even = 0;
	for(let i = 0, j = 0; i !== total; i += channels, j += 1) {
		even = (Math.floor(j/dims) + (j % dims)) % 2;
		buffer[i] = even ? 255 : 0;
		buffer[i+1] = even ? 255 : 0;
		buffer[i+2] = even ? 255 : 0;
		if(channels > 3) buffer[i+3] = 255;
	}

	return buffer;
}

// A bit of a temporary hack to trim an input image to have dimensions of multiples of 4
export function trimImage (pixels: Uint8ClampedArray, width: number, height: number, channels: number): [Uint8ClampedArray, number, number] {
	const nW = Math.floor(width / 4) * 4;
	const nH = Math.floor(height / 4) * 4;
	if(nW === width && nH === height) return [pixels, width, height];

	const buffer = new Uint8ClampedArray(channels * nW * nH);
	for(let r = 0; r !== nH; ++r) {
		const to = r * nW;
		const so = r * width;
		for(let c = 0; c !== nW; ++c) {
			const o = (to + c) * channels;
			const i = (so + c) * channels;
			for(let ch = 0; ch !== channels; ++ch)
				buffer[o+ch] = pixels[i+ch];
		}
	}

	return [buffer, nW, nH];
}