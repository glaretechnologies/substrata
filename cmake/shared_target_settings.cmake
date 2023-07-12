# shared settings for console, sdk and gui
include(../cmake/llvm.cmake)
include(../cmake/ssl.cmake)


MESSAGE("Current target is: " ${CURRENT_TARGET})


#add_dependencies(${CURRENT_TARGET} _INDIGO_PREBUILD_TARGET)


if(WIN32)
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "")

	# /DEBUG /OPT:REF /OPT:ICF are for writing pdb files that can be used with minidumps.
	# /LTCG temporarily removed due to problem with inlining: https://developercommunity.visualstudio.com/t/Visual-Studio-not-inlining-small-functio/10412214?q=link+time+code+generation+inline
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /OPT:REF /OPT:ICF")

	target_link_libraries(${CURRENT_TARGET}
		libs
		odbc32
		comctl32
		rpcrt4
		Iphlpapi  # For GetAdaptersInfo() in SystemInfo::getMACAddresses().
		ws2_32 # Winsock
		delayimp # for delay loading via command line, as 2008 generator doesn't seem to support the VS setting for it
		
		debug     "${jpegturbodir}-debug/lib/turbojpeg-static.lib"
		optimized "${jpegturbodir}/lib/turbojpeg-static.lib"
	)
	
elseif(APPLE)
	
	# NOTE: -stdlib=libc++ is needed for C++11 etc.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++17 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework CoreServices")
	
	target_link_libraries(${CURRENT_TARGET}
		libs
		${jpegturbodir}/lib/libjpeg.a
	)
	
else()
	
	# Linux
	# Add required Sanitizer link flags
	if(NOT USE_SANITIZER STREQUAL "")
		SET(SANITIZER_LINK_FLAGS "-fsanitize=${USE_SANITIZER} -fno-omit-frame-pointer -g") # -pie
	endif()

	# Note that for some stupid reason, -no-pie is needed to get the executable to show up as clickable in the Ubuntu files app.
	# See https://askubuntu.com/questions/1056882/i-cannot-run-any-executable-from-nautilus and https://gitlab.gnome.org/GNOME/nautilus/-/issues/1601
	# Note that the server target doesn't include this cmake file, so the -no-pie won't apply to it.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "-std=c++17 ${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib' -no-pie")
	
	target_link_libraries(${CURRENT_TARGET}
		libs
		${jpegturbodir}/lib/libjpeg.a
	)

endif()
