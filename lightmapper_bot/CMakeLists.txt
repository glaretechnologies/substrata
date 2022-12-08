# server

set(CURRENT_TARGET lightmapper_bot)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
include(../cmake/winter.cmake)


FILE(GLOB lightmapper_bot "./*.cpp" "./*.h")
SET(gui_client_files
../gui_client/ClientThread.cpp
../gui_client/ClientThread.h
../gui_client/ClientSenderThread.cpp
../gui_client/ClientSenderThread.h
../gui_client/WorldState.cpp
../gui_client/WorldState.h
../gui_client/URLWhitelist.cpp
../gui_client/URLWhitelist.h
../gui_client/DownloadResourcesThread.cpp
../gui_client/DownloadResourcesThread.h
../gui_client/NetDownloadResourcesThread.cpp
../gui_client/NetDownloadResourcesThread.h
../gui_client/UploadResourceThread.cpp
../gui_client/UploadResourceThread.h
../gui_client/IndigoConversion.cpp
../gui_client/IndigoConversion.h
../gui_client/DownloadingResourceQueue.cpp
../gui_client/DownloadingResourceQueue.h
#../gui_client/ModelLoading.cpp
#../gui_client/ModelLoading.h
)

SET(shared_files
../shared/Avatar.cpp
../shared/Avatar.h
../shared/ImageDecoding.cpp
../shared/ImageDecoding.h
../shared/GroundPatch.cpp
../shared/GroundPatch.h
../shared/LODGeneration.cpp
../shared/LODGeneration.h
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
../shared/VoxelMeshBuilding.cpp
../shared/VoxelMeshBuilding.h
)



include_directories(${basisu_dir})

add_definitions(-DBASISU_SUPPORT_SSE=1)
# Disable some basis universal stuff we don't use
# add_definitions(-DBASISD_SUPPORT_UASTC=0) # This would be nice but it doesn't compile with it defined.
# add_definitions(-DBASISD_SUPPORT_BC7_MODE5=0)
# add_definitions(-DBASISD_SUPPORT_PVRTC1=0)
# add_definitions(-DBASISD_SUPPORT_ETC2_EAC_A8=0)
# add_definitions(-DBASISD_SUPPORT_UASTC=0)
# add_definitions(-DBASISD_SUPPORT_ASTC=0)

add_definitions(-DBASIS_NO_IMAGE_READERS=1)




SOURCE_GROUP(lightmapper_bot FILES ${lightmapper_bot})
SOURCE_GROUP(docs FILES ${docs})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(gui_client_files FILES ${gui_client_files})

if(WIN32)
	SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			/I\"${INDIGO_QT_INCLUDE_DIR}\"")

	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	/I\"${INDIGO_QT_INCLUDE_DIR}\"")

	SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			/I\"${INDIGO_QT_INCLUDE_DIR}\"")
elseif(APPLE)
	SET(CMAKE_CXX_FLAGS			"${CMAKE_CXX_FLAGS}			-F\"${INDIGO_QT_LIB_DIR}\"")
else()# linux
	SET(CMAKE_CXX_FLAGS			"${CMAKE_CXX_FLAGS}			-I\"${INDIGO_QT_INCLUDE_DIR}\"")
endif()

#link_directories(
#${INDIGO_QT_LIB_DIR}
#)

checkAndGetEnvVar(INDIGO_TRUNK_DIR INDIGO_TRUNK_DIR_ENV)
MESSAGE("INDIGO_TRUNK_DIR: ${INDIGO_TRUNK_DIR_ENV}")

include_directories(${INDIGO_TRUNK_DIR_ENV})


add_executable(${CURRENT_TARGET}
${graphics}
${indigo_src}
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
${lightmapper_bot}
${gui_client_files}
${shared_files}
${scripts}
${double_conversion}
${dll_src}
${fft2d}
${xxhash}
${docs}
${lang}
${meshoptimizer}
${basisu_files}
)


include(../cmake/shared_target_settings.cmake)



add_definitions(-DINDIGO_SUPPORT) 


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


# Use $ENV{INDIGO_OUTPUT}?

target_link_libraries(${CURRENT_TARGET}
#${MYSQL_CONNECTOR_LIB}
debug 		C:/programming/indigo/output/vs2022/indigo_x64/Debug/indigo_sdk_lib.lib
optimized 	C:/programming/indigo/output/vs2022/indigo_x64/RelWithDebInfo/indigo_sdk_lib.lib
)
