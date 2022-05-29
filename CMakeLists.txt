cmake_minimum_required (VERSION 2.8.7)

# set a dummy project name, so we can detect the bitness
project (dummy)

set(PROJECT_NAME "new_cyberspace")

# detect bitness, vs version
if(WIN32)
	if(CMAKE_CL_64)
		set(CYBERSPACE_BITNESS 64)
		set(CYBERSPACE_PLATFORM "x64")
		set(PROJECT_NAME "new_cyberspace64")
	else()
		message(FATAL_ERROR "Only 64 bit builds supported")
	endif()
	
	# See https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html, https://en.wikipedia.org/wiki/Microsoft_Visual_Studio
	if(MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS 1930)
		set(CYBERSPACE_VS_VER 2019)
		set(CYBERSPACE_LIBS_VS_VER 2019)
	elseif(MSVC_VERSION GREATER_EQUAL 1930 AND MSVC_VERSION LESS 1940)
		set(CYBERSPACE_VS_VER 2022)
		set(CYBERSPACE_LIBS_VS_VER 2022)
	else()
		message(FATAL_ERROR "Unhandled MSVC version")
	endif()
else()
	#linux stuff goes here
endif()

MESSAGE("CYBERSPACE_VS_VER: ${CYBERSPACE_VS_VER}")
MESSAGE("CYBERSPACE_LIBS_VS_VER: ${CYBERSPACE_LIBS_VS_VER}")

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



# To use a Clang sanitizer, for example address sanitizer, set on command line like so:
# -DINDIGO_USE_SANITIZER=address
#SET(INDIGO_USE_SANITIZER "" CACHE STRING "INDIGO_USE_SANITIZER")

#MESSAGE("INDIGO_USE_SANITIZER: ${INDIGO_USE_SANITIZER}")

SET(INDIGO_USE_LIBRESSL TRUE)
#MESSAGE("INDIGO_USE_LIBRESSL: ${INDIGO_USE_LIBRESSL}")

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

MESSAGE("\n----------Env vars:----------")

# Get env vars
checkAndGetEnvVar(INDIGO_LIBS INDIGO_LIBS_ENV)
MESSAGE("INDIGO_LIBS: ${INDIGO_LIBS_ENV}")

checkAndGetEnvVar(WINTER_DIR winterdir)
MESSAGE("Winter location: ${winterdir}")

checkAndGetEnvVar(CYBERSPACE_OUTPUT CYBERSPACE_OUTPUT_ENV)
MESSAGE("CYBERSPACE_OUTPUT: ${CYBERSPACE_OUTPUT_ENV}")

checkAndGetEnvVar(INDIGO_TRUNK_DIR INDIGO_TRUNK_DIR_ENV)
MESSAGE("INDIGO_TRUNK_DIR: ${INDIGO_TRUNK_DIR_ENV}")

checkAndGetEnvVar(WEBSITE_CORE_TRUNK_DIR WEBSITE_CORE_TRUNK_DIR_ENV)
MESSAGE("WEBSITE_CORE_TRUNK_DIR: ${WEBSITE_CORE_TRUNK_DIR_ENV}")

SET(CEF_BINARY_DISTRIB_DIR_ENV $ENV{CEF_BINARY_DISTRIB_DIR})
MESSAGE("CEF_BINARY_DISTRIB_DIR: ${CEF_BINARY_DISTRIB_DIR_ENV}")




# External library directory (apart from llvm, openssl and Qt)
set(tiffdir		"${INDIGO_TRUNK_DIR_ENV}/tiff-3.8.2/libtiff")
set(imathdir	"${INDIGO_TRUNK_DIR_ENV}/Imath")
set(openexrdir	"${INDIGO_TRUNK_DIR_ENV}/OpenEXR")
set(zlibdir		"${INDIGO_TRUNK_DIR_ENV}/zlib-1.2.11")
set(pugixmldir	"${INDIGO_TRUNK_DIR_ENV}/pugixml-1.9/src")
set(pngdir		"${INDIGO_TRUNK_DIR_ENV}/libpng-1.6.37")
set(zstddir		"${INDIGO_TRUNK_DIR_ENV}/zstd-1.5.2")
set(resonance_audio_dir "${INDIGO_LIBS_ENV}/resonance-audio/trunk/install/resonance_audio")

set(cef_binary_dist_dir ${CEF_BINARY_DISTRIB_DIR_ENV})

# The version of Qt we use needs to be accessed by both CMake and ruby build_dist script etc..
# So get via getConfigOption()
getConfigOption("--qtversion" INDIGO_QT_VERSION)
getConfigOption("--qtdir" INDIGO_QT_DIR)

getConfigOption("--version" SUBSTRATA_VERSION)

message("")
message("SUBSTRATA_VERSION: ${SUBSTRATA_VERSION}")


SET(INDIGO_LLVM_VERSION 6.0.0)
SET(INDIGO_LIBRESSL_VERSION 3.5.2)

SET(INDIGO_USE_SANITIZER "")

SET(PERFORMANCEAPI_ENABLED 0) # Superluminal instrumentation support
MESSAGE("PERFORMANCEAPI_ENABLED (Superluminal instrumentation support): ${PERFORMANCEAPI_ENABLED}")

if(WIN32)
	set(jpegturbodir "${INDIGO_LIBS_ENV}/libjpeg-turbo/libjpeg-turbo-vs${CYBERSPACE_LIBS_VS_VER}-install") # e.g. D:\programming\libjpeg-turbo\libjpeg-turbo-vs2019-install
else()
	set(jpegturbodir "${INDIGO_LIBS_ENV}/libjpeg-turbo/libjpeg-turbo-install")
	#set(jpegturbodir "${INDIGO_LIBS_ENV}/libjpeg-turbo-builds/build")
endif()


# The llvm dirs use the version seperator "_" instead of "."
string(REPLACE "." "_" INDIGO_LLVM_VERSION_PATH ${INDIGO_LLVM_VERSION})

# Qt
if(WIN32)	
	set(INDIGO_QT_DIR			"${INDIGO_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}-vs${CYBERSPACE_LIBS_VS_VER}-${CYBERSPACE_BITNESS}")
else()
	set(INDIGO_QT_DIR        	"${INDIGO_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}")
endif()

set(INDIGO_QT_INCLUDE_DIR	"${INDIGO_QT_DIR}/include")
set(INDIGO_QT_LIB_DIR	"${INDIGO_QT_DIR}/lib")

MESSAGE("\n----------Library versions:----------")
MESSAGE("QT version: ${INDIGO_QT_VERSION}")
MESSAGE("LLVM version: ${INDIGO_LLVM_VERSION}")
MESSAGE("LibreSSL version: ${INDIGO_LIBRESSL_VERSION}")


# LLVM, LibreSSL, OpenSSL
if(WIN32)
	# VS subdir
	set(VS_SUB_DIR "vs${CYBERSPACE_VS_VER}")
	
	set(LLVM_BUILD_VS_VER 2022)
	
	set(llvmdir "${INDIGO_LIBS_ENV}/llvm/llvm_${INDIGO_LLVM_VERSION_PATH}_install_vs${LLVM_BUILD_VS_VER}_${CYBERSPACE_BITNESS}")
	set(libressldir "${INDIGO_LIBS_ENV}/LibreSSL/libressl-${INDIGO_LIBRESSL_VERSION}-${CYBERSPACE_PLATFORM}-vs${CYBERSPACE_LIBS_VS_VER}-install")
		
	# Set the output directory.  Cmake will append the configuration string to this.
	set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	
	set (ACTUAL_FUCKING_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$(Configuration)")
	
else() # Mac or Linux.
	if(APPLE)
		set(llvmdir "${INDIGO_LIBS_ENV}/llvm/llvm_${INDIGO_LLVM_VERSION_PATH}_install")
	else()
		set(llvmdir "${INDIGO_LIBS_ENV}/llvm/llvm_${INDIGO_LLVM_VERSION_PATH}_dylib_install") # Use dylib build
	endif()
	set(libressldir	"${INDIGO_LIBS_ENV}/LibreSSL/libressl-${INDIGO_LIBRESSL_VERSION}-install")
	
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
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "SDKDebug")
		set (ACTUAL_FUCKING_OUTPUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_SDKDEBUG})
	else()
		MESSAGE(FATAL_ERROR "Unknown build type, can't set ACTUAL_FUCKING_OUTPUT_DIR")
	endif()

	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "$ENV{CYBERSPACE_OUTPUT}")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_SDKDEBUG "$ENV{CYBERSPACE_OUTPUT}/SDKDebug")
	
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "$ENV{CYBERSPACE_OUTPUT}/test_builds")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "$ENV{CYBERSPACE_OUTPUT}")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_SDKDEBUG "$ENV{CYBERSPACE_OUTPUT}/SDKDebug")

endif()

MESSAGE("\n----------Library dirs:----------")
MESSAGE("QT dir: ${INDIGO_QT_DIR}")
MESSAGE("LibJPEG Turbo dir: ${jpegturbodir}")
MESSAGE("llvmdir: ${llvmdir}")
MESSAGE("libressldir: ${libressldir}")
MESSAGE("openssldir: ${openssldir}")
MESSAGE("jpegturbodir: ${jpegturbodir}")

MESSAGE("")
MESSAGE("ACTUAL_FUCKING_OUTPUT_DIR: ${ACTUAL_FUCKING_OUTPUT_DIR}")


# The custom prebuild step target.
#add_custom_target(_INDIGO_PREBUILD_TARGET ALL ruby "${CMAKE_SOURCE_DIR}/scripts/prebuild.rb"
#	COMMENT "Running Indigo prebuild step")

# Also need to run the prebuild step before generating projects.
#execute_process(COMMAND ruby "${CMAKE_SOURCE_DIR}/scripts/prebuild.rb" OUTPUT_QUIET)

MESSAGE("") # New line


ADD_SUBDIRECTORY(libs)

# GUI client
ADD_SUBDIRECTORY(gui_client gui_client)

# server
ADD_SUBDIRECTORY(server server)

# Disabled for now as require INDIGO SDK:
#ADD_SUBDIRECTORY(cv_bot cv_bot)

#ADD_SUBDIRECTORY(lightmapper_bot lightmapper_bot)

#ADD_SUBDIRECTORY(screenshot_bot screenshot_bot)

#ADD_SUBDIRECTORY(eth_bot eth_bot)

#ADD_SUBDIRECTORY(backup_bot backup_bot)

#ADD_SUBDIRECTORY(webclient webclient)

#ADD_SUBDIRECTORY(stress_test stress_test)

if(APPLE)
else()
	SET(browser_process_suffix "")
	ADD_SUBDIRECTORY(browser_process browser_process)
endif()

if(APPLE)
	# Build all the helper processes that CEF requires.
	#SET(browser_process_suffix "")
	SET(BROWSER_PROCESS_TARGET_NAME, "gui_client_Helper") # Target names shouldn't have spaces in them.
	SET(BROWSER_PROCESS_OUTPUT_NAME, "gui_client Helper") # CEF on Mac requires a very particular naming scheme for CEF helper processes.
	ADD_SUBDIRECTORY(browser_process
		browser_process # binary (output) dir
	)
	
	SET(BROWSER_PROCESS_TARGET_NAME, "gui_client_Helper_GPU")
	SET(BROWSER_PROCESS_OUTPUT_NAME, "gui_client Helper (GPU)")
	ADD_SUBDIRECTORY(browser_process
		browser_process_gpu # binary dir
	)
	
	SET(BROWSER_PROCESS_TARGET_NAME, "gui_client_Helper_Renderer")
	SET(BROWSER_PROCESS_OUTPUT_NAME, "gui_client Helper (Renderer)")
	ADD_SUBDIRECTORY(browser_process
		browser_process_renderer
	)
	
	SET(BROWSER_PROCESS_TARGET_NAME, "gui_client_Helper_Plugin")
	SET(BROWSER_PROCESS_OUTPUT_NAME, "gui_client Helper (Plugin)")
	ADD_SUBDIRECTORY(browser_process
		browser_process_plugin
	)
 
    add_dependencies("gui_client" "gui_client Helper")
    add_dependencies("gui_client" "gui_client Helper (GPU)")
    add_dependencies("gui_client" "gui_client Helper (Renderer)")
    add_dependencies("gui_client" "gui_client Helper (Plugin)")
endif()
