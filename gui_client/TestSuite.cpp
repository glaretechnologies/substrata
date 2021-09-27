/*=====================================================================
TestSuite.cpp
-------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "TestSuite.h"


#include "ModelLoading.h"
#include "../shared/VoxelMeshBuilding.h"
#include "../physics/TreeTest.h"
#include "../opengl/OpenGLEngineTests.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../graphics/GifDecoder.h"
#include "../graphics/PNGDecoder.h"
#include "../graphics/FormatDecoderVox.h"
#include "../graphics/BatchedMeshTests.h"
#include "../graphics/KTXDecoder.h"
#include "../graphics/ImageMapSequence.h"
#include "../graphics/NoiseTests.h"
#include "../graphics/MeshSimplification.h"
#include "../opengl/TextureLoadingTests.h"
#include "../indigo/UVUnwrapper.h"
#include "../audio/AudioFileReader.h"
#include "../utils/VectorUnitTests.h"
#include "../utils/ReferenceTest.h"
#include "../utils/JSONParser.h"
#include "../utils/PoolAllocator.h"
#include "../utils/TestUtils.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"
#include <functional>


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
// render_test_scenes is enabled with --render_test_scenes
// run_comprehensive_tests is enabled with --comprehensive
void TestSuite::test(const std::string& appdata_path)
{
#if BUILD_TESTS

	const std::string indigo_base_dir_path = PlatformUtils::getResourceDirectoryPath();
	const std::string cache_dir_path = ".";

	conPrint("==============Doing Substrata unit tests ====================");

	Timer timer;

	runTest([&]() { VoxelMeshBuilding::test(); });
	
	runTest([&]() { ModelLoading::test(); });

	runTest([&]() { AudioFileReader::test(); });

	//TLSSocketTests::test();
	//URLParser::test();
	//testManagerWithCache();
	//GIFDecoder::test();
	///BitUtils::test();
	//MeshSimplification::test();
	//EnvMapProcessing::run(cyberspace_base_dir_path);
	//NoiseTests::test();
	//OpenGLEngineTests::buildData();
	//Matrix4f::test();
	//UVUnwrapper::test();
	//quaternionTests();
	//FormatDecoderGLTF::test();
	//BatchedMeshTests::test();
	//Matrix3f::test();
	//glare::AudioEngine::test();
	//circularBufferTest();
	//glare::testPoolAllocator();
	//WMFVideoReader::test();
	//TextureLoadingTests::test();
	//KTXDecoder::test();
	//BatchedMeshTests::test();
	//FormatDecoderVox::test();
	//HTTPClient::test();
	//GIFDecoder::test();
	//PNGDecoder::test();
	//FileUtils::doUnitTests();
	//StringUtils::test();
	//HTTPClient::test();
	//return 0;
	//GIFDecoder::test();
	//TLSSocketTests::test();
	//URLParser::test();
	//TextureLoading::test();
	//js::Triangle::test();
	//Timer::test();
	//IPAddress::test();
	//FormatDecoderGLTF::test();
	//JSONParser::test();
	//OpenGLEngineTests::test(cyberspace_base_dir_path);
	//StringUtils::test();
	//URL::test();
	//HTTPClient::test();
	//EnvMapProcessing::run(cyberspace_base_dir_path);
	//SMTPClient::test();
	//js::VectorUnitTests::test();
	//js::TreeTest::doTests(appdata_path);
	//Vec4f::test();
	//js::AABBox::test();
	//Matrix4f::test();
	//ReferenceTest::run();
	//Matrix4f::test();
	//CameraController::test();

	
	conPrint("========== Successfully completed Substrata unit tests (Elapsed: " + timer.elapsedStringNPlaces(3) + ") ==========");

#else // else if !BUILD_TESTS:

	conPrint("BUILD_TESTS is not enabled, tests cannot be run.");
	exit(1);

#endif
}
