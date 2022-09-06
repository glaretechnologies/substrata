# screenshot_bot

set(CURRENT_TARGET screenshot_bot)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)


FILE(GLOB screenshot_bot "./*.cpp" "./*.h")

SOURCE_GROUP(screenshot_bot FILES ${screenshot_bot})

add_executable(${CURRENT_TARGET}
${networking}
${utils}
${screenshot_bot}
${double_conversion}
)

include(../cmake/ssl.cmake)

if(WIN32)
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "")

	# /DEBUG /OPT:REF /OPT:ICF are for writing pdb files that can be used with minidumps.
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /OPT:REF /OPT:ICF /LTCG")

	SET(INDIGO_WIN32_LIBS odbc32
		comctl32
		rpcrt4
		Iphlpapi
		ws2_32 # Winsock
	)
elseif(APPLE)
	# NOTE: -stdlib=libc++ is needed for C++11.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++11 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework OpenCL -framework CoreServices")
else()
	# Linux
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib'")
endif()


target_link_libraries(${CURRENT_TARGET}
libs
${INDIGO_WIN32_LIBS}
${LINUX_LIBS}
)
