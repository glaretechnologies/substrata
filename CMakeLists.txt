cmake_minimum_required (VERSION 3.5)

# set a dummy project name, so we can detect the bitness
project (dummy)

set(PROJECT_NAME "substrata")

# detect bitness, vs version
if(WIN32)
	if(CMAKE_CL_64)
		set(CYBERSPACE_BITNESS 64)
		set(CYBERSPACE_PLATFORM "x64")
	else()
		message(FATAL_ERROR "Only 64 bit builds supported")
	endif()
	
	# See https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html, https://en.wikipedia.org/wiki/Microsoft_Visual_Studio
	if(MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS 1930)
		set(CYBERSPACE_VS_VER 2019)
	elseif(MSVC_VERSION GREATER_EQUAL 1930 AND MSVC_VERSION LESS_EQUAL 1950)
		set(CYBERSPACE_VS_VER 2022)
	else()
		message(FATAL_ERROR "Unhandled MSVC version")
	endif()
	
	MESSAGE("CYBERSPACE_VS_VER: ${CYBERSPACE_VS_VER}")
endif()



# Set the real project name
project (${PROJECT_NAME})


if(WIN32)
	# Remove the MinSizeRel config.
	list(REMOVE_ITEM CMAKE_CONFIGURATION_TYPES MinSizeRel)

	LIST(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
	SET(CMAKE_CONFIGURATION_TYPES ${CMAKE_CONFIGURATION_TYPES} CACHE STRING "CMake configuration types" FORCE)
endif()


# suppress regen after build. this gets rid of the ZERO_CHECK project, which triggers the regen
#set(CMAKE_SUPPRESS_REGENERATION TRUE)


MESSAGE("
Boolean options: (Set with -DOPTION_NAME=TRUE)

Other options: (Set with -DOPTION_NAME=value)
Unix only:
CMAKE_BUILD_TYPE (valid options: Debug, Release, RelWithDebInfo)
")

# Print some info about compiler and toolset.
if(WIN32)
	if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "")
		set(USED_PLATFORM_TOOLSET "default")
	else()
		set(USED_PLATFORM_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
	endif()
	
	MESSAGE("Platform toolset: ${USED_PLATFORM_TOOLSET}")
endif()

if(NOT WIN32)
	if("${CMAKE_BUILD_TYPE}" STREQUAL "")
		MESSAGE(FATAL_ERROR "ERROR: No build type specified. Use -DCMAKE_BUILD_TYPE to specify a build type.")
	else()
		if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
			if(NOT CMAKE_BUILD_TYPE STREQUAL "Release") 
				if(NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")					
					MESSAGE(FATAL_ERROR "ERROR: ${CMAKE_BUILD_TYPE} is not a valid build type.")
				endif()
			endif()
		endif()
	endif()

	MESSAGE("Build type: ${CMAKE_BUILD_TYPE}")
endif()


# Includes
include("functions.cmake")


# Make sure stuff exists.
programAvailable("ruby")


SET(INDIGO_SUPPORT OFF CACHE BOOL "Enable building with Indigo Renderer.")


MESSAGE("\n----------Env vars:----------")

# Get env vars
checkAndGetEnvVar(GLARE_CORE_LIBS GLARE_CORE_LIBS_ENV)
MESSAGE("GLARE_CORE_LIBS: ${GLARE_CORE_LIBS_ENV}")

checkAndGetEnvVar(WINTER_DIR winterdir)
MESSAGE("Winter location: ${winterdir}")

checkAndGetEnvVar(CYBERSPACE_OUTPUT CYBERSPACE_OUTPUT_ENV)
MESSAGE("CYBERSPACE_OUTPUT: ${CYBERSPACE_OUTPUT_ENV}")

checkAndGetEnvVar(GLARE_CORE_TRUNK_DIR GLARE_CORE_TRUNK_DIR_ENV)
MESSAGE("GLARE_CORE_TRUNK_DIR: ${GLARE_CORE_TRUNK_DIR_ENV}")

SET(CEF_BINARY_DISTRIB_DIR_ENV $ENV{CEF_BINARY_DISTRIB_DIR})
MESSAGE("CEF_BINARY_DISTRIB_DIR: ${CEF_BINARY_DISTRIB_DIR_ENV}")

if(INDIGO_SUPPORT)
	checkAndGetEnvVar(INDIGO_TRUNK_DIR INDIGO_TRUNK_DIR_ENV)
	MESSAGE("INDIGO_TRUNK_DIR: ${INDIGO_TRUNK_DIR_ENV}")
endif()

MESSAGE("\n----------Cache vars:----------")


SET(CEF_SUPPORT ON CACHE BOOL "Enable use of Chromium Embedded Framework (CEF) - needed for webviews and video playback")
MESSAGE("CEF_SUPPORT: ${CEF_SUPPORT}")

SET(BUGSPLAT_SUPPORT ON CACHE BOOL "Enable building with BugSplat - the crash handling and bug reporting tool.")
MESSAGE("BUGSPLAT_SUPPORT: ${BUGSPLAT_SUPPORT}")


MESSAGE("INDIGO_SUPPORT: ${INDIGO_SUPPORT}")


SET(ENABLE_GLARE_PARCEL_AUCTION_CODE OFF CACHE BOOL "Enable use of private parcel auction code.")
SET(SUBSTRATA_PRIVATE_REPO_PATH "" CACHE PATH "Path to substrata-private repo.")


SET(TARGET_ARM64 OFF CACHE BOOL "Target ARM64 architecture.  Only used on Mac currently.")

if(TARGET_ARM64)
    MESSAGE("Targeting ARM64")
    SET(CMAKE_OS_ARCHITECTURES "arm64")
else()
    SET(CMAKE_OS_ARCHITECTURES "x86_64")
    MESSAGE("Targeting x64")
endif()

MESSAGE("ENABLE_GLARE_PARCEL_AUCTION_CODE: ${ENABLE_GLARE_PARCEL_AUCTION_CODE}")
MESSAGE("SUBSTRATA_PRIVATE_REPO_PATH: ${SUBSTRATA_PRIVATE_REPO_PATH}")


SET(SDL_BUILD_DIR 	"${GLARE_CORE_LIBS_ENV}/SDL/sdl_build" 	CACHE FILEPATH "SDL build directory")

MESSAGE("SDL_BUILD_DIR: ${SDL_BUILD_DIR}")

SET(USE_SDL OFF CACHE BOOL "Use SDL for client UI instead of Qt")
MESSAGE("USE_SDL (instead of Qt): ${USE_SDL}")


# External library directory (apart from llvm, openssl and Qt)
set(imathdir	"${GLARE_CORE_TRUNK_DIR_ENV}/Imath")
set(openexrdir	"${GLARE_CORE_TRUNK_DIR_ENV}/OpenEXR")
set(zlibdir		"${GLARE_CORE_TRUNK_DIR_ENV}/zlib")
set(pugixmldir	"${GLARE_CORE_TRUNK_DIR_ENV}/pugixml/src")
set(pngdir		"${GLARE_CORE_TRUNK_DIR_ENV}/libpng")
set(zstddir		"${GLARE_CORE_TRUNK_DIR_ENV}/zstd")

set(cef_binary_dist_dir ${CEF_BINARY_DISTRIB_DIR_ENV})

# The version of Qt we use needs to be accessed by both CMake and ruby build_dist script etc..
# So get via getConfigOption()
getConfigOption("--qtversion" INDIGO_QT_VERSION)
getConfigOption("--qtdir" INDIGO_QT_DIR)

getConfigOption("--version" SUBSTRATA_VERSION)

message("")
message("SUBSTRATA_VERSION: ${SUBSTRATA_VERSION}")


SET(SUBSTRATA_LLVM_VERSION 15.0.7) # NOTE: Also defined config_lib.rb.
SET(INDIGO_LIBRESSL_VERSION 3.5.2)
SET(SUBSTRATA_JPEGTURBO_VERSION 3.0.0)

SET(PERFORMANCEAPI_ENABLED OFF CACHE BOOL "Enable Superluminal instrumentation support") # Superluminal instrumentation support
MESSAGE("PERFORMANCEAPI_ENABLED (Superluminal instrumentation support): ${PERFORMANCEAPI_ENABLED}")

SET(TRACY_ENABLED OFF CACHE BOOL "Enable Tracy profiler instrumentation support")
MESSAGE("TRACY_ENABLED (Tracy profiler instrumentation support): ${TRACY_ENABLED}")

# To use a Clang sanitizer, for example address sanitizer, set on command line like so:
# -DUSE_SANITIZER=address or -DUSE_SANITIZER=thread
# Note that due to visual studio being shitty, you also need to manually set the 'Enable Address Sanitizer' setting in C/C++ > General project settings to 'Yes', otherwise
# you get an error at runtime: "clang_rt.asan_dynamic-x86-64.dll was not found".
SET(USE_SANITIZER "" CACHE STRING "USE_SANITIZER")
MESSAGE("USE_SANITIZER (address, thread): ${USE_SANITIZER}")

SET(FUZZING OFF CACHE BOOL "Enable fuzzing")
MESSAGE("FUZZING: ${FUZZING}")

if(EMSCRIPTEN)
	set(jpegturbodir "${GLARE_CORE_LIBS_ENV}/libjpeg-turbo/libjpeg-turbo-emscripten-build")
elseif(WIN32)
	set(jpegturbodir "${GLARE_CORE_LIBS_ENV}/libjpeg-turbo/libjpeg-turbo-${SUBSTRATA_JPEGTURBO_VERSION}-vs${CYBERSPACE_VS_VER}-install") # e.g. D:\programming\libjpeg-turbo\libjpeg-turbo-vs2019-install
else()
	set(jpegturbodir "${GLARE_CORE_LIBS_ENV}/libjpeg-turbo/libjpeg-turbo-${SUBSTRATA_JPEGTURBO_VERSION}-install")
	#set(jpegturbodir "${GLARE_CORE_LIBS_ENV}/libjpeg-turbo-builds/build")
endif()


# The llvm dirs use the version seperator "_" instead of "."
string(REPLACE "." "_" SUBSTRATA_LLVM_VERSION_PATH ${SUBSTRATA_LLVM_VERSION})

# Qt
if(WIN32)	
	set(INDIGO_QT_DIR			"${GLARE_CORE_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}-vs${CYBERSPACE_VS_VER}-${CYBERSPACE_BITNESS}")
else()
	set(INDIGO_QT_DIR        	"${GLARE_CORE_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}")
endif()

set(INDIGO_QT_INCLUDE_DIR	"${INDIGO_QT_DIR}/include")
set(INDIGO_QT_LIB_DIR	"${INDIGO_QT_DIR}/lib")

MESSAGE("\n----------Library versions:----------")
MESSAGE("QT version: ${INDIGO_QT_VERSION}")
MESSAGE("LLVM version: ${SUBSTRATA_LLVM_VERSION}")
MESSAGE("LibreSSL version: ${INDIGO_LIBRESSL_VERSION}")

enable_language( C CXX ASM )


# LLVM, LibreSSL, OpenSSL
if(WIN32)
	# VS subdir
	set(VS_SUB_DIR "vs${CYBERSPACE_VS_VER}")
	
	set(llvmdir "${GLARE_CORE_LIBS_ENV}/llvm/llvm_${SUBSTRATA_LLVM_VERSION_PATH}_install_vs${CYBERSPACE_VS_VER}_${CYBERSPACE_BITNESS}")
	set(libressldir "${GLARE_CORE_LIBS_ENV}/LibreSSL/libressl-${INDIGO_LIBRESSL_VERSION}-${CYBERSPACE_PLATFORM}-vs${CYBERSPACE_VS_VER}-install")
		
	# Set the output directory.  Cmake will append the configuration string to this.
	set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	
	set (ACTUAL_FUCKING_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$(Configuration)")
	
else() # Mac or Linux.
	if(APPLE)
		set(llvmdir "${GLARE_CORE_LIBS_ENV}/llvm/llvm_${SUBSTRATA_LLVM_VERSION_PATH}_install")
	else()
		set(llvmdir "${GLARE_CORE_LIBS_ENV}/llvm/llvm_${SUBSTRATA_LLVM_VERSION_PATH}_dylib_install") # Use dylib build
	endif()
	set(libressldir	"${GLARE_CORE_LIBS_ENV}/LibreSSL/libressl-${INDIGO_LIBRESSL_VERSION}-install")
	
	set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "$ENV{CYBERSPACE_OUTPUT}")
	
	# Set ACTUAL_FUCKING_OUTPUT_DIR
	if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
		set (ACTUAL_FUCKING_OUTPUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG})
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
		set (ACTUAL_FUCKING_OUTPUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "Release")
		set (ACTUAL_FUCKING_OUTPUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE})
	else()
		MESSAGE(FATAL_ERROR "Unknown build type, can't set ACTUAL_FUCKING_OUTPUT_DIR")
	endif()

	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "$ENV{CYBERSPACE_OUTPUT}")
	
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "$ENV{CYBERSPACE_OUTPUT}")

endif()

MESSAGE("\n----------Library dirs:----------")
MESSAGE("QT dir: ${INDIGO_QT_DIR}")
MESSAGE("LibJPEG Turbo dir: ${jpegturbodir}")
MESSAGE("llvmdir: ${llvmdir}")
MESSAGE("libressldir: ${libressldir}")
MESSAGE("openssldir: ${openssldir}")

MESSAGE("")
MESSAGE("ACTUAL_FUCKING_OUTPUT_DIR: ${ACTUAL_FUCKING_OUTPUT_DIR}")
MESSAGE("")


ADD_SUBDIRECTORY(libs)

# GUI client
ADD_SUBDIRECTORY(gui_client gui_client)

# server
ADD_SUBDIRECTORY(server server)

# Disabled for now as require INDIGO SDK:
#ADD_SUBDIRECTORY(cv_bot cv_bot)

#ADD_SUBDIRECTORY(lightmapper_bot lightmapper_bot)

#ADD_SUBDIRECTORY(screenshot_bot screenshot_bot)

#ADD_SUBDIRECTORY(${SUBSTRATA_PRIVATE_REPO_PATH}/eth_bot eth_bot)

#ADD_SUBDIRECTORY(backup_bot backup_bot)

#ADD_SUBDIRECTORY(webclient webclient)

#ADD_SUBDIRECTORY(stress_test stress_test)

if(CEF_SUPPORT)
	if(APPLE)
	else()
		SET(browser_process_suffix "")
		ADD_SUBDIRECTORY(browser_process browser_process)

		add_dependencies("gui_client" "browser_process")
	endif()

	if(APPLE)
		# Build all the helper processes that CEF requires.
		#SET(browser_process_suffix "")
		SET(BROWSER_PROCESS_TARGET_NAME "gui_client_Helper") # Target names shouldn't have spaces in them.
		SET(BROWSER_PROCESS_OUTPUT_NAME "gui_client Helper") # CEF on Mac requires a very particular naming scheme for CEF helper processes.
		ADD_SUBDIRECTORY(browser_process
			browser_process # binary (output) dir
		)
		
		SET(BROWSER_PROCESS_TARGET_NAME "gui_client_Helper_GPU")
		SET(BROWSER_PROCESS_OUTPUT_NAME "gui_client Helper (GPU)")
		ADD_SUBDIRECTORY(browser_process
			browser_process_gpu # binary dir
		)
		
		SET(BROWSER_PROCESS_TARGET_NAME "gui_client_Helper_Renderer")
		SET(BROWSER_PROCESS_OUTPUT_NAME "gui_client Helper (Renderer)")
		ADD_SUBDIRECTORY(browser_process
			browser_process_renderer
		)
		
		SET(BROWSER_PROCESS_TARGET_NAME "gui_client_Helper_Plugin")
		SET(BROWSER_PROCESS_OUTPUT_NAME "gui_client Helper (Plugin)")
		ADD_SUBDIRECTORY(browser_process
			browser_process_plugin
		)
	 
		add_dependencies("gui_client" "gui_client_Helper")
		add_dependencies("gui_client" "gui_client_Helper_GPU")
		add_dependencies("gui_client" "gui_client_Helper_Renderer")
		add_dependencies("gui_client" "gui_client_Helper_Plugin")
	endif()
endif()
