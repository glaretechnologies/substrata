# gui_client

set(CURRENT_TARGET gui_client)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
include(../cmake/winter.cmake)

# Rules to build moc_xx.cpp files, from xx.h files, using the program 'moc'.
addMocFileRule(gui_client MainWindow)
addMocFileRule(gui_client GuiClientApplication)
addMocFileRule(gui_client GlWidget)
addMocFileRule(gui_client AvatarSettingsDialog)
addMocFileRule(gui_client AvatarPreviewWidget)
addMocFileRule(gui_client AddObjectDialog)
addMocFileRule(gui_client AddObjectPreviewWidget)
addMocFileRule(gui_client ObjectEditor)
addMocFileRule(gui_client MaterialEditor)

addMocFileRule(qt FileSelectWidget)
addMocFileRule(qt RealControl)
addMocFileRule(qt IndigoDoubleSpinBox)
#addMocFileRule(qt RgbColourPicker)

# Rules to compile the .ui files into ui_xx.h, using 'uic'.
addUIFileRule(gui_client AddObjectDialog)
addUIFileRule(gui_client AvatarSettingsDialog)
addUIFileRule(gui_client MainWindow)
addUIFileRule(gui_client ObjectEditor)
addUIFileRule(gui_client MaterialEditor)

addUIFileRule(qt FileSelectWidget)
addUIFileRule(qt RealControl)
#addUIFileRule(qt RgbColourPicker)


FILE(GLOB gui_client "./*.cpp" "./*.h" "./*.ui")

SET(gui_client ${gui_client} 
${CMAKE_SOURCE_DIR}/gui_client/moc_MainWindow.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_GuiClientApplication.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_GlWidget.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_AvatarSettingsDialog.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_AvatarPreviewWidget.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_AddObjectDialog.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_AddObjectPreviewWidget.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_ObjectEditor.cpp
${CMAKE_SOURCE_DIR}/gui_client/moc_MaterialEditor.cpp
${CMAKE_SOURCE_DIR}/gui_client/ui_AddObjectDialog.h
${CMAKE_SOURCE_DIR}/gui_client/ui_AvatarSettingsDialog.h
${CMAKE_SOURCE_DIR}/gui_client/ui_ObjectEditor.h
${CMAKE_SOURCE_DIR}/gui_client/ui_MaterialEditor.h
${CMAKE_SOURCE_DIR}/gui_client/ui_MainWindow.h
)

SET(qt 
../qt/QtUtils.cpp
../qt/QtUtils.h
../qt/SignalBlocker.cpp
../qt/SignalBlocker.h

../qt/FileSelectWidget.cpp
../qt/FileSelectWidget.h
../qt/moc_FileSelectWidget.cpp
../qt/ui_FileSelectWidget.h

../qt/RealControl.cpp
../qt/RealControl.h
../qt/moc_RealControl.cpp
../qt/ui_RealControl.h

../qt/IndigoDoubleSpinBox.cpp
../qt/IndigoDoubleSpinBox.h
../qt/moc_IndigoDoubleSpinBox.cpp

#../qt/RgbColourPicker.cpp
#../qt/RgbColourPicker.h
#../qt/moc_RgbColourPicker.cpp
#../qt/ui_RgbColourPicker.h
)

SET(shared_files 
../shared/Avatar.cpp
../shared/Avatar.h
../shared/ResourceManager.cpp
../shared/ResourceManager.h
../shared/WorldState.cpp
../shared/WorldState.h
../shared/WorldObject.cpp
../shared/WorldObject.h
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
)

SET(client_indigo_files 
${INDIGO_TRUNK_DIR_ENV}/indigo/TextureServer.cpp
${INDIGO_TRUNK_DIR_ENV}/indigo/TextureServer.h
)


# SET(rc_stuff "../vs2008/indigo_icon.rc")

SOURCE_GROUP(gui_client FILES ${gui_client})
SOURCE_GROUP(qt FILES ${qt})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(client_indigo_files FILES ${client_indigo_files})


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
${client_indigo_files}
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
${shared_files}
${scripts}
${double_conversion}
${opengl}
${opengl_shaders}
${dll_src}
${fft2d}
${xxhash}
${qt}
)


include(../cmake/shared_target_settings.cmake)


if(WIN32)
	SET(QT_WIN32_LIBS
		OpenGL32
		optimized qtmain
		optimized Qt5Gui
		optimized Qt5Core
		optimized Qt5OpenGL
		optimized Qt5Widgets
		debug qtmaind
		debug Qt5Guid
		debug Qt5Cored
		debug Qt5OpenGLd
		debug Qt5Widgetsd
		)
		
	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	SET(EXECUTABLE_TYPE MACOSX_BUNDLE)
	
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/osx/CyberspaceInfo.plist.in")
	

	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtSql -framework OpenGL")
else() # linux
	SET(QT_LINUX_LIBS GL
		Qt5Gui
		Qt5Core
		Qt5OpenGL
		Qt5Widgets
	)
	
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -Xlinker -rpath='$ORIGIN'")
endif()


target_link_libraries(${CURRENT_TARGET}
${QT_WIN32_LIBS}
${QT_LINUX_LIBS}
)

