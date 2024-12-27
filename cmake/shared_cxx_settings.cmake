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

if(EMSCRIPTEN)
	include_directories("${GLARE_CORE_LIBS_ENV}/libjpeg-turbo/libjpeg-turbo-3.0.0-vs2022-install/include")
endif()

addIncludeDirectory(${jpegturbodir}/include)
#addIncludeDirectory(${jpegturbodir}) # This one works on linux/mac
addIncludeDirectory(${pngdir})
addIncludeDirectory(${pugixmldir})
addIncludeDirectory(${zlibdir})

# Include openexr.cmake to set include paths to OpenEXR and Imath dirs. 
include(${GLARE_CORE_TRUNK_DIR_ENV}/OpenEXR/openexr.cmake)

addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/utils")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/networking")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/maths")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/opengl") # For Glew
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/giflib/lib")
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/little_cms/include")
addIncludeDirectory("${zstddir}/lib")
addIncludeDirectory("${zstddir}/lib/common")

#Indigo SDK:
if(INDIGO_SUPPORT)
	addIncludeDirectory("${INDIGO_TRUNK_DIR_ENV}")
	addIncludeDirectory("${INDIGO_TRUNK_DIR_ENV}/dll/include")
endif()


# Add OpenCL paths
addIncludeDirectory("${GLARE_CORE_TRUNK_DIR_ENV}/opencl/khronos")

# BugSplat
addIncludeDirectory("${GLARE_CORE_LIBS_ENV}/BugSplat/BugSplat/inc")


# Append INDIGO_SHARED_INCLUDE_DIRS

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${INDIGO_SHARED_INCLUDE_DIRS}")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${INDIGO_SHARED_INCLUDE_DIRS}")


include_directories("${GLARE_CORE_TRUNK_DIR_ENV}/tracy/public")
if(TRACY_ENABLED)
	add_definitions(-DTRACY_ENABLE=1)
endif()



# some non windows preprocessor defs
if(WIN32)
	add_definitions(-DPLATFORM_WINDOWS)
else()
	add_definitions(-DCOMPILER_GCC -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS) # LLVM wants this to be defined.
endif()

add_definitions(-DNO_EMBREE)
add_definitions(-DMAP2D_FILTERING_SUPPORT=1)
add_definitions(-DUSING_LIBRESSL)
add_definitions(-DRAYMESH_TRACING_SUPPORT=1)

add_definitions(-DCMS_NO_REGISTER_KEYWORD) # Tell Little CMS not to use the register keyword, gives warnings and/or errors.
add_definitions(-DNO_LCMS_SUPPORT=1) # Disable Little CMS support, to reduce overall code size, and since we only need it for loading CMYK jpegs which are rare.

if(EMSCRIPTEN)
	add_definitions(-fwasm-exceptions)
	add_definitions(-pthread)

	add_definitions(-DEMSCRIPTEN=1)
	
	add_definitions(-msimd128)
	add_definitions(-msse4.1)
	#add_definitions(-msse4.2)

	add_definitions(-Wno-deprecated-builtins)
	add_definitions(-Wno-deprecated-non-prototype)

	#add_definitions("-sGL_ASSERTIONS")
	#add_definitions("-fsanitize=address")
	
	#add_definitions("--profiling")			# https://emscripten.org/docs/tools_reference/emcc.html
	#add_definitions("-gsource-map") # Generate a source map using LLVM debug information: https://emscripten.org/docs/tools_reference/emcc.html  NOTE: doesn't seem to work, doesn't give line numbers in stack traces.
	
	include_directories("${GLARE_CORE_LIBS_ENV}/emsdk/upstream/emscripten/cache/sysroot/include")
	
		
	# MESSAGE("original CMAKE_CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")                     # -g
	# MESSAGE("original CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}")                 # -O3 -DNDEBUG
	# MESSAGE("original CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")   # -O2 -g -DNDEBUG
	
	# MESSAGE("original CMAKE_C_FLAGS_DEBUG: ${CMAKE_C_FLAGS_DEBUG}")                         # -g
	# MESSAGE("original CMAKE_C_FLAGS_RELEASE: ${CMAKE_C_FLAGS_RELEASE}")                     # -O3 -DNDEBUG
	# MESSAGE("original CMAKE_C_FLAGS_RELWITHDEBINFO: ${CMAKE_C_FLAGS_RELWITHDEBINFO}")       # -O2 -g -DNDEBUG
	
	# Use -g1 for faster builds.  The disadvantage relative to -g is that it doesn't have function names in call stacks.
	SET(COMMON_C_CXX_OPTIONS_DEBUG				"-O0 -g1")
	SET(COMMON_C_CXX_OPTIONS_RELEASE			"-O3 -DNDEBUG")
	SET(COMMON_C_CXX_OPTIONS_RELWITHDEBINFO		"-O1 -g1 -DNDEBUG")  # NOTE: Using -O1 instead of -O2.  Links much faster (e.g. 2 seconds instead of 30 seconds)
	
	# Emscripten build times
	# (changing SDLClient.cpp)
	# ========================
	# Incremental build times with Emscripten, debug config: 
	# 37s
	# 
	# Incremental build times with Emscripten, RelWithDebInfo config: 
	# 33 s, 34 s
	# without --use-preload-cache etc, still 34 s.
	# 
	# Incremental build times with Emscripten, RelWithDebInfo config, -O1 -g1 -DNDEBUG:
	# ~ 6s 
	# 
	# Incremental build times with Emscripten, debug config, -O0 -g1:
	# ~ 10s

	SET(CMAKE_CXX_FLAGS_DEBUG			"${COMMON_C_CXX_OPTIONS_DEBUG}")
	SET(CMAKE_CXX_FLAGS_RELEASE			"${COMMON_C_CXX_OPTIONS_RELEASE}")
	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${COMMON_C_CXX_OPTIONS_RELWITHDEBINFO}")
	
	SET(CMAKE_C_FLAGS_DEBUG				"${COMMON_C_CXX_OPTIONS_DEBUG}")
	SET(CMAKE_C_FLAGS_RELEASE			"${COMMON_C_CXX_OPTIONS_RELEASE}")
	SET(CMAKE_C_FLAGS_RELWITHDEBINFO	"${COMMON_C_CXX_OPTIONS_RELWITHDEBINFO}")

elseif(WIN32)
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
	
	if(FUZZING)
		add_definitions(-DFUZZING=1)
		
		MESSAGE("================================ Enabling /fsanitize=fuzzer ============================")
		add_definitions(/fsanitize=fuzzer)
	endif()
	
	if(FUZZING OR (NOT USE_SANITIZER STREQUAL ""))
		MESSAGE("================================ Enabling /fsanitize=address ============================")
		add_definitions(/fsanitize=address)
		add_definitions(-D_DISABLE_VECTOR_ANNOTATION)
		add_definitions(-D_DISABLE_STRING_ANNOTATION)
	endif()
	
	# Consider some options.
	if(NO_WHOLE_PROGRAM_OPT)
		SET(GL_OPT)
	else()
		#SET(GL_OPT "/GL")
		# Whole program optimisations (/GL) temporarily disabled due to inlining problem:
		# https://developercommunity.visualstudio.com/t/Visual-Studio-not-inlining-small-functio/10412214?q=link+time+code+generation+inline
		SET(GL_OPT)
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
		SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Xarch_arm64")
	else()
		SET(APPLE_C_CXX_OPTIONS "-stdlib=libc++ -Wall -fPIC -mmmx -msse -msse2 -mssse3 -msse4.1 -arch x86_64 -Wthread-safety")
		SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Xarch_x86_64 -D__x86_64__")
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
	
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
	
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
add_definitions(-DOPENEXR_SUPPORT -DPNG_ALLOW_BENIGN_ERRORS -DPNG_INTEL_SSE -DPNG_NO_SETJMP -DWUFFS_SUPPORT=1)
