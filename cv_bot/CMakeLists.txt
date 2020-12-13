# server

set(CURRENT_TARGET cv_bot)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
include(../cmake/winter.cmake)


FILE(GLOB cv_bot "./*.cpp" "./*.h")
SET(gui_client_files
../gui_client/ClientThread.cpp
../gui_client/ClientThread.h
../gui_client/WorldState.cpp
../gui_client/WorldState.h
)

SET(shared_files
../shared/Avatar.cpp
../shared/Avatar.h
../shared/GroundPatch.cpp
../shared/GroundPatch.h
../shared/Parcel.cpp
../shared/Parcel.h
../shared/ParcelID.h
../shared/Protocol.h
../shared/Resource.cpp
../shared/Resource.h
../shared/ResourceManager.cpp
../shared/ResourceManager.h
../shared/TimeStamp.cpp
../shared/TimeStamp.h
../shared/UID.h
../shared/UserID.h
../shared/WorldObject.cpp
../shared/WorldObject.h
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
)


SOURCE_GROUP(cv_bot FILES ${cv_bot})
SOURCE_GROUP(docs FILES ${docs})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(gui_client_files FILES ${gui_client_files})

if(WIN32)
	SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			/I\"${INDIGO_QT_INCLUDE_DIR}\"")

	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	/I\"${INDIGO_QT_INCLUDE_DIR}\"")

	SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			/I\"${INDIGO_QT_INCLUDE_DIR}\"")

	SET(CMAKE_CXX_FLAGS_SDKDEBUG		"${CMAKE_CXX_FLAGS_SDKDEBUG}		/I\"${INDIGO_QT_INCLUDE_DIR}\"")
elseif(APPLE)
	SET(CMAKE_CXX_FLAGS			"${CMAKE_CXX_FLAGS}			-F\"${INDIGO_QT_LIB_DIR}\"")
else()# linux
	SET(CMAKE_CXX_FLAGS			"${CMAKE_CXX_FLAGS}			-I\"${INDIGO_QT_INCLUDE_DIR}\"")
endif()

#link_directories(
#${INDIGO_QT_LIB_DIR}
#)


add_executable(${CURRENT_TARGET}
${graphics}
${indigo_src}
${indigo_files_in_sdk_lib}
${indigo_console}
${maths}
${networking}
${physics}
${raytracing}
${simpleraytracer}
${sceneparser}
${utils}
${hdr}
${winter}
${cv_bot}
${gui_client_files}
${shared_files}
${scripts}
${double_conversion}
${dll_src}
${fft2d}
${xxhash}
${docs}
${lang}
)


include(../cmake/shared_target_settings.cmake)


if(WIN32)
	#SET(QT_WIN32_LIBS)

	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework #QtOpenGL -framework QtSql -framework OpenGL")
else() # linux
	#SET(QT_LINUX_LIBS dl)

	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -Xlinker -rpath='$ORIGIN/lib'")
endif()


if(WIN32)
#	SET(MYSQL_CONNECTOR_LIB ${MYSQL_CONNECTOR_DIR}/lib64/vs14/$(Configuration)/mysqlcppconn8.lib)
else()
	# TODO
endif()


target_link_libraries(${CURRENT_TARGET}
#${MYSQL_CONNECTOR_LIB}
)
