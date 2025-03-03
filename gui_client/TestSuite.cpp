/*=====================================================================
TestSuite.cpp
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TestSuite.h"


#include "ModelLoading.h"
#include "PhysicsWorld.h"
#include "TerrainTests.h"
#include "URLParser.h"
#include "CameraController.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../physics/TreeTest.h"
#include "../opengl/TextureLoading.h"
#include "../opengl/OpenGLEngineTests.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../graphics/GifDecoder.h"
#include "../graphics/NonZeroMipMap.h"
#include "../graphics/PNGDecoder.h"
#include "../graphics/EXRDecoder.h"
#include "../graphics/FormatDecoderVox.h"
#include "../graphics/BatchedMeshTests.h"
#include "../graphics/KTXDecoder.h"
#include "../graphics/ImageMapSequence.h"
#include "../graphics/PerlinNoise.h"
#include "../graphics/NoiseTests.h"
#include "../graphics/MeshSimplification.h"
#include "../graphics/SRGBUtils.h"
#include "../graphics/ImageMapTests.h"
#include "../graphics/TextureProcessing.h"
#include "../graphics/DXTCompression.h"
#include "../graphics/TextureProcessingTests.h"
#include "../graphics/jpegdecoder.h"
#include "../graphics/TextRenderer.h"
#include "../opengl/TextureLoadingTests.h"
#include "../indigo/UVUnwrapper.h"
#include "../audio/AudioFileReader.h"
#include "../utils/VectorUnitTests.h"
#include "../utils/ReferenceTest.h"
#include "../utils/JSONParser.h"
#include "../utils/PoolAllocator.h"
#include "../utils/FastPoolAllocator.h"
#include "../utils/TestUtils.h"
#include "../utils/StackAllocator.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"
#include "../utils/TopologicalSort.h"
#include "../utils/MemAlloc.h"
#include "../maths/CheckedMaths.h"
#include "../utils/SmallArray.h"
#include "../utils/SmallVector.h"
#include "../utils/BestFitAllocator.h"
#include "../utils/LinearIterSet.h"
#include "../utils/FastIterMap.h"
#include "../utils/ArenaAllocator.h"
#include "../utils/FileUtils.h"
#include "../utils/DatabaseTests.h"
#include "../utils/TaskTests.h"
#include "../audio/AudioResampler.h"
#include "../networking/URL.h"
#include "../networking/TLSSocketTests.h"
#include "../networking/HTTPClient.h"
#include "../networking/SMTPClient.h"
#include "../webserver/Escaping.h"
#include "../opengl/ui/TextEditingUtils.h"
#include <settings/XMLSettingsStore.h>
#include <functional>
#include <Sort.h>


#if BUILD_TESTS

// Run some test code, while checking for memory leaks, if we are running in debug mode on Windows.
static void runTest(std::function<void()> test_func, bool mem_leak_allowed = false)
{
	// Do mem snapshotting for leak detection.
#if defined(_DEBUG) && defined(_MSC_VER)
	_CrtMemState start_state, end_state, diff;
	memset(&start_state, 0, sizeof(_CrtMemState));
	memset(&end_state,   0, sizeof(_CrtMemState));
	memset(&diff,        0, sizeof(_CrtMemState));
	if(!mem_leak_allowed)
		_CrtMemCheckpoint(&start_state); // Capture memory state snapshot.
#endif

	test_func(); // Run the test

#if defined(_DEBUG) && defined(_MSC_VER)
	if(!mem_leak_allowed)
	{
		// If a memory leak is detected, you can tell VS to break at that particular allocation number, next time you run the program again with
		// _CrtSetBreakAlloc(N);
		// Where N is the number given in braces in the error message printed to the console.
		// This approach works better running in indigo_console, beacuse QT does a lot of allocations that differ in quantity each execution.
		_CrtMemCheckpoint(&end_state);
		if(_CrtMemDifference(&diff, &start_state, &end_state) != 0)
		{
			_CrtMemDumpAllObjectsSince(&start_state);

			conPrint("Memory Leak Detected: " + std::string(__FILE__) + ", line " + toString((int)__LINE__));
			assert(!"Memory Leak Detected");
			exit(1);
		}
	}
#endif
}

#endif // BUILD_TESTS


// NOTE: you should run the test suite in debug mode, to enable memory leak detection!
void TestSuite::test()
{
#if BUILD_TESTS

	conPrint("==============Doing Substrata unit tests ====================");
	Timer timer;

	PhysicsWorld::init(); // Needed to set memory allocator functions for Jolt, before ModelLoading tests etc.

#if EMSCRIPTEN
	const std::string base_dir_path = "."; // Shoudn't be used
#else
	const std::string base_dir_path = PlatformUtils::getResourceDirectoryPath();
#endif
	runTest([&]() { XMLSettingsStore::test(); });
	runTest([&]() { TextEditingUtils::test(); });
	runTest([&]() { JPEGDecoder::test(base_dir_path); });
	runTest([&]() { PNGDecoder::test(base_dir_path); });
	runTest([&]() { Vec4i::test(); });
	runTest([&]() { glare::testFastPoolAllocator(); });
	runTest([&]() { glare::TaskTests::test(); });
	runTest([&]() { MemAlloc::test(); });
	runTest([&]() { Maths::test(); });
	runTest([&]() { DatabaseTests::test(); });
	runTest([&]() { WorldObject::test(); });
	runTest([&]() { WorldMaterial::test(); });
	runTest([&]() { glare::ArenaAllocator::test(); });
	runTest([&]() { Matrix4f::test(); });
	runTest([&]() { NonZeroMipMap::test(); });
	runTest([&]() { js::VectorUnitTests::test(); });
	runTest([&]() { glare::StackAllocator::test(); });
	runTest([&]() { glare::AtomicInt::test(); });
	runTest([&]() { TextureProcessingTests::test(); });
	runTest([&]() { ImageMapTests::test(); });
	runTest([&]() { web::Escaping::test(); });
	runTest([&]() { URL::test(); });
	runTest([&]() { glare::testLinearIterSet(); });
	runTest([&]() { glare::testFastIterMap(); });
	runTest([&]() { SmallArrayTest::test(); });
	runTest([&]() { SmallVectorTest::test(); });
	runTest([&]() { glare::AudioResampler::test(); });
	runTest([&]() { Sort::test(); });
	runTest([&]() { glare::BestFitAllocator::test(); });
	runTest([&]() { testSRGBUtils(); });
	runTest([&]() { TopologicalSort::test(); });
	runTest([&]() { CheckedMaths::test(); });
	runTest([&]() { VoxelMeshBuilding::test(); });
	runTest([&]() { ModelLoading::test(); });
	runTest([&]() { glare::AudioFileReader::test(); });
	runTest([&]() { URLParser::test(); });
	runTest([&]() { testManagerWithCache(); });
	runTest([&]() { BitUtils::test(); });
	runTest([&]() { quaternionTests(); });
	runTest([&]() { Matrix3f::test(); });
	runTest([&]() { circularBufferTest(); });
	runTest([&]() { glare::testPoolAllocator(); });
	runTest([&]() { TextureLoadingTests::test(); });
	runTest([&]() { Timer::test(); });
	runTest([&]() { IPAddress::test(); });
	runTest([&]() { StringUtils::test(); });
	runTest([&]() { Vec4f::test(); });
	runTest([&]() { js::AABBox::test(); });
	runTest([&]() { ReferenceTest::run(); });
	runTest([&]() { CameraController::test(); });

#if !defined(EMSCRIPTEN)

	// These tests tend to use testfiles that we don't currently package for the Emscripten build, so don't run them.
	runTest([&]() { JSONParser::test(); });
	runTest([&]() { FileUtils::doUnitTests(); });
	runTest([&]() { LODGeneration::test(); });
	runTest([&]() { MeshSimplification::test(); });
	runTest([&]() { PhysicsWorld::test(); });
	runTest([&]() { FormatDecoderGLTF::test(); });
	runTest([&]() { BatchedMeshTests::test(); });
	runTest([&]() { EXRDecoder::test(); }, /*mem leak allowed=*/true); // OpenEXR leaks some minor stuff
	runTest([&]() { TextRenderer::test(); });
	//runTest([&]() { glare::AudioEngine::test(); }); // Disabled, noisy
	runTest([&]() { KTXDecoder::test(); });
	runTest([&]() { FormatDecoderVox::test(); });
	runTest([&]() { GIFDecoder::test(); }, /*mem leak allowed=*/true);
	runTest([&]() { TLSSocketTests::test(); }, /*mem leak allowed=*/true);
	runTest([&]() { HTTPClient::test(); }, /*mem leak allowed=*/true); // Leaks, probably due to TLS leaks.
	runTest([&]() { SMTPClient::test(); });

#endif // end if !defined(EMSCRIPTEN)

	// WMFVideoReader::test();
	// UVUnwrapper::test(); // Disabled as tries to load a bunch of Indigo test scenes
	// OpenGLEngineTests::test(base_dir_path); // Disabled as tries to load a bunch of Indigo test scenes

	
	conPrint("========== Successfully completed Substrata unit tests (Elapsed: " + timer.elapsedStringNPlaces(3) + ") ==========");

#else // else if !BUILD_TESTS:

	conPrint("BUILD_TESTS is not enabled, tests cannot be run.");
	exit(1);

#endif
}
