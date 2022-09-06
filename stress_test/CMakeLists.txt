# stress_test

set(CURRENT_TARGET stress_test)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)


FILE(GLOB stress_test "./*.cpp" "./*.h")

SOURCE_GROUP(stress_test FILES ${stress_test})


SET(shared_files 
../shared/Avatar.cpp
../shared/Avatar.h
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
../shared/Resource.cpp
../shared/Resource.h
../shared/ResourceManager.cpp
../shared/ResourceManager.h
)

SOURCE_GROUP(shared_files FILES ${shared_files})

SET(stress_test_maths
${GLARE_CORE_TRUNK_DIR_ENV}/maths/Matrix4f.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/maths/Vec4f.cpp
)

add_executable(${CURRENT_TARGET}
${networking}
${utils}
${stress_test}
${shared_files}
${double_conversion}
${stress_test_maths}
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
