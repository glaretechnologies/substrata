# gui_client

set(CURRENT_TARGET gui_client)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
include(../cmake/winter.cmake)


FILE(GLOB gui_client "./*.cpp" "./*.h" "./*.ui")


# SET(rc_stuff "../vs2008/indigo_icon.rc")

SOURCE_GROUP(gui_client FILES ${gui_client})


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

link_directories(
${INDIGO_QT_LIB_DIR}
)


add_executable(${CURRENT_TARGET} WIN32
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
${gui_client}
${scripts}
${double_conversion}
${opengl}
${dll_src}
)


include(../cmake/shared_target_settings.cmake)


if(WIN32)
	SET(QT_WIN32_LIBS
		OpenGL32
		optimized qtmain
		optimized Qt5Gui
		optimized Qt5Core
		optimized Qt5Network
		optimized Qt5OpenGL
		optimized Qt5Sql
		optimized Qt5Widgets
		debug qtmaind
		debug Qt5Guid
		debug Qt5Cored
		debug Qt5Networkd
		debug Qt5OpenGLd
		debug Qt5Sqld
		debug Qt5Widgetsd)
		
	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtSql -framework OpenGL")
else() # linux
	SET(QT_LINUX_LIBS GL
		Qt5Gui
		Qt5Core
		Qt5Network
		Qt5OpenGL
		Qt5Sql
		Qt5Widgets)
	
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -Xlinker -rpath='$ORIGIN'")
endif()


target_link_libraries(${CURRENT_TARGET}
${QT_WIN32_LIBS}
${QT_LINUX_LIBS}
)
