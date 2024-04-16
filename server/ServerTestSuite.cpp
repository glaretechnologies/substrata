/*=====================================================================
ServerTestSuite.cpp
-------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "ServerTestSuite.h"


#include "AccountHandlers.h"
#include "../shared/WorldObject.h"
#include "../shared/LODGeneration.h"
#include "../ethereum/RLP.h"
#include "../ethereum/Signing.h"
#include "../ethereum/Infura.h"
#include <networking/HTTPClient.h>
#include <webserver/ResponseUtils.h>
#include <WebWorkerThreadTests.h>
#include <WebSocketTests.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/PNGDecoder.h>
#include <graphics/GifDecoder.h>
#include <graphics/BatchedMeshTests.h>
#include <utils/PlatformUtils.h>
#include <utils/ConPrint.h>
#include <utils/Timer.h>
#include <utils/SHA256.h>
#include <utils/DatabaseTests.h>
#include <utils/BestFitAllocator.h>
#include <utils/Parser.h>
#include <utils/Keccak256.h>
#include <utils/CryptoRNG.h>
#include <utils/Base64.h>
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


void ServerTestSuite::test()
{
#if BUILD_TESTS

	conPrint("==============Doing Substrata server unit tests ====================");

	Timer timer;

	runTest([&]() { web::ResponseUtils::test();											});
	runTest([&]() { Parser::doUnitTests();												});
	runTest([&]() { StringUtils::test();												});
	runTest([&]() { CryptoRNG::test();													});
	runTest([&]() { Base64::test();														});
	runTest([&]() { SHA256::test();														});
	runTest([&]() { Keccak256::test();													});
	runTest([&]() { WorldMaterial::test();												});
	runTest([&]() { LODGeneration::test();												});
	runTest([&]() { WebSocketTests::test();												});
	runTest([&]() { GIFDecoder::test();													}, /*mem leak allowed=*/true); // NOTE: leaks mem due to https://sourceforge.net/p/giflib/bugs/165/
	runTest([&]() { PNGDecoder::test(".");												});
	runTest([&]() { glare::BestFitAllocator::test();									}, /*mem leak allowed=*/true); // Some tests intentionally leak mem
	runTest([&]() { FormatDecoderGLTF::test();											});
	runTest([&]() { DatabaseTests::test();												});
	runTest([&]() { RLP::test();														});
	runTest([&]() { Signing::test();													});
	runTest([&]() { AccountHandlers::test();											});
	runTest([&]() { HTTPClient::test();													}, /*mem leak allowed=*/true); // Leaks due to libtls allocating globals
	
	// runTest([&]() { BatchedMeshTests::test();										}); // Uses some Indigo files
	// runTest([&]() { Infura::test();													}); // Don't hit up Infura API usually
	// runTest([&]() { web::WebWorkerThreadTests::test();								}); // Doesn't return

	conPrint("========== Successfully completed Substrata server unit tests (Elapsed: " + timer.elapsedStringNPlaces(3) + ") ==========");

#else // else if !BUILD_TESTS:

	conPrint("BUILD_TESTS is not enabled, tests cannot be run.");
	exit(1);

#endif
}
