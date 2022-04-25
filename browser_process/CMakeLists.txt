# browser_process

set(CURRENT_TARGET "gui_client Helper${browser_process_suffix}")

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)


set(browser_process BrowserProcess.cpp)

SOURCE_GROUP(browser_process FILES ${browser_process})



#include(../cmake/ssl.cmake)

include_directories(${cef_binary_dist_dir}/include)
include_directories(${cef_binary_dist_dir})

if(WIN32)
	add_executable(${CURRENT_TARGET} WIN32 # WIN32 to make it use Windows subsystem
		${browser_process}
	)
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "")

	# /DEBUG /OPT:REF /OPT:ICF are for writing pdb files that can be used with minidumps.
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /OPT:REF /OPT:ICF /LTCG")

	SET(INDIGO_WIN32_LIBS
		debug 		${cef_binary_dist_dir}/Debug/libcef.lib
		optimized 	${cef_binary_dist_dir}/Release/libcef.lib
		
		debug 		${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/Debug/libcef_dll_wrapper.lib
		optimized 	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib
	)
elseif(APPLE)
	# NOTE: -stdlib=libc++ is needed for C++11.
	
	set(OSX_LIBS
		# Chromium embedded framework
		#debug 		${cef_binary_dist_dir}/Debug/libcef.lib
		#optimized 	${cef_binary_dist_dir}/Release/libcef.lib
		
		debug     	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/libcef_dll_wrapper.a
		optimized 	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/libcef_dll_wrapper.a
		
		debug 		${cef_binary_dist_dir}/Debug/cef_sandbox.a
		optimized	${cef_binary_dist_dir}/Release/cef_sandbox.a
	)
	
#	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++14 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework OpenCL -framework CoreServices")


	# CEF requires we build app bundles for these helpers.
	add_executable(${CURRENT_TARGET} MACOSX_BUNDLE ${browser_process})
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES
		LINK_FLAGS "-std=c++14 -stdlib=libc++"
		BUNDLE True
		MACOSX_BUNDLE_GUI_INDENTIFIER com.glaretechnologies.browser_process
		MACOSX_BUNDLE_BUNDLE_NAME ${CURRENT_TARGET}
		MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/scripts/osx/browser_process.plist.in"
	)
		
else()
	# Linux
#	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib'")
endif()

target_sources(${CURRENT_TARGET} PRIVATE manifest.manifest)

target_link_libraries(${CURRENT_TARGET}
#indigo_libs
${INDIGO_WIN32_LIBS}
${OSX_LIBS}
${LINUX_LIBS}
)
