# server

set(CURRENT_TARGET server)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
include(../cmake/winter.cmake)


FILE(GLOB server "./*.cpp" "./*.h")
FILE(GLOB webserver "../webserver/*.cpp" "../webserver/*.h")
FILE(GLOB docs "../docs/*.txt")
SET(shared_files
../shared/Avatar.cpp
../shared/Avatar.h
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

set(WEBSITE_CORE_DIR "$ENV{WEBSITE_CORE_TRUNK_DIR}")

########### Website Core ################
SET(website_core_files
${WEBSITE_CORE_DIR}/Escaping.cpp
${WEBSITE_CORE_DIR}/Escaping.h
${WEBSITE_CORE_DIR}/WebListenerThread.cpp
${WEBSITE_CORE_DIR}/WebListenerThread.h
${WEBSITE_CORE_DIR}/RequestInfo.cpp
${WEBSITE_CORE_DIR}/RequestInfo.h
${WEBSITE_CORE_DIR}/ResponseUtils.cpp
${WEBSITE_CORE_DIR}/ResponseUtils.h
${WEBSITE_CORE_DIR}/Log.cpp
${WEBSITE_CORE_DIR}/Log.h
${WEBSITE_CORE_DIR}/StressTest.cpp
${WEBSITE_CORE_DIR}/StressTest.h
${WEBSITE_CORE_DIR}/TimeStamp.cpp
${WEBSITE_CORE_DIR}/TimeStamp.h
${WEBSITE_CORE_DIR}/WebsiteExcep.h
${WEBSITE_CORE_DIR}/WorkerThread.cpp
${WEBSITE_CORE_DIR}/WorkerThread.h
${WEBSITE_CORE_DIR}/RequestHandler.cpp
${WEBSITE_CORE_DIR}/RequestHandler.h
${WEBSITE_CORE_DIR}/UnsafeString.cpp
${WEBSITE_CORE_DIR}/UnsafeString.h
${WEBSITE_CORE_DIR}/WorkerThreadTests.cpp
${WEBSITE_CORE_DIR}/WorkerThreadTests.h
#${WEBSITE_CORE_DIR}/StaticAssetManager.cpp
#${WEBSITE_CORE_DIR}/StaticAssetManager.h
)


include_directories(
${WEBSITE_CORE_DIR}
)

SOURCE_GROUP(server FILES ${server})
SOURCE_GROUP(webserver FILES ${webserver})
SOURCE_GROUP(website_core FILES ${website_core_files})
SOURCE_GROUP(docs FILES ${docs})
SOURCE_GROUP(shared_files FILES ${shared_files})

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
${indigo_console}
${indigo_files_in_sdk_lib}
${maths}
${networking}
${physics}
${raytracing}
${simpleraytracer}
${sceneparser}
${utils}
${hdr}
${winter}
${server}
${webserver}
${website_core_files}
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
	SET(QT_WIN32_LIBS)

	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtSql -framework OpenGL")
else() # linux
	SET(QT_LINUX_LIBS dl)

	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -Xlinker -rpath='$ORIGIN/lib'")
endif()


if(WIN32)
#	SET(MYSQL_CONNECTOR_LIB ${MYSQL_CONNECTOR_DIR}/lib64/vs14/$(Configuration)/mysqlcppconn8.lib)
else()
	# TODO
endif()


target_link_libraries(${CURRENT_TARGET}
${QT_WIN32_LIBS}
${QT_LINUX_LIBS}
#${MYSQL_CONNECTOR_LIB}
)
