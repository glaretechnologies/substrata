# shared settings for console, sdk and gui
include(../cmake/llvm.cmake)
include(../cmake/ssl.cmake)


MESSAGE("Current target is: " ${CURRENT_TARGET})


#add_dependencies(${CURRENT_TARGET} _INDIGO_PREBUILD_TARGET)


if(WIN32)
	# add AuxFunctions as a dependency
	#add_dependencies(${CURRENT_TARGET} AuxFunctions)
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "")
	
	# /DEBUG /OPT:REF /OPT:ICF are for writing pdb files that can be used with minidumps.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /OPT:REF /OPT:ICF /LTCG")
	
	SET(INDIGO_WIN32_LIBS odbc32
		comctl32
		rpcrt4
		Iphlpapi 
		ws2_32 # Winsock
		delayimp # for delay loading via command line, as 2008 generator doesn't seem to support the VS setting for it
		)
elseif(APPLE)
	# NOTE: -stdlib=libc++ is needed for C++11.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++11 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework OpenCL -framework CoreServices")
else()
	# Linux
	# Add required Sanitizer link flags
	if(NOT INDIGO_USE_SANITIZER STREQUAL "")
		SET(SANITIZER_LINK_FLAGS "-fsanitize=${INDIGO_USE_SANITIZER} -fno-omit-frame-pointer -g -pie")
	endif()

	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN'")
endif()


#if(WIN32)
#	SET(INDIGO_EMBREE_LIB) # On Windows, embree is built as a dll that is loaded at runtime.
#else()
#	SET(INDIGO_EMBREE_LIB embree) # Linux and OSX need to link against the static embree lib.
#endif()

if(WIN32)
	SET(TURBOJPEG_LIB ${LIBJPEG_TURBO_DIR}/$(Configuration)/turbojpeg-static.lib)
else()
	SET(TURBOJPEG_LIB ${LIBJPEG_TURBO_DIR}/libjpeg.a)
endif()


target_link_libraries(${CURRENT_TARGET} 
indigo_libs
${INDIGO_WIN32_LIBS}
${LINUX_LIBS}
${TURBOJPEG_LIB}
)



