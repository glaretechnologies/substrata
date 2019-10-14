cmake_minimum_required (VERSION 2.8.7)

# set a dummy project name, so we can detect the bitness
project (dummy)

set(PROJECT_NAME "new_cyberspace")

# detect bitness, vs version
if(WIN32)
	if(CMAKE_CL_64)
		#set(INDIGO_64_BIT TRUE)
		set(CYBERSPACE_BITNESS 64)
		set(CYBERSPACE_PLATFORM "x64")
		set(PROJECT_NAME "new_cyberspace64")
	else()
		message(FATAL_ERROR "Only 64 bit builds supported")
	endif()
	
	if(MSVC_VERSION EQUAL 1700) # VS2012
		set(CYBERSPACE_VS_VER 2012)
	elseif(MSVC_VERSION EQUAL 1800) # VS2013
		set(CYBERSPACE_VS_VER 2013)
	elseif(MSVC_VERSION EQUAL 1900) # VS2015
		set(CYBERSPACE_VS_VER 2015)
		elseif(MSVC_VERSION GREATER_EQUAL 1911 AND MSVC_VERSION LESS 1920) # VS2017 has several version numbers 1911, 1912, 1915
		set(CYBERSPACE_VS_VER 2017)
	elseif(MSVC_VERSION GREATER_EQUAL 1920)
		set(CYBERSPACE_VS_VER 2019)
	else()
		message(FATAL_ERROR "Unhandled MSVC version")
	endif()
else()
	#linux stuff goes here
	#set(INDIGO_64_BIT FALSE)
endif()

MESSAGE("CYBERSPACE_VS_VER: ${CYBERSPACE_VS_VER}")

# Set the real project name
project (${PROJECT_NAME})


if(WIN32)
	# Remove the MinSizeRel config.
	list(REMOVE_ITEM CMAKE_CONFIGURATION_TYPES MinSizeRel)

	LIST(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
	SET(CMAKE_CONFIGURATION_TYPES ${CMAKE_CONFIGURATION_TYPES} CACHE STRING "CMake configuration types" FORCE)
endif()

set(CMAKE_C_FLAGS_SDKDEBUG ${CMAKE_C_FLAGS_DEBUG})
set(CMAKE_CXX_FLAGS_SDKDEBUG ${CMAKE_CXX_FLAGS_DEBUG})
set(CMAKE_EXE_LINKER_FLAGS_SDKDEBUG ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
set(CMAKE_SHARED_LINKER_FLAGS_SDKDEBUG ${CMAKE_SHARED_LINKER_FLAGS_DEBUG})
set(CMAKE_MODULE_LINKER_FLAGS_SDKDEBUG ${CMAKE_MODULE_LINKER_FLAGS_DEBUG})

mark_as_advanced(CMAKE_C_FLAGS_SDKDEBUG CMAKE_CXX_FLAGS_SDKDEBUG CMAKE_EXE_LINKER_FLAGS_SDKDEBUG CMAKE_SHARED_LINKER_FLAGS_SDKDEBUG CMAKE_MODULE_LINKER_FLAGS_SDKDEBUG)

# suppress regen after build. this gets rid of the ZERO_CHECK project, which triggers the regen
set(CMAKE_SUPPRESS_REGENERATION TRUE)


MESSAGE("
Boolean options: (Set with -DOPTION_NAME=TRUE)


Other options: (Set with -DOPTION_NAME=value)
Unix only:
CMAKE_BUILD_TYPE (valid options: Debug, Release, RelWithDebInfo)
")

# Print some info about compiler and toolset.
if(WIN32)
	MESSAGE("Microsoft Visual C/C++ version: ${MSVC_VERSION}")
	MESSAGE("Microsoft Visual Studio version: ${CYBERSPACE_VS_VER}")
	
	if("${CMAKE_GENERATOR_TOOLSET}" STREQUAL "")
		set(USED_PLATFORM_TOOLSET "default")
	else()
		set(USED_PLATFORM_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
	endif()
	
	MESSAGE("Platform toolset: ${USED_PLATFORM_TOOLSET}")
endif()

MESSAGE("
Options:")

#OPTION(INDIGO_CONSOLE_ONLY "Build only the console executable" FALSE)
#if(INDIGO_CONSOLE_ONLY)
#	MESSAGE("INDIGO_CONSOLE_ONLY=TRUE.")
#else()
#	MESSAGE("INDIGO_CONSOLE_ONLY=FALSE.")
#endif()


# To use a Clang sanitizer, for example address sanitizer, set on command line like so:
# -DINDIGO_USE_SANITIZER=address
#SET(INDIGO_USE_SANITIZER "" CACHE STRING "INDIGO_USE_SANITIZER")

#MESSAGE("INDIGO_USE_SANITIZER: ${INDIGO_USE_SANITIZER}")

#OPTION(INDIGO_USE_LCOV "Build with LCOV (code coverage) support (GCC only)" FALSE)
#if(INDIGO_USE_LCOV)
#	MESSAGE("INDIGO_USE_LCOV=TRUE.")
#else()
#	MESSAGE("INDIGO_USE_LCOV=FALSE")
#endif()

#OPTION(INDIGO_USE_LIBRESSL "Build with LibreSSL" FALSE)
#if(INDIGO_USE_LIBRESSL)
#	MESSAGE("INDIGO_USE_LIBRESSL=TRUE.")
#else()
#	MESSAGE("INDIGO_USE_LIBRESSL=FALSE")
#endif()
SET(INDIGO_USE_LIBRESSL TRUE)
MESSAGE("INDIGO_USE_LIBRESSL: ${INDIGO_USE_LIBRESSL}")

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


MESSAGE("
Env vars:")


# Get env vars
checkAndGetEnvVar(INDIGO_LIBS INDIGO_LIBS_ENV)
MESSAGE("INDIGO_LIBS: ${INDIGO_LIBS_ENV}")

checkAndGetEnvVar(WINTER_DIR winterdir)
MESSAGE("Winter location: ${winterdir}")

checkAndGetEnvVar(CYBERSPACE_OUTPUT CYBERSPACE_OUTPUT_ENV)
MESSAGE("CYBERSPACE_OUTPUT: ${CYBERSPACE_OUTPUT_ENV}")

checkAndGetEnvVar(INDIGO_TRUNK_DIR INDIGO_TRUNK_DIR_ENV)
MESSAGE("INDIGO_TRUNK_DIR: ${INDIGO_TRUNK_DIR_ENV}")


MESSAGE("
Library versions:")


# External library directory (apart from llvm, openssl and Qt)
set(jpegdir		"${INDIGO_LIBS_ENV}/libjpeg-turbo-master")
set(tiffdir		"${INDIGO_LIBS_ENV}/tiff-3.8.2/libtiff")
set(ilmbasedir	"${INDIGO_TRUNK_DIR_ENV}/IlmBase")
set(openexrdir	"${INDIGO_TRUNK_DIR_ENV}/OpenEXR")
set(zlibdir		"${INDIGO_TRUNK_DIR_ENV}/zlib-1.2.11")
set(pugixmldir	"${INDIGO_TRUNK_DIR_ENV}/pugixml-1.9/src")
set(pngdir		"${INDIGO_TRUNK_DIR_ENV}/lpng1635")

# The version of Qt we use needs to be accessed by both CMake and ruby build_dist script etc..
# So get via getConfigOption()
getConfigOption("--qtversion" INDIGO_QT_VERSION)
getConfigOption("--qtdir" INDIGO_QT_DIR)

#if(WIN32)
#	SET(INDIGO_QT_VERSION 5.7.0)
#	SET(INDIGO_QT_DIR "${INDIGO_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}-vs${CYBERSPACE_VS_VER}-64")
#else() # Mac or Linux.
#	SET(INDIGO_QT_VERSION 5.8.0)
#	SET(INDIGO_QT_DIR "${INDIGO_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}")
#endif()

SET(INDIGO_LLVM_VERSION 6.0.0)
SET(INDIGO_LIBRESSL_VERSION 2.8.3)
SET(INDIGO_OPENSSL_VERSION 1.0.2a)
SET(LIBJPEG_TURBO_DIR 1.0.2a)

SET(INDIGO_USE_SANITIZER "")

if(WIN32)
	set(LIBJPEG_TURBO_DIR "${INDIGO_LIBS_ENV}/libjpeg-turbo-builds/vs_${CYBERSPACE_VS_VER}_64")
else()
	set(LIBJPEG_TURBO_DIR "${INDIGO_LIBS_ENV}/libjpeg-turbo-builds/build")
endif()


SET(MYSQL_CONNECTOR_DIR "${INDIGO_LIBS_ENV}/mysql_connector_install")


# The llvm dirs use the version seperator "_" instead of "."
string(REPLACE "." "_" INDIGO_LLVM_VERSION_PATH ${INDIGO_LLVM_VERSION})

# Qt
if(WIN32)	
	set(INDIGO_QT_DIR			"${INDIGO_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}-vs${CYBERSPACE_VS_VER}-${CYBERSPACE_BITNESS}")
else()
	set(INDIGO_QT_DIR        	"${INDIGO_LIBS_ENV}/Qt/${INDIGO_QT_VERSION}")
endif()

set(INDIGO_QT_INCLUDE_DIR	"${INDIGO_QT_DIR}/include")
set(INDIGO_QT_LIB_DIR	"${INDIGO_QT_DIR}/lib")

MESSAGE("QT version: ${INDIGO_QT_VERSION}")
MESSAGE("LLVM version: ${INDIGO_LLVM_VERSION}")
MESSAGE("LibreSSL version: ${INDIGO_LIBRESSL_VERSION}")
MESSAGE("OpenSSL version: ${INDIGO_OPENSSL_VERSION}")
MESSAGE("QT dir: ${INDIGO_QT_DIR}")
MESSAGE("LibJPEG Turbo dir: ${LIBJPEG_TURBO_DIR}")
MESSAGE("MySQL connector dir: ${MYSQL_CONNECTOR_DIR}")

# LLVM, LibreSSL, OpenSSL
if(WIN32)
	# VS subdir
	set(VS_SUB_DIR "vs${CYBERSPACE_VS_VER}")
	
	set(llvmdir "${INDIGO_LIBS_ENV}/llvm/llvm_${INDIGO_LLVM_VERSION_PATH}_install_vs${CYBERSPACE_VS_VER}_${CYBERSPACE_BITNESS}")
	set(libressldir "${INDIGO_LIBS_ENV}/LibreSSL/libressl-${INDIGO_LIBRESSL_VERSION}-${CYBERSPACE_PLATFORM}-vs${CYBERSPACE_VS_VER}-install")
	set(openssldir "${INDIGO_LIBS_ENV}/OpenSSL/openssl-${INDIGO_OPENSSL_VERSION}-${CYBERSPACE_PLATFORM}-vs${CYBERSPACE_VS_VER}")
		
	# Set the output directory.  Cmake will append the configuration string to this.
	set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "$ENV{CYBERSPACE_OUTPUT}/${VS_SUB_DIR}/cyberspace_${CYBERSPACE_PLATFORM}")
	
	set (ACTUAL_FUCKING_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$(Configuration)")
	
else() # Mac or Linux.
	set(llvmdir "${INDIGO_LIBS_ENV}/llvm/llvm_${INDIGO_LLVM_VERSION_PATH}_install")
	set(libressldir	"${INDIGO_LIBS_ENV}/LibreSSL/libressl-${INDIGO_LIBRESSL_VERSION}-install")
	set(openssldir "${INDIGO_LIBS_ENV}/OpenSSL/openssl-${INDIGO_OPENSSL_VERSION}-install")
	
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

message("ACTUAL_FUCKING_OUTPUT_DIR: ${ACTUAL_FUCKING_OUTPUT_DIR}")


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

ADD_SUBDIRECTORY(cv_bot cv_bot)
