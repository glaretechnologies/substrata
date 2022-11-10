enum WorkerTaskType {
	INIT,
	COMPRESS,
	WASM_COMPRESS,			// Compresses a whole mipmap level in WASM
	MIPMAP_COMPRESS,		// Performs all mipmap level calculation and compression in WASM
	CLEANUP
}

// An interface compatible with the Three.js CompressedTexture class
interface CompressedImage {
	width: number
	height: number
	channels: number
	data: Uint8Array
}

interface MipmapLevel {
	width: number
	height: number
	channels: number
	levelSize: number // Total size of mipmap layer (uncompressed)
	compressedDataSize: number // Total size of layer (rounded up to nearest power-of-two)
	dataOffset: number // Offset in compressedData buffer
}

interface CompressionTask {
	url: string;
	width: number;
	height: number;
	channels: number;
	levels?: MipmapLevel[] // Used to reconstruct the compressed data at the caller

	// Used for passing data internally
	data?: Uint8ClampedArray; // Input pixel data - transfer in
	compressedData?: Uint8Array; // Output buffer - transfer out (compressed)
	mipmaps?: CompressedImage[]; // Return the interface expected by THREE.CompressedTexture

	// Transfer Buffers (from/to worker)
	dataBuffer?: ArrayBuffer // Contains the input image (transferred in)
	compressedDataBuffer?: ArrayBuffer; // Contains the output compressed mipmaps (packed as per mipmapLevels)
}

const STB_DXT_HIGHQUAL = 2;

const inputBlock = new Uint8ClampedArray(64);
const outputBlock8 = new Uint8Array(8); // For RGB
const outputBlock16 = new Uint8Array(16); // For RGBA

let context: WASMContext;

function isPowerOfTwo(value: number): boolean {
	value = Math.floor(value);
	return (value > 0) && ((value & (value - 1)) === 0);
}

function roundUpToMultipleOfPowerOf2(x: number, n: number): number {
	x = Math.floor(x); n = Math.floor(n);
	return (x + n - 1) & ~(n - 1);
}

function computeMipmapLevels (width: number, height: number, channels: number): [MipmapLevel[], number, number] {
	const mipmaps: MipmapLevel[] = [];

	let total = 0;

	mipmaps[0] = {
		width,
		height,
		channels,
		levelSize: width * height * channels,
		compressedDataSize: computeCompressedBufferSize(width, height, channels),
		dataOffset: 0
	};

	total += mipmaps[0].levelSize;

	let k = 1;
	let currW = width;
	let currH = height;
	let currOffset = roundUpToMultipleOfPowerOf2(mipmaps[0].compressedDataSize, 8);
	while (currW !== 1 || currH !== 1) {
		currW = Math.max(1, Math.floor(width / (1 << k)));
		currH = Math.max(1, Math.floor(height / (1 << k)));

		const levelSize = channels * currW * currH;
		const compressedSize = computeCompressedBufferSize(currW, currH, channels);
		const offset = roundUpToMultipleOfPowerOf2(currOffset + compressedSize, 8);

		mipmaps[k] = {
			width: currW,
			height: currH,
			channels,
			levelSize,
			compressedDataSize: compressedSize,
			dataOffset: currOffset
		};

		total += levelSize;
		currOffset = offset;
		k += 1;
	}

	return [mipmaps, total, currOffset]; // Level mipmaps, mipmap buffer size, compressed buffer size]
}

function computeCompressedBufferSize(width: number, height: number, channels: number) {
	const numBlocksX = Math.ceil(width / 4);
	const numBlocksY = Math.ceil(height / 4);
	const totalBlocks = numBlocksX * numBlocksY;
	return totalBlocks * (channels === 3 ? 8 : 16);
}


/*
// See glare-core::TextureLoading computeAlphaCoverage
function computeAlphaCoverage (buf: Uint8ClampedArray, width: number, height: number): number {
	const N = 4;
	let numOpaquePx = 0;
	for(let y = 0; y < height; ++y) {
		for(let x = 0; x < width; ++x) {
			const alpha = buf[(width * y + x) * N + 3];
			if(alpha > 186) numOpaquePx++;
		}
	}
	return numOpaquePx / (width * height);
}

function downsampleToNextMipmapLevel(
	prevW: number,
	prevH: number,
	channels: number,
	prevBuffer: Uint8ClampedArray,
	alphaScale: number,
	currW: number,
	currH: number,
	currBuffer: Uint8ClampedArray): number { // Return alphaCoverageOut
	const srcData = prevBuffer;
	const srcW = prevW; const srcH = prevH;
	let alphaCoverageOut = 0;

	if(channels !== 3 && channels !== 4) console.error('downsample inputs invalid');
	if(!(srcW == 1 || ((currW - 1) * 2 + 1 < srcW))) console.error('downsample inputs invalid');
	if(!(srcH == 1 || ((currH - 1) * 2 + 1 < srcH))) console.error('downsample inputs invalid');

	const val = new Uint32Array(4);

	if(channels === 3) {
		if(srcW === 1) {
			for(let y = 0; y < currH; ++y) {
				val.fill(0);
				let sy = y * 2;
				{
					const idx = (srcW * sy) * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
				}
				sy = y * 2 + 1;
				{
					const idx = (srcW * sy) * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
				}
				const dest = (currW * y) * channels;
				currBuffer[dest] = val[0] / 2;
				currBuffer[dest+1] = val[1] / 2;
				currBuffer[dest+2] = val[2] / 2;
			}
		} else if(srcH === 1) {
			for(let x = 0; x < currW; ++x) {
				val.fill(0);
				let sx = x * 2;
				{
					const idx = sx * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
				}
				sx = x * 2 + 1;
				{
					const idx = sx * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
				}
				const dst = x * channels;
				currBuffer[dst] = val[0] / 2;
				currBuffer[dst+1] = val[1] / 2;
				currBuffer[dst+2] = val[2] / 2;
			}
		} else { // srcW > 1 && srcH > 1
			for(let y = 0; y < currH; ++y) {
				for(let x = 0; x < currW; ++x) {
					val.fill(0);
					let sx = x * 2; let sy = y * 2;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
					}
					sx = x * 2 + 1;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
					}
					sx = x * 2;
					sy = y * 2 + 1;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
					}
					sx = x * 2 + 1;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
					}

					const dst = (x + currW * y) * channels;
					currBuffer[dst] = val[0] / 4;
					currBuffer[dst+1] = val[1] / 4;
					currBuffer[dst+2] = val[2] / 4;
				}
			}
		}
	} else { // if(channels === 4)
		let numOpaquePx = 0;

		if(srcW === 1) {
			for(let y = 0; y < currH; ++y) {
				val.fill(0);
				let sy = y * 2;
				{
					const idx = (srcW * sy) * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
					val[3] += srcData[idx+3];
				}
				sy = y * 2 + 1;
				{
					const idx = (srcW * sy) * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
					val[3] += srcData[idx+3];
				}
				const dest = (currW * y) * channels;
				currBuffer[dest] = val[0] / 2;
				currBuffer[dest+1] = val[1] / 2;
				currBuffer[dest+2] = val[2] / 2;
				currBuffer[dest+3] = Math.min(255, alphaScale * (val[3] / 2));
				if(currBuffer[dest+3] >= 186) numOpaquePx++;
			}
		} else if(srcH === 1) {
			for(let x = 0; x < currW; ++x) {
				val.fill(0);
				let sx = x * 2;
				{
					const idx = sx * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
					val[3] += srcData[idx+3];
				}
				sx = x * 2 + 1;
				{
					const idx = sx * channels;
					val[0] += srcData[idx];
					val[1] += srcData[idx+1];
					val[2] += srcData[idx+2];
					val[3] += srcData[idx+3];
				}
				const dst = x * channels;
				currBuffer[dst] = val[0] / 2;
				currBuffer[dst+1] = val[1] / 2;
				currBuffer[dst+2] = val[2] / 2;
				currBuffer[dst+3] = Math.min(255, alphaScale * (val[3] / 2));

				if(currBuffer[dst+3] >= 186) numOpaquePx++;
			}
		} else { // srcW > 1 && srcH > 1
			for(let y = 0; y < currH; ++y) {
				for(let x = 0; x < currW; ++x) {
					val.fill(0);
					let sx = x * 2; let sy = y * 2;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
						val[3] += srcData[idx+3];
					}
					sx = x * 2 + 1;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
						val[3] += srcData[idx+3];
					}
					sx = x * 2;
					sy = y * 2 + 1;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
						val[3] += srcData[idx+3];
					}
					sx = x * 2 + 1;
					{
						const idx = (sx + srcW * sy) * channels;
						val[0] += srcData[idx];
						val[1] += srcData[idx+1];
						val[2] += srcData[idx+2];
						val[3] += srcData[idx+3];
					}

					const dst = (x + currW * y) * channels;
					currBuffer[dst] = val[0] / 4;
					currBuffer[dst+1] = val[1] / 4;
					currBuffer[dst+2] = val[2] / 4;
					currBuffer[dst+3] = Math.min(255, alphaScale * (val[3] / 4));
					if(currBuffer[dst+3] >= 186) numOpaquePx++;
				}
			}
			alphaCoverageOut = numOpaquePx / (currW * currH);
		}
	}

	return alphaCoverageOut;
}

function buildMipmaps (task: CompressionTask): CompressionTask {
	const { width, height, channels, data } = task;

	const mipmaps: CompressedImage[] = [];
	let level0AlphaCoverage = 0;
	let k = 0; let levelW = width; let levelH = height;
	let currBuffer: Uint8ClampedArray | undefined;
	let prevBuffer: Uint8ClampedArray | undefined;

	const bufferA = new Uint8ClampedArray(Math.floor(data.length / 2)); // Test 4
	const bufferB = new Uint8ClampedArray(Math.floor(bufferA.length / 2));

	while(levelW !== 1 || levelH !== 1) {
		levelW = Math.max(1, Math.floor(width / (1 << k)));
		levelH = Math.max(1, Math.floor(height / (1 << k)));

		if(k === 0) {
			currBuffer = task.data;
			if(channels === 4) level0AlphaCoverage = computeAlphaCoverage(currBuffer, levelW, levelH);
		} else {
			const prevLevelW = Math.max(1, Math.floor(width / (1 << (k - 1))));
			const prevLevelH = Math.max(1, Math.floor(height / (1 << (k - 1))));

			prevBuffer = currBuffer;
			currBuffer = k % 2 === 0 ? bufferA : bufferB;

			if(currBuffer.length < levelW * levelH * channels) {
				console.error('Insufficient buffer space');
			}

			let alphaScale = 1.;
			if(channels === 4) {
				for(let i = 0; i !== 8; ++i) {
					const coverage = downsampleToNextMipmapLevel(prevLevelW, prevLevelH, channels, prevBuffer, alphaScale,
						levelW, levelH, currBuffer);
					if(coverage >= .9 * level0AlphaCoverage) break;
					alphaScale *= 1.1;
				}
			} else {
				downsampleToNextMipmapLevel(prevLevelW, prevLevelH, channels, prevBuffer, alphaScale, levelW,
					levelH, currBuffer);
			}
		}

		let subTask: CompressionTask;
		if(channels === 3) {
			subTask = compressRGB({
				url: '',
				width: levelW,
				height: levelH,
				channels,
				data: currBuffer,
			});
		} else {
			// Test difference in speed wasm vs js + wasm

			const start = performance.now();
			
			subTask = compressRGBA({
				url: '',
				width: levelW,
				height: levelH,
				channels,
				data: currBuffer,
			});

			const end = performance.now();
			if(k === 0) {
				console.log('TOTAL TIME:', (end - start)*.001);
			}
		}

		mipmaps[k] = {
			width: levelW,
			height: levelH,
			channels,
			data: subTask.compressedData
		};

		k += 1;
	}

	return {
		...task,
		mipmaps
	};
}

function compressRGB(task: CompressionTask): CompressionTask {
	const W = task.width, Wm1 = W - 1;
	const H = task.height, Hm1 = H - 1;
	const numBlocksX = Math.ceil(W / 4);
	const numBlocksY = Math.ceil(H / 4);
	const totalBlocks = numBlocksX * numBlocksY;

	const pixelData = task.data;

	const outputBufferSize = totalBlocks * 8; // DXT1
	const compressedOutput = new Uint8Array(outputBufferSize);

	let write_i = 0;

	for (let by = 0; by < H; by += 4) {
		for (let bx = 0; bx < W; bx += 4) {
			let z = 0;
			for (let y = by; y < by + 4; ++y) {
				const useY = Math.min(y, Hm1);
				for (let x = bx; x < bx + 4; ++x) {
					const useX = Math.min(x, Wm1);
					let offset = (useY * W + useX) * 3;
					inputBlock[z++] = pixelData[offset++];
					inputBlock[z++] = pixelData[offset++];
					inputBlock[z++] = pixelData[offset];
					inputBlock[z++] = 0;
				}
			}

			compressDXTBlock(inputBlock, outputBlock8, 0, STB_DXT_HIGHQUAL);
			compressedOutput.set(outputBlock8, write_i);
			write_i += 8;
		}
	}

	task.compressedData = compressedOutput;
	return task;
}

function compressRGBA(task: CompressionTask): CompressionTask {
	const W = task.width, Wm1 = W - 1;
	const H = task.height, Hm1 = H - 1;
	const numBlocksX = Math.ceil(W / 4);
	const numBlocksY = Math.ceil(H / 4);
	const totalBlocks = numBlocksX * numBlocksY;

	const pixelData = task.data;

	const outputBufferSize = totalBlocks * 16; // DXT5
	const compressedOutput = new Uint8Array(outputBufferSize);

	let write_i = 0;

	for (let by = 0; by < H; by += 4) {
		for (let bx = 0; bx < W; bx += 4) {
			let z = 0;
			for (let y = by; y < by + 4; ++y) {
				const useY = Math.min(y, Hm1);
				for (let x = bx; x < bx + 4; ++x) {
					const useX = Math.min(x, Wm1);
					let offset = (useY * W + useX) * 4;
					inputBlock[z++] = pixelData[offset++];
					inputBlock[z++] = pixelData[offset++];
					inputBlock[z++] = pixelData[offset++];
					inputBlock[z++] = pixelData[offset];
				}
			}

			compressDXTBlock(inputBlock, outputBlock16, 1, STB_DXT_HIGHQUAL);
			compressedOutput.set(outputBlock16, write_i);
			write_i += 16;
		}
	}

	task.compressedData = compressedOutput;
	return task;
}

function wasmCompress(task: CompressionTask): CompressionTask {
	const start = performance.now();

	const inBuf = context.compressDXTinBuffer;
	const outBuf = context.compressDXToutBuffer;
	const compressedSize = computeCompressedBufferSize(task.width, task.height, task.channels);

	if(inBuf.length < task.data.length || outBuf.length < compressedSize) {
		console.error('Input too large!');
		return task;
	}

	inBuf.set(task.data);
	if(task.channels === 4) {
		context.compress_dxt5(outBuf.byteOffset, inBuf.byteOffset, task.width, task.height, task.channels);
	} else {
		context.compress_dxt1(outBuf.byteOffset, inBuf.byteOffset, task.width, task.height, task.channels);
	}

	const compressedData = outBuf.slice(0, compressedSize);

	//console.log('compressedData:', compressedData);

	const mipmaps = [
		{
			width: task.width,
			height: task.height,
			channels: task.channels,
			data: compressedData
		}
	];

	task.mipmaps = mipmaps;

	const end = performance.now();
	console.log('TIME WASM:', (end - start) * .001);

	return task;
}

// Build all the mipmaps and compress
function wasmMipmaps(task: CompressionTask): CompressionTask {
	return task;
}
*/

interface WASMContext {
	module: any
	compressDXTBuffer: Uint8Array	// A small buffer used to call stb_compress_dxt_block
	compressDXTinBuffer: Uint8Array // A large input buffer used to call compress_dxt1 and compress_dxt5
	compressDXToutBuffer: Uint8Array // A large output buffer used to call compress_dxt1 and compress_dxt5
	levelsBuffer: Int32Array // Input to build_mipmaps containing mipmap levels
	compressDXTBufferPtr: number
	compressDXTinBufferPtr: number
	compressDXToutBufferPtr: number
	levelsBufferPtr: number
	stb_compress_dxt_block: (outBuf: number, inBuf: number, alpha: number, mode: number) => void
	compress_dxt1: (outBuf: number, inBuf: number, width: number, height: number, channels: number) => void
	compress_dxt5: (outBuf: number, inBuf: number, width: number, height: number, channels: number) => void
	build_mipmaps: (outBuf: number, inBuf: number, levelsCount: number, levels: number) => void
}

function compressDXTBlock (inBuf: Uint8ClampedArray, outBuf: Uint8Array, alpha: number, mode: number=STB_DXT_HIGHQUAL) {
	const buf = context.compressDXTBuffer;
	buf.set(inBuf);
	context.stb_compress_dxt_block(buf.byteOffset + 64, buf.byteOffset, alpha, STB_DXT_HIGHQUAL);
	outBuf.set(buf.slice(64, 64+outBuf.length));
}

function initLevelBuffer(levelBuffer: Int32Array, levels: MipmapLevel[]) {
	const buf = levelBuffer;
	for(let i = 0; i !== levels.length; ++i) {
		let offset = 6 * i;
		buf[offset++] = levels[i].width;
		buf[offset++] = levels[i].height;
		buf[offset++] = levels[i].channels;
		buf[offset++] = levels[i].levelSize;
		buf[offset++] = levels[i].compressedDataSize;
		buf[offset] = levels[i].dataOffset;
	}
}

// This function allocates a context object containing all buffers for communication between JS and WASM
function initialiseWASM (module: any) {
	// console.log('Module.HEAPU8 before:', module.HEAPU8.length);
	// stb_compress block
	const bufferByteCount = 64 + 16; // 64 bytes input, max 16 bytes output - we do not want to reallocate this buffer
	const compressDXTBufferPtr = module._malloc(bufferByteCount);

	// Input Block - Max Level 0 at 4096 x 4096 x (4 + 2) (each level half of the previous)
	// For inblock we need level0 and level1 of the mipmaps (4096 x 4096 x (4 + 2)) = approx 96 Mb per worker
	const inblockByteCount = 4096 * 4096 * (4 + 2);
	const compressDXTinBufferPtr = module._malloc(inblockByteCount);

	// Output Block (4 : 1) - approx 24 Mb per worker
	const outblockByteCount = Math.ceil(inblockByteCount / 4);
	const compressDXToutBufferPtr = module._malloc(outblockByteCount);

	const levelsCount = 6 * 14; // Max 14 levels of 6 32-bit integers
	const levelsBufferPtr = module._malloc(levelsCount * 4); // Allocate in bytes

	const compressDXTBuffer = new Uint8Array(module.HEAPU8.buffer, compressDXTBufferPtr, bufferByteCount);
	const compressDXTinBuffer = new Uint8Array(module.HEAPU8.buffer, compressDXTinBufferPtr, inblockByteCount);
	const compressDXToutBuffer = new Uint8Array(module.HEAPU8.buffer, compressDXToutBufferPtr, outblockByteCount);
	const levelsBuffer = new Int32Array(module.HEAPU8.buffer, levelsBufferPtr, levelsCount); // count is in elements, not bytes

	//console.log('Module.HEAPU8 after:', module.HEAPU8.length);

	const stb_compress_dxt_block = module.cwrap(
		'stb_compress_dxt_block', null, ['number', 'number', 'number', 'number']
	);

	const compress_dxt1 = module.cwrap(
		'compress_dxt1', null, ['number', 'number', 'number', 'number', 'number']
	);

	const compress_dxt5 = module.cwrap(
		'compress_dxt5', null, ['number', 'number', 'number', 'number', 'number']
	);

	const build_mipmaps = module.cwrap(
		'build_mipmaps', null, ['number', 'number', 'number', 'number']
	);

	return {
		module,
		compressDXTBuffer,
		compressDXTinBuffer,
		compressDXToutBuffer,
		levelsBuffer,
		compressDXTBufferPtr,
		compressDXTinBufferPtr,
		compressDXToutBufferPtr,
		levelsBufferPtr,
		stb_compress_dxt_block,
		compress_dxt1,
		compress_dxt5,
		build_mipmaps
	};
}

function shutdownWASM (context: WASMContext) {
	// Free all malloc'ed memory, etc.
	// Essentially, we don't ever shut this down so we always need the buffer.
}

// @ts-expect-error - no importScripts
if(typeof importScripts === 'function') {
	// @ts-expect-error - import WASM
	importScripts('/webclient/graphics/wasm/stb_dxt.js');
	// @ts-expect-error - import WASM
	const Module = self.Module;

	Module.onRuntimeInitialized = () => {
		context = initialiseWASM(Module);

		self.onmessage = ev => {
			switch(ev.data.taskType) {
			/*
			case WorkerTaskType.COMPRESS: {
				let task = ev.data.task;
				const start = performance.now() * .001;
				task = buildMipmaps(task);
				const end = performance.now() * .001;
				//console.log('TIME:', end - start);
				self.postMessage(task);
				break;
			}
			*/
			case WorkerTaskType.MIPMAP_COMPRESS: {
				const task = ev.data.task;
				// Compress the input image only (no mipmapping)
				const start = performance.now();

				const [levels, mipsSize, compressedSize] = computeMipmapLevels(task.width, task.height, task.channels);
				const lvlBuf = context.levelsBuffer;
				initLevelBuffer(lvlBuf, levels);
				const outBuf = context.compressDXToutBuffer;
				const inBuf = context.compressDXTinBuffer;
				inBuf.set(task.data);

				context.build_mipmaps(outBuf.byteOffset, inBuf.byteOffset, levels.length, lvlBuf.byteOffset);

				const end = performance.now();
				//console.log('TIME:', (end - start) * .001);

				task.levels = levels;
				task.data = undefined;
				task.dataBuffer = undefined;
				// We have to create a copy of the outBuf, otherwise we transfer ownership of that buffer...
				const buffer = outBuf.slice(0, compressedSize);
				task.compressedDataBuffer = buffer.buffer;

				//task.mipmaps = mipmaps;
				//task.compressedData = outBuf.slice(0, compressedSize);

				// @ts-expect-error - incorrect interface
				self.postMessage(task, [ task.compressedDataBuffer ]);
				break;
			}
			}
		};
	};
}