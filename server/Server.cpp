/*=====================================================================
Server.cpp
---------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "Server.h"


#include "ListenerThread.h"
#include <ThreadManager.h>
#include <PlatformUtils.h>
#include "../networking/networking.h"
#include <Clock.h>
#include <FileUtils.h>
#include <ConPrint.h>
#include <Parser.h>
#include <Base64.h>
#include <ImmutableVector.h>
#include <ArgumentParser.h>


int main(int argc, char *argv[])
{
	Clock::init();
	Networking::createInstance();
	PlatformUtils::ignoreUnixSignals();

	try
	{

		//---------------------- Parse and process comment line arguments -------------------------
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--src_resource_dir"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // One string arg

		std::vector<std::string> args;
		for(int i=0; i<argc; ++i)
			args.push_back(argv[i]);

		ArgumentParser parsed_args(args, syntax);

		// src_resource_dir can be set to something like C:\programming\chat_site\trunk to read e.g. script.js directly from trunk
		std::string src_resource_dir = "./";
		if(parsed_args.isArgPresent("--src_resource_dir"))
			src_resource_dir = parsed_args.getArgStringValue("--src_resource_dir");

		conPrint("src_resource_dir: '" + src_resource_dir + "'");

		// Run tests if --test is present.
		if(parsed_args.isArgPresent("--test") || parsed_args.getUnnamedArg() == "--test")
		{
#if BUILD_TESTS
			Base64::test();
			Parser::doUnitTests();
			conPrint("----Finished tests----");
#endif
			return 0;
		}
		//-----------------------------------------------------------------------------------------

		const int listen_port = 1234;
		conPrint("listen port: " + toString(listen_port));

		ThreadManager thread_manager;
		thread_manager.addThread(new ListenerThread(listen_port, NULL));
		//thread_manager.addThread(new DataStoreSavingThread(data_store));

		while(1)
		{
			PlatformUtils::Sleep(10000);
		}
	}
	catch(ArgumentParserExcep& e)
	{
		conPrint("ArgumentParserExcep: " + e.what());
		return 1;
	}

	Networking::destroyInstance();
	return 0;
}
