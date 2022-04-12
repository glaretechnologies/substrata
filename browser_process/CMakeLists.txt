# browser_process

set(CURRENT_TARGET browser_process)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)


FILE(GLOB browser_process "./*.cpp" "./*.h")

SOURCE_GROUP(browser_process FILES ${browser_process})

add_executable(${CURRENT_TARGET}
${browser_process}
)

#include(../cmake/ssl.cmake)

include_directories(${cef_binary_dist_dir}/include)
include_directories(${cef_binary_dist_dir})

if(WIN32)
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
#	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++11 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework OpenCL -framework CoreServices")
else()
	# Linux
#	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib'")
endif()

target_sources(${CURRENT_TARGET} PRIVATE manifest.manifest)

target_link_libraries(${CURRENT_TARGET}
#indigo_libs
${INDIGO_WIN32_LIBS}
${LINUX_LIBS}
)
