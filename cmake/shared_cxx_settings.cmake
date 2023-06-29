# shared cxx settings for all targets

if(WIN32)
	SET(INCLUDE_ARG "/I")
else()
	SET(INCLUDE_ARG "-I")
endif()

# adds dir_to_include to INDIGO_SHARED_INCLUDE_DIRS
macro(addIncludeDirectory dir_to_include)
	SET(INDIGO_SHARED_INCLUDE_DIRS "${INDIGO_SHARED_INCLUDE_DIRS} ${INCLUDE_ARG}\"${dir_to_include}\"")
endmacro(addIncludeDirectory)


addIncludeDirectory(${jpegturbodir}/include)
#addIncludeDirectory(${jpegturbodir}) # This one works on linux/mac
addIncludeDirectory(${pngdir})
addIncludeDirectory(${tiffdir})
addIncludeDirectory(${pugixmldir})
addIncludeDirectory(${zlibdir})


# OpenEXR:
addIncludeDirectory("${imathdir}/src/Imath")
addIncludeDirectory("${openexrdir}/src/lib/Iex")
addIncludeDirectory("${openexrdir}/src/lib/IlmThread")
addIncludeDirectory("${openexrdir}/src/lib/OpenEXR")
addIncludeDirectory("${openexrdir}/src/lib/OpenEXRUtil")

# OpenEXR per-platform config files:
if(WIN32)
	addIncludeDirectory("${imathdir}/config_windows")
	addIncludeDirectory("${openexrdir}/config_windows")
elseif(APPLE)
	addIncludeDirectory("${imathdir}/config_mac")
	addIncludeDirectory("${openexrdir}/config_mac")
else()
	addIncludeDirectory("${imathdir}/config_linux")
	addIncludeDirectory("${openexrdir}/config_linux")
endif()


addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/utils")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/networking")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/maths")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/opengl") # For Glew
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/giflib/lib")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/little_cms/include")
addIncludeDirectory("${zstddir}/lib")
addIncludeDirectory("${zstddir}/lib/common")

#Indigo SDK:
#addIncludeDirectory("${INDIGO_TRUNK_DIR_ENV}/dll/include")


# Add OpenCL paths
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/opencl/khronos")

# BugSplat
addIncludeDirectory("${INDIGO_LIBS_ENV}/BugSplat/inc")


# Append INDIGO_SHARED_INCLUDE_DIRS

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${INDIGO_SHARED_INCLUDE_DIRS}")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${INDIGO_SHARED_INCLUDE_DIRS}")


# some non windows preprocessor defs
if(WIN32)
	add_definitions(-DPLATFORM_WINDOWS)
else()
	add_definitions(-DCOMPILER_GCC -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS) # LLVM wants this to be defined.
endif()

add_definitions(-DNO_EMBREE)
add_definitions(-DGEOMETRY_NO_TREE_BUILD_SUPPORT) # Don't build BVH stuff, using Jolt for now.
add_definitions(-DMAP2D_FILTERING_SUPPORT=1)
add_definitions(-DUSING_LIBRESSL)

add_definitions(-DCMS_NO_REGISTER_KEYWORD) # Tell Little CMS not to use the register keyword, gives warnings and/or errors.

if(WIN32)
	# TEMP: needed for building LLVM with address sanitizer on Windows, which has:
	# include <sanitizer/asan_interface.h>
	# addIncludeDirectory("C:/Program Files/Microsoft Visual Studio/2022/Preview/VC/Tools/MSVC/14.32.31302/crt/src")

	add_definitions(-DWIN32_LEAN_AND_MEAN) # Speed up parsing of Windows.h
	add_definitions(-DNOMINMAX) # Stop windows.h from defining the min() and max() macros

	add_definitions(-DUNICODE -D_UNICODE)
	add_definitions(/MP)
	
	# Set warning level to 4 - this shows some useful stuff not in /W3 in vs2012.
	add_definitions(/W4)
	
	##### Ignore some warnings #####	
	# add_definitions(/wd4146) # 'unary minus operator applied to unsigned type, result still unsigned'
	# add_definitions(/wd4800) # ''type' : forcing value to bool 'true' or 'false' (performance warning)'
	
	# ''function': was declared deprecated'
	add_definitions(/wd4996)
	
	# ''this' : used in base member initializer list' (get it in LLVM a lot)
	add_definitions(/wd4355)
	
	# 'conditional expression is constant' - don't want this warning as we tend to turn off code with 'if(false) {}'
	add_definitions(/wd4127)

	# 'unreferenced formal parameter'
	add_definitions(/wd4100)
	
	# 'assignment operator could not be generated'
	add_definitions(/wd4512)	

	# 'nonstandard extension used : nameless struct/union'
	add_definitions(/wd4201)
	
	# warning C5240: 'nodiscard': attribute is ignored in this syntactic position (compiling source file O:\new_cyberspace\trunk\qt
	# Getting in 5.13.2 Qt headers.
	add_definitions(/wd5240)
	
	################################
	
	add_definitions(/GS-) # Disable Security Check (/GS-)
	add_definitions(/fp:fast) # Set fast floating point model.
	
	add_definitions(-D__SSE4_1__)
	
	add_definitions(/std:c++17)
	add_definitions(/Zc:__cplusplus) # Qt wants this for some reason
	
	if(PERFORMANCEAPI_ENABLED)
		add_definitions(-DPERFORMANCEAPI_ENABLED=1) # Turn on or off superluminal profiler integration.
	else()
		add_definitions(-DPERFORMANCEAPI_ENABLED=0) # We need to set it to 0 explicitly to disable.
	endif()
	
	# Consider some options.
	if(NO_WHOLE_PROGRAM_OPT)
		SET(GL_OPT)
	else()
		SET(GL_OPT "/GL")
	endif()
	
	# Append optimisation flags.
	SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			")
	SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			-D_SECURE_SCL=0 /O2 -DNDEBUG ${GL_OPT} /Zi") # /Zi = use program database for debug info (.pdb)
	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	-D_SECURE_SCL=0 /O2 -DNDEBUG")
	
elseif(APPLE)
	add_definitions(-DOSX -DINDIGO_NO_OPENMP)
	add_definitions(-D__NO_AVX__)

	# Jolt uses shared_mutex which was introduced in macOS 10.12.
	SET(CMAKE_OSX_DEPLOYMENT_TARGET "10.12")
	SET(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")

	add_definitions(-DOSX_DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET}")

	# NOTE: -stdlib=libc++ is needed for C++11.
	# -Wthread-safety is Thread Safety Analysis: https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
	if(TARGET_ARM64)
		SET(APPLE_C_CXX_OPTIONS "-stdlib=libc++ -Wall -fPIC -arch arm64 -Wthread-safety")
	else()
		SET(APPLE_C_CXX_OPTIONS "-stdlib=libc++ -Wall -fPIC -mmmx -msse -msse2 -mssse3 -msse4.1 -arch x86_64 -Wthread-safety")
	endif()
	
	SET(COMMON_C_CXX_OPTIONS_DEBUG				"${APPLE_C_CXX_OPTIONS} -gdwarf-2")
	SET(COMMON_C_CXX_OPTIONS_RELEASE			"${APPLE_C_CXX_OPTIONS} -gdwarf-2 -O3 -DNDEBUG") # NOTE: removed -fvisibility=hidden to allow debug symbols in exe.
	# For some reason tests will fail with -O2 so we build with settigns similar to release
	#SET(COMMON_C_CXX_OPTIONS_RELWITHDEBINFO	"${APPLE_C_CXX_OPTIONS} -O2 -DNDEBUG")
	SET(COMMON_C_CXX_OPTIONS_RELWITHDEBINFO		"${APPLE_C_CXX_OPTIONS} -gdwarf-2 -O3 -DNDEBUG")

	# Append optimisation and some other flags.
	# NOTE: -Wno-reorder gets rid of warnings like: warning: 'IndigoDriver::appdata_path' will be initialized after 'std::string IndigoDriver::scenefilepath'.
	# NOTE: c++14 seems to be needed by CEF, c++17 seems to be needed by Jolt.
	SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			${COMMON_C_CXX_OPTIONS_DEBUG} -std=c++17 -Wno-reorder")
	SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			${COMMON_C_CXX_OPTIONS_RELEASE} -std=c++17 -fvisibility-inlines-hidden -Wno-reorder")
	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	${COMMON_C_CXX_OPTIONS_RELWITHDEBINFO} -std=c++17 -fvisibility-inlines-hidden -Wno-reorder")
	
	SET(CMAKE_C_FLAGS_DEBUG				"${CMAKE_C_FLAGS_DEBUG}				${COMMON_C_CXX_OPTIONS_DEBUG}")
	SET(CMAKE_C_FLAGS_RELEASE			"${CMAKE_C_FLAGS_RELEASE}			${COMMON_C_CXX_OPTIONS_RELEASE}")
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO	"${CMAKE_C_FLAGS_RELWITHDEBINFO}	${COMMON_C_CXX_OPTIONS_RELWITHDEBINFO}")

else() # Linux
	add_definitions(-D__SSSE3__ -D__NO_AVX__)

	SET(LINUX_C_CXX_OPTIONS "-Wall -fPIC -pthread -mmmx -msse -msse2 -mssse3 -msse4.1")
	
	# Turn on address/memory etc.. sanitizer if requested.  See http://code.google.com/p/address-sanitizer/wiki/AddressSanitizer 
	if(NOT USE_SANITIZER STREQUAL "")
		# Also emit frame pointers and debug info (-g)
		# Thread sanitizer requires -fPIE so just add it in for all sanitizers.
		SET(LINUX_C_CXX_OPTIONS "${LINUX_C_CXX_OPTIONS} -fsanitize=${USE_SANITIZER} -fno-omit-frame-pointer -g") # -fPIE
	endif()

	SET(COMMON_C_CXX_OPTIONS_DEBUG				"${LINUX_C_CXX_OPTIONS} -g")
	SET(COMMON_C_CXX_OPTIONS_RELEASE			"${LINUX_C_CXX_OPTIONS} -O3 -g -DNDEBUG")
	SET(COMMON_C_CXX_OPTIONS_RELWITHDEBINFO		"${LINUX_C_CXX_OPTIONS} -O2 -g -DNDEBUG")

	# Append optimisation flags.
	SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			${COMMON_C_CXX_OPTIONS_DEBUG} -std=c++17 -Wno-reorder")
	SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			${COMMON_C_CXX_OPTIONS_RELEASE} -std=c++17 -Wno-reorder")
	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	${COMMON_C_CXX_OPTIONS_RELWITHDEBINFO} -std=c++17 -Wno-reorder")
	
	SET(CMAKE_C_FLAGS_DEBUG				"${CMAKE_C_FLAGS_DEBUG}				${COMMON_C_CXX_OPTIONS_DEBUG}")
	SET(CMAKE_C_FLAGS_RELEASE			"${CMAKE_C_FLAGS_RELEASE}			${COMMON_C_CXX_OPTIONS_RELEASE}")
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO	"${CMAKE_C_FLAGS_RELWITHDEBINFO}	${COMMON_C_CXX_OPTIONS_RELWITHDEBINFO}")
endif()


# Append BUILD_TEST=1 preprocessor def for debug + release with debug info, all platforms.
SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	-DBUILD_TESTS=1")
SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			-DBUILD_TESTS=1")

SET(CMAKE_C_FLAGS_RELWITHDEBINFO	"${CMAKE_C_FLAGS_RELWITHDEBINFO}	-DBUILD_TESTS=1")
SET(CMAKE_C_FLAGS_DEBUG				"${CMAKE_C_FLAGS_DEBUG}				-DBUILD_TESTS=1")


# Add general preprocessor definitions.
add_definitions(-DOPENEXR_SUPPORT -DPNG_ALLOW_BENIGN_ERRORS -DPNG_INTEL_SSE -DPNG_NO_SETJMP)
