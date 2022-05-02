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
		Iphlpapi  # For GetAdaptersInfo() in SystemInfo::getMACAddresses().
		ws2_32 # Winsock
		delayimp # for delay loading via command line, as 2008 generator doesn't seem to support the VS setting for it
		)
elseif(APPLE)
	# NOTE: -stdlib=libc++ is needed for C++11.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++11 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework CoreServices")
else()
	# Linux
	# Add required Sanitizer link flags
	if(NOT INDIGO_USE_SANITIZER STREQUAL "")
		SET(SANITIZER_LINK_FLAGS "-fsanitize=${INDIGO_USE_SANITIZER} -fno-omit-frame-pointer -g -pie")
	endif()

	# Note that for some stupid reason, -no-pie is needed to get the executable to show up as clickable in the Ubuntu files app.
	# See https://askubuntu.com/questions/1056882/i-cannot-run-any-executable-from-nautilus and https://gitlab.gnome.org/GNOME/nautilus/-/issues/1601
	# Note that the server target doesn't include this cmake file, so the -no-pie won't apply to it.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib' -no-pie")
endif()


#if(WIN32)
#	SET(INDIGO_EMBREE_LIB) # On Windows, embree is built as a dll that is loaded at runtime.
#else()
#	SET(INDIGO_EMBREE_LIB embree) # Linux and OSX need to link against the static embree lib.
#endif()


if(WIN32)
	target_link_libraries(${CURRENT_TARGET}
		debug     "${jpegturbodir}-debug/lib/turbojpeg-static.lib"
		optimized "${jpegturbodir}/lib/turbojpeg-static.lib"
	)
else()
	target_link_libraries(${CURRENT_TARGET}
		${jpegturbodir}/lib/libjpeg.a
	)
endif()


target_link_libraries(${CURRENT_TARGET}
indigo_libs
${INDIGO_WIN32_LIBS}
${LINUX_LIBS}
#C:/programming/indigo/output/vs2019/indigo_x64/Debug/indigo_sdk_lib.lib
#C:/programming/indigo/output/vs2019/indigo_x64/RelWithDebInfo/indigo_sdk_lib.lib
)
