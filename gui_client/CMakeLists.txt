# gui_client

set(CURRENT_TARGET gui_client)

if(NOT USE_SDL)
	programAvailable(${INDIGO_QT_DIR}/bin/moc)
endif()

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)

if(NOT EMSCRIPTEN)
	include(../cmake/winter.cmake)
endif()

add_definitions(${WINTER_PREPROCESSOR_DEFINITIONS}) # WINTER_PREPROCESSOR_DEFINITIONS is returned from embed_winter.cmake via winter.cmake.


# Add graphics files used only in GUI client
set(gui_client_graphics
	"${GLARE_CORE_TRUNK_DIR_ENV}/graphics/TextRenderer.cpp"
	"${GLARE_CORE_TRUNK_DIR_ENV}/graphics/TextRenderer.h"
)

SOURCE_GROUP(gui_client_graphics FILES ${gui_client_graphics})


#============== Jolt physics ==============

add_definitions(-DUSE_WASM_SIMD=1)

if(NOT EXISTS "${GLARE_CORE_LIBS_ENV}/jolt/5.3.0")
	message(FATAL_ERROR "Jolt files not found, please run scripts/get_libs.rb to download them.")
endif()

set(PHYSICS_REPO_ROOT "${GLARE_CORE_LIBS_ENV}/jolt/5.3.0")
#set(PHYSICS_REPO_ROOT "${GLARE_CORE_LIBS_ENV}/JoltPhysics")
include(${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake)

include_directories(${PHYSICS_REPO_ROOT})

add_definitions(-DUSE_JOLT=1)


#============== Tracy profiler ==============

include_directories("${GLARE_CORE_TRUNK_DIR_ENV}/tracy/public")
set(tracy_files "${GLARE_CORE_TRUNK_DIR_ENV}/tracy/public/TracyClient.cpp")


#============== SDL / Qt ==============

if(USE_SDL)
	if(NOT EXISTS "${SDL_BUILD_DIR}/include/SDL2/SDL.h")
		MESSAGE(FATAL_ERROR "Could not find SDL.h from your SDL2 build; looked at '${SDL_BUILD_DIR}/include/SDL2/SDL.h'. Please specify the SDL2 build directory with -DSDL_BUILD_DIR=XXX")
	endif()
endif()

if(NOT USE_SDL)
	# Rules to build moc_xx.cpp files, from xx.h files, using the program 'moc'.
	addMocFileRule(gui_client MainWindow)
	addMocFileRule(gui_client GuiClientApplication)
	addMocFileRule(gui_client GlWidget)
	addMocFileRule(gui_client AvatarSettingsDialog)
	addMocFileRule(gui_client AvatarPreviewWidget)
	addMocFileRule(gui_client AddObjectDialog)
	addMocFileRule(gui_client AddVideoDialog)
	addMocFileRule(gui_client AddObjectPreviewWidget)
	addMocFileRule(gui_client ObjectEditor)
	addMocFileRule(gui_client EnvironmentOptionsWidget)
	addMocFileRule(gui_client ParcelEditor)
	addMocFileRule(gui_client LoginDialog)
	addMocFileRule(gui_client SignUpDialog)
	addMocFileRule(gui_client MaterialEditor)
	addMocFileRule(gui_client UserDetailsWidget)
	addMocFileRule(gui_client URLWidget)
	addMocFileRule(gui_client ResetPasswordDialog)
	addMocFileRule(gui_client ChangePasswordDialog)
	addMocFileRule(gui_client MaterialBrowser)
	addMocFileRule(gui_client IndigoView)
	addMocFileRule(gui_client ShaderEditorDialog)
	addMocFileRule(gui_client AboutDialog)
	addMocFileRule(gui_client MainOptionsDialog)
	addMocFileRule(gui_client MainOptionsDialog)
	addMocFileRule(gui_client GoToParcelDialog)
	addMocFileRule(gui_client GoToPositionDialog)
	addMocFileRule(gui_client FindObjectDialog)
	addMocFileRule(gui_client ListObjectsNearbyDialog)
	addMocFileRule(gui_client LogWindow)
	addMocFileRule(gui_client DiagnosticsWidget)
	addMocFileRule(gui_client TerrainSpecSectionWidget)
	addMocFileRule(gui_client WorldSettingsWidget)
	addMocFileRule(gui_client CreateObjectsDialog)

	addMocFileRule(qt FileSelectWidget)
	addMocFileRule(qt RealControl)
	addMocFileRule(qt IndigoDoubleSpinBox)
	#addMocFileRule(qt RgbColourPicker)

	# Rules to compile the .ui files into ui_xx.h, using 'uic'.
	addUIFileRule(gui_client AddObjectDialog)
	addUIFileRule(gui_client AddVideoDialog)
	addUIFileRule(gui_client AvatarSettingsDialog)
	addUIFileRule(gui_client MainWindow)
	addUIFileRule(gui_client ObjectEditor)
	addUIFileRule(gui_client EnvironmentOptionsWidget)
	addUIFileRule(gui_client ParcelEditor)
	addUIFileRule(gui_client LoginDialog)
	addUIFileRule(gui_client SignUpDialog)
	addUIFileRule(gui_client MaterialEditor)
	addUIFileRule(gui_client UserDetailsWidget)
	addUIFileRule(gui_client URLWidget)
	addUIFileRule(gui_client ResetPasswordDialog)
	addUIFileRule(gui_client ChangePasswordDialog)
	addUIFileRule(gui_client MaterialBrowser)
	addUIFileRule(gui_client ShaderEditorDialog)
	addUIFileRule(gui_client AboutDialog)
	addUIFileRule(gui_client MainOptionsDialog)
	addUIFileRule(gui_client GoToParcelDialog)
	addUIFileRule(gui_client GoToPositionDialog)
	addUIFileRule(gui_client FindObjectDialog)
	addUIFileRule(gui_client ListObjectsNearbyDialog)
	addUIFileRule(gui_client LogWindow)
	addUIFileRule(gui_client DiagnosticsWidget)
	addUIFileRule(gui_client TerrainSpecSectionWidget)
	addUIFileRule(gui_client WorldSettingsWidget)
	addUIFileRule(gui_client CreateObjectsDialog)

	addUIFileRule(qt FileSelectWidget)
	addUIFileRule(qt RealControl)
	#addUIFileRule(qt RgbColourPicker)
endif()



SET(gui_client
${CMAKE_SOURCE_DIR}/gui_client/AnimatedTextureManager.cpp
${CMAKE_SOURCE_DIR}/gui_client/AnimatedTextureManager.h
${CMAKE_SOURCE_DIR}/gui_client/AvatarGraphics.cpp
${CMAKE_SOURCE_DIR}/gui_client/AvatarGraphics.h
${CMAKE_SOURCE_DIR}/gui_client/BikePhysics.cpp
${CMAKE_SOURCE_DIR}/gui_client/BikePhysics.h
${CMAKE_SOURCE_DIR}/gui_client/BiomeManager.cpp
${CMAKE_SOURCE_DIR}/gui_client/BiomeManager.h
${CMAKE_SOURCE_DIR}/gui_client/BoatPhysics.cpp
${CMAKE_SOURCE_DIR}/gui_client/BoatPhysics.h
${CMAKE_SOURCE_DIR}/gui_client/BrowserVidPlayer.cpp
${CMAKE_SOURCE_DIR}/gui_client/BrowserVidPlayer.h
${CMAKE_SOURCE_DIR}/gui_client/BuildScatteringInfoTask.cpp
${CMAKE_SOURCE_DIR}/gui_client/BuildScatteringInfoTask.h
${CMAKE_SOURCE_DIR}/gui_client/CameraController.cpp
${CMAKE_SOURCE_DIR}/gui_client/CameraController.h
${CMAKE_SOURCE_DIR}/gui_client/CarPhysics.cpp
${CMAKE_SOURCE_DIR}/gui_client/CarPhysics.h
${CMAKE_SOURCE_DIR}/gui_client/CEF.cpp
${CMAKE_SOURCE_DIR}/gui_client/CEF.h
${CMAKE_SOURCE_DIR}/gui_client/CEFInternal.cpp
${CMAKE_SOURCE_DIR}/gui_client/CEFInternal.h
${CMAKE_SOURCE_DIR}/gui_client/ClientSenderThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/ClientSenderThread.h
${CMAKE_SOURCE_DIR}/gui_client/ClientThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/ClientThread.h
${CMAKE_SOURCE_DIR}/gui_client/ClientUDPHandlerThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/ClientUDPHandlerThread.h
${CMAKE_SOURCE_DIR}/gui_client/CMakeLists.txt
${CMAKE_SOURCE_DIR}/gui_client/CredentialManager.cpp
${CMAKE_SOURCE_DIR}/gui_client/CredentialManager.h
${CMAKE_SOURCE_DIR}/gui_client/DownloadingResourceQueue.cpp
${CMAKE_SOURCE_DIR}/gui_client/DownloadingResourceQueue.h
${CMAKE_SOURCE_DIR}/gui_client/DownloadResourcesThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/DownloadResourcesThread.h
${CMAKE_SOURCE_DIR}/gui_client/EmbeddedBrowser.cpp
${CMAKE_SOURCE_DIR}/gui_client/EmbeddedBrowser.h
${CMAKE_SOURCE_DIR}/gui_client/GestureUI.cpp
${CMAKE_SOURCE_DIR}/gui_client/GestureUI.h
${CMAKE_SOURCE_DIR}/gui_client/GUIClient.cpp
${CMAKE_SOURCE_DIR}/gui_client/GUIClient.h
${CMAKE_SOURCE_DIR}/gui_client/HashedObGrid.h
${CMAKE_SOURCE_DIR}/gui_client/HeadUpDisplayUI.cpp
${CMAKE_SOURCE_DIR}/gui_client/HeadUpDisplayUI.h
${CMAKE_SOURCE_DIR}/gui_client/HoverCarPhysics.cpp
${CMAKE_SOURCE_DIR}/gui_client/HoverCarPhysics.h
${CMAKE_SOURCE_DIR}/gui_client/Imposter.cpp
${CMAKE_SOURCE_DIR}/gui_client/Imposter.h
${CMAKE_SOURCE_DIR}/gui_client/IndigoConversion.cpp
${CMAKE_SOURCE_DIR}/gui_client/IndigoConversion.h
${CMAKE_SOURCE_DIR}/gui_client/JoltUtils.h
${CMAKE_SOURCE_DIR}/gui_client/LoadAudioTask.cpp
${CMAKE_SOURCE_DIR}/gui_client/LoadAudioTask.h
${CMAKE_SOURCE_DIR}/gui_client/LoadItemQueue.cpp
${CMAKE_SOURCE_DIR}/gui_client/LoadItemQueue.h
${CMAKE_SOURCE_DIR}/gui_client/LoadModelTask.cpp
${CMAKE_SOURCE_DIR}/gui_client/LoadModelTask.h
${CMAKE_SOURCE_DIR}/gui_client/LoadScriptTask.cpp
${CMAKE_SOURCE_DIR}/gui_client/LoadScriptTask.h
${CMAKE_SOURCE_DIR}/gui_client/LoadTextureTask.cpp
${CMAKE_SOURCE_DIR}/gui_client/LoadTextureTask.h
${CMAKE_SOURCE_DIR}/gui_client/MakeHypercardTextureTask.cpp
${CMAKE_SOURCE_DIR}/gui_client/MakeHypercardTextureTask.h
${CMAKE_SOURCE_DIR}/gui_client/MeshBuilding.cpp
${CMAKE_SOURCE_DIR}/gui_client/MeshBuilding.h
${CMAKE_SOURCE_DIR}/gui_client/MeshManager.cpp
${CMAKE_SOURCE_DIR}/gui_client/MeshManager.h
${CMAKE_SOURCE_DIR}/gui_client/MiniMap.cpp
${CMAKE_SOURCE_DIR}/gui_client/MiniMap.h
${CMAKE_SOURCE_DIR}/gui_client/MiscInfoUI.cpp
${CMAKE_SOURCE_DIR}/gui_client/MiscInfoUI.h
${CMAKE_SOURCE_DIR}/gui_client/ModelLoading.cpp
${CMAKE_SOURCE_DIR}/gui_client/ModelLoading.h
${CMAKE_SOURCE_DIR}/gui_client/NetDownloadResourcesThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/NetDownloadResourcesThread.h
${CMAKE_SOURCE_DIR}/gui_client/ObInfoUI.cpp
${CMAKE_SOURCE_DIR}/gui_client/ObInfoUI.h
${CMAKE_SOURCE_DIR}/gui_client/ObjectPathController.cpp
${CMAKE_SOURCE_DIR}/gui_client/ObjectPathController.h
${CMAKE_SOURCE_DIR}/gui_client/ParticleManager.cpp
${CMAKE_SOURCE_DIR}/gui_client/ParticleManager.h
${CMAKE_SOURCE_DIR}/gui_client/PhysicsObject.cpp
${CMAKE_SOURCE_DIR}/gui_client/PhysicsObject.h
${CMAKE_SOURCE_DIR}/gui_client/PhysicsWorld.cpp
${CMAKE_SOURCE_DIR}/gui_client/PhysicsWorld.h
${CMAKE_SOURCE_DIR}/gui_client/PlayerPhysics.cpp
${CMAKE_SOURCE_DIR}/gui_client/PlayerPhysics.h
${CMAKE_SOURCE_DIR}/gui_client/PlayerPhysicsInput.h
${CMAKE_SOURCE_DIR}/gui_client/ProximityLoader.cpp
${CMAKE_SOURCE_DIR}/gui_client/ProximityLoader.h
${CMAKE_SOURCE_DIR}/gui_client/ResourceProcessing.cpp
${CMAKE_SOURCE_DIR}/gui_client/ResourceProcessing.h
${CMAKE_SOURCE_DIR}/gui_client/SaveResourcesDBThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/SaveResourcesDBThread.h
${CMAKE_SOURCE_DIR}/gui_client/Scripting.cpp
${CMAKE_SOURCE_DIR}/gui_client/Scripting.h
${CMAKE_SOURCE_DIR}/gui_client/TerrainDecalManager.cpp
${CMAKE_SOURCE_DIR}/gui_client/TerrainDecalManager.h
${CMAKE_SOURCE_DIR}/gui_client/TerrainScattering.cpp
${CMAKE_SOURCE_DIR}/gui_client/TerrainScattering.h
${CMAKE_SOURCE_DIR}/gui_client/TerrainSystem.cpp
${CMAKE_SOURCE_DIR}/gui_client/TerrainSystem.h
${CMAKE_SOURCE_DIR}/gui_client/TerrainTests.cpp
${CMAKE_SOURCE_DIR}/gui_client/TerrainTests.h
${CMAKE_SOURCE_DIR}/gui_client/TestSuite.cpp
${CMAKE_SOURCE_DIR}/gui_client/TestSuite.h
${CMAKE_SOURCE_DIR}/gui_client/ThreadMessages.h
${CMAKE_SOURCE_DIR}/gui_client/UIInterface.h
${CMAKE_SOURCE_DIR}/gui_client/UndoBuffer.cpp
${CMAKE_SOURCE_DIR}/gui_client/UndoBuffer.h
${CMAKE_SOURCE_DIR}/gui_client/UploadResourceThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/UploadResourceThread.h
${CMAKE_SOURCE_DIR}/gui_client/URLParser.cpp
${CMAKE_SOURCE_DIR}/gui_client/URLParser.h
${CMAKE_SOURCE_DIR}/gui_client/URLWhitelist.cpp
${CMAKE_SOURCE_DIR}/gui_client/URLWhitelist.h
${CMAKE_SOURCE_DIR}/gui_client/VehiclePhysics.h
${CMAKE_SOURCE_DIR}/gui_client/WebViewData.cpp
${CMAKE_SOURCE_DIR}/gui_client/WebViewData.h
${CMAKE_SOURCE_DIR}/gui_client/WinterShaderEvaluator.cpp
${CMAKE_SOURCE_DIR}/gui_client/WinterShaderEvaluator.h
${CMAKE_SOURCE_DIR}/gui_client/WorldState.cpp
${CMAKE_SOURCE_DIR}/gui_client/WorldState.h
${CMAKE_SOURCE_DIR}/gui_client/EmscriptenResourceDownloader.cpp
${CMAKE_SOURCE_DIR}/gui_client/EmscriptenResourceDownloader.h
${CMAKE_SOURCE_DIR}/gui_client/ChatUI.cpp
${CMAKE_SOURCE_DIR}/gui_client/ChatUI.h
${CMAKE_SOURCE_DIR}/gui_client/ScriptedObjectProximityChecker.cpp
${CMAKE_SOURCE_DIR}/gui_client/ScriptedObjectProximityChecker.h
${CMAKE_SOURCE_DIR}/gui_client/PhotoModeUI.cpp
${CMAKE_SOURCE_DIR}/gui_client/PhotoModeUI.h
${CMAKE_SOURCE_DIR}/gui_client/GarbageDeleterThread.cpp
${CMAKE_SOURCE_DIR}/gui_client/GarbageDeleterThread.h
)


if(USE_SDL)
	SET(gui_client ${gui_client}
		${CMAKE_SOURCE_DIR}/gui_client/SDLClient.cpp
		${CMAKE_SOURCE_DIR}/gui_client/SDLUIInterface.cpp
		${CMAKE_SOURCE_DIR}/gui_client/SDLUIInterface.h
	)
else()
	SET(gui_client ${gui_client}
		${CMAKE_SOURCE_DIR}/gui_client/IndigoView.cpp
		${CMAKE_SOURCE_DIR}/gui_client/IndigoView.h
		${CMAKE_SOURCE_DIR}/gui_client/AboutDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/AboutDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/AboutDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/AddObjectDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/AddObjectDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/AddObjectDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/AddObjectPreviewWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/AddObjectPreviewWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/AddVideoDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/AddVideoDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/AddVideoDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/AvatarPreviewWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/AvatarPreviewWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/AvatarSettingsDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/AvatarSettingsDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/AvatarSettingsDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/ChangePasswordDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ChangePasswordDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/ChangePasswordDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/DiagnosticsWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/DiagnosticsWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/DiagnosticsWidget.ui
		${CMAKE_SOURCE_DIR}/gui_client/EnvironmentOptionsWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/EnvironmentOptionsWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/EnvironmentOptionsWidget.ui
		${CMAKE_SOURCE_DIR}/gui_client/FindObjectDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/FindObjectDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/FindObjectDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/GlWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/GlWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/GoToParcelDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/GoToParcelDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/GoToParcelDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/GoToPositionDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/GoToPositionDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/GoToPositionDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/GuiClientApplication.cpp
		${CMAKE_SOURCE_DIR}/gui_client/GuiClientApplication.h
		${CMAKE_SOURCE_DIR}/gui_client/ISLSyntaxHighlighter.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ISLSyntaxHighlighter.h
		${CMAKE_SOURCE_DIR}/gui_client/ListObjectsNearbyDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ListObjectsNearbyDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/ListObjectsNearbyDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/LoginDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/LoginDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/LoginDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/LogWindow.cpp
		${CMAKE_SOURCE_DIR}/gui_client/LogWindow.h
		${CMAKE_SOURCE_DIR}/gui_client/LogWindow.ui
		${CMAKE_SOURCE_DIR}/gui_client/MainOptionsDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/MainOptionsDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/MainOptionsDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/MainWindow.cpp
		${CMAKE_SOURCE_DIR}/gui_client/MainWindow.h
		${CMAKE_SOURCE_DIR}/gui_client/MainWindow.ui
		${CMAKE_SOURCE_DIR}/gui_client/MainWindowTestCode.txt
		${CMAKE_SOURCE_DIR}/gui_client/MaterialBrowser.cpp
		${CMAKE_SOURCE_DIR}/gui_client/MaterialBrowser.h
		${CMAKE_SOURCE_DIR}/gui_client/MaterialBrowser.ui
		${CMAKE_SOURCE_DIR}/gui_client/MaterialEditor.cpp
		${CMAKE_SOURCE_DIR}/gui_client/MaterialEditor.h
		${CMAKE_SOURCE_DIR}/gui_client/MaterialEditor.ui
		${CMAKE_SOURCE_DIR}/gui_client/ObjectEditor.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ObjectEditor.h
		${CMAKE_SOURCE_DIR}/gui_client/ObjectEditor.ui
		${CMAKE_SOURCE_DIR}/gui_client/ParcelEditor.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ParcelEditor.h
		${CMAKE_SOURCE_DIR}/gui_client/ParcelEditor.ui
		${CMAKE_SOURCE_DIR}/gui_client/TerrainSpecSectionWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/TerrainSpecSectionWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/TerrainSpecSectionWidget.ui
		${CMAKE_SOURCE_DIR}/gui_client/ShaderEditorDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ShaderEditorDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/ShaderEditorDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/ResetPasswordDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ResetPasswordDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/ResetPasswordDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/SignUpDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/SignUpDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/SignUpDialog.ui
		${CMAKE_SOURCE_DIR}/gui_client/SubstrataVideoSurface.h
		${CMAKE_SOURCE_DIR}/gui_client/UserDetailsWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/UserDetailsWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/UserDetailsWidget.ui
		${CMAKE_SOURCE_DIR}/gui_client/WorldSettingsWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/WorldSettingsWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/WorldSettingsWidget.ui
		${CMAKE_SOURCE_DIR}/gui_client/URLWidget.cpp
		${CMAKE_SOURCE_DIR}/gui_client/URLWidget.h
		${CMAKE_SOURCE_DIR}/gui_client/URLWidget.ui
		${CMAKE_SOURCE_DIR}/gui_client/ShaderEditorOutputTextEdit.cpp
		${CMAKE_SOURCE_DIR}/gui_client/ShaderEditorOutputTextEdit.h
		${CMAKE_SOURCE_DIR}/gui_client/CreateObjectsDialog.cpp
		${CMAKE_SOURCE_DIR}/gui_client/CreateObjectsDialog.h
		${CMAKE_SOURCE_DIR}/gui_client/CreateObjectsDialog.ui
	)
endif()

if(NOT USE_SDL)
	SET(gui_client ${gui_client}
	${CMAKE_SOURCE_DIR}/gui_client/moc_MainWindow.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_GuiClientApplication.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_GlWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_AvatarSettingsDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_AvatarPreviewWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_AddObjectDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_AddVideoDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_AddObjectPreviewWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_ObjectEditor.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_EnvironmentOptionsWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_MaterialEditor.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_ParcelEditor.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_LoginDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_SignUpDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_UserDetailsWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_URLWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_ResetPasswordDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_ChangePasswordDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_MaterialBrowser.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_IndigoView.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_ShaderEditorDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_AboutDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_MainOptionsDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_GoToParcelDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_GoToPositionDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_FindObjectDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_ListObjectsNearbyDialog.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_LogWindow.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_DiagnosticsWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_TerrainSpecSectionWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_WorldSettingsWidget.cpp
	${CMAKE_SOURCE_DIR}/gui_client/moc_CreateObjectsDialog.cpp

	${CMAKE_SOURCE_DIR}/gui_client/ui_AddObjectDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_AddVideoDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_AvatarSettingsDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_ObjectEditor.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_EnvironmentOptionsWidget.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_ParcelEditor.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_MaterialEditor.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_LoginDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_SignUpDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_MainWindow.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_UserDetailsWidget.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_URLWidget.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_ResetPasswordDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_ChangePasswordDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_MaterialBrowser.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_ShaderEditorDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_AboutDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_MainOptionsDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_GoToParcelDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_GoToPositionDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_FindObjectDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_ListObjectsNearbyDialog.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_LogWindow.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_DiagnosticsWidget.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_TerrainSpecSectionWidget.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_WorldSettingsWidget.h
	${CMAKE_SOURCE_DIR}/gui_client/ui_CreateObjectsDialog.h
	)


	SET(qt
	${GLARE_CORE_TRUNK_DIR_ENV}/qt/QtUtils.cpp
	${GLARE_CORE_TRUNK_DIR_ENV}/qt/QtUtils.h
	../qt/SignalBlocker.cpp
	../qt/SignalBlocker.h
	../qt/FlowLayout.cpp
	../qt/FlowLayout.h
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
endif()

SET(shared_files
../shared/Avatar.cpp
../shared/Avatar.h
../shared/ImageDecoding.cpp
../shared/ImageDecoding.h
../shared/FileTypes.cpp
../shared/FileTypes.h
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
../shared/UID.h
../shared/UserID.h
../shared/Version.h
../shared/WorldObject.cpp
../shared/WorldObject.h
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
../shared/WorldSettings.cpp
../shared/WorldSettings.h
../shared/VoxelMeshBuilding.cpp
../shared/VoxelMeshBuilding.h
../shared/LuaScriptEvaluator.cpp
../shared/LuaScriptEvaluator.h
../shared/SubstrataLuaVM.cpp
../shared/SubstrataLuaVM.h
../shared/TimerQueue.cpp
../shared/TimerQueue.h
../shared/ObjectEventHandlers.cpp
../shared/ObjectEventHandlers.h
../shared/LODChunk.cpp
../shared/LODChunk.h
../shared/WorldDetails.cpp
../shared/WorldDetails.h
)

SET(client_indigo_files
${GLARE_CORE_TRUNK_DIR_ENV}/indigo/TextureServer.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/indigo/TextureServer.h
${GLARE_CORE_TRUNK_DIR_ENV}/indigo/DiscreteDistribution.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/indigo/DiscreteDistribution.h
)

SET(webserver_files
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/Escaping.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/Escaping.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/ResponseUtils.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/ResponseUtils.h
)

SET(direct3d
${GLARE_CORE_TRUNK_DIR_ENV}/direct3d/Direct3DUtils.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/direct3d/Direct3DUtils.h
)

SET(rtaudio
../rtaudio/RtAudio.cpp
../rtaudio/RtAudio.h
)

SET(audio
../audio/AudioEngine.cpp
../audio/AudioEngine.h
../audio/AudioFileReader.cpp
../audio/AudioFileReader.h
../audio/AudioResampler.cpp
../audio/AudioResampler.h
../audio/MP3AudioFileReader.cpp
../audio/MP3AudioFileReader.h
../audio/StreamerThread.cpp
../audio/StreamerThread.h
../audio/WavAudioFileReader.cpp
../audio/WavAudioFileReader.h
../audio/MicReadThread.cpp
../audio/MicReadThread.h
)

SET(ui
${GLARE_CORE_TRUNK_DIR_ENV}/ui/UIEvents.h
)

include_directories(${GLARE_CORE_TRUNK_DIR_ENV}/webserver)


set(settings
${GLARE_CORE_TRUNK_DIR_ENV}/settings/SettingsStore.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/settings/SettingsStore.h
${GLARE_CORE_TRUNK_DIR_ENV}/settings/XMLSettingsStore.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/settings/XMLSettingsStore.h
)

if(USE_SDL)
	if(EMSCRIPTEN)
		set(settings
		${settings}
		${GLARE_CORE_TRUNK_DIR_ENV}/settings/EmscriptenSettingsStore.cpp
		${GLARE_CORE_TRUNK_DIR_ENV}/settings/EmscriptenSettingsStore.h
		)
	else()
		set(settings
		${settings}	
		${GLARE_CORE_TRUNK_DIR_ENV}/settings/RegistrySettingsStore.cpp
		${GLARE_CORE_TRUNK_DIR_ENV}/settings/RegistrySettingsStore.h
		)
	endif()
else()
	set(settings
	${settings}
	${GLARE_CORE_TRUNK_DIR_ENV}/settings/QSettingsStore.cpp
	${GLARE_CORE_TRUNK_DIR_ENV}/settings/QSettingsStore.h
	)
endif()


#===================================== Docs =====================================

FILE(GLOB doc_files "../docs/*.txt")
if(NOT SUBSTRATA_PRIVATE_REPO_PATH STREQUAL "")
	FILE(GLOB private_docs "${SUBSTRATA_PRIVATE_REPO_PATH}/docs/*.txt")
	SET(doc_files
		${doc_files}
		${private_docs}
	)
endif()


#===================================== Resonance Audio =====================================
if(1)

	set(res_ambisonics
	../resonance-audio/resonance_audio/ambisonics/ambisonic_binaural_decoder.cc
	../resonance-audio/resonance_audio/ambisonics/ambisonic_binaural_decoder.h
	../resonance-audio/resonance_audio/ambisonics/ambisonic_codec.h
	../resonance-audio/resonance_audio/ambisonics/ambisonic_binaural_decoder.cc                      
	../resonance-audio/resonance_audio/ambisonics/ambisonic_binaural_decoder.h                       
	../resonance-audio/resonance_audio/ambisonics/ambisonic_codec.h                                  
	../resonance-audio/resonance_audio/ambisonics/ambisonic_codec_impl.h                             
	../resonance-audio/resonance_audio/ambisonics/ambisonic_lookup_table.cc                          
	../resonance-audio/resonance_audio/ambisonics/ambisonic_lookup_table.h                           
	../resonance-audio/resonance_audio/ambisonics/ambisonic_spread_coefficients.h                    
	../resonance-audio/resonance_audio/ambisonics/associated_legendre_polynomials_generator.cc       
	../resonance-audio/resonance_audio/ambisonics/associated_legendre_polynomials_generator.h        
	../resonance-audio/resonance_audio/ambisonics/foa_rotator.cc                                     
	../resonance-audio/resonance_audio/ambisonics/foa_rotator.h                                      
	../resonance-audio/resonance_audio/ambisonics/hoa_rotator.cc                                     
	../resonance-audio/resonance_audio/ambisonics/hoa_rotator.h                                      
	../resonance-audio/resonance_audio/ambisonics/stereo_from_soundfield_converter.cc                
	../resonance-audio/resonance_audio/ambisonics/stereo_from_soundfield_converter.h                 
	../resonance-audio/resonance_audio/ambisonics/utils.h                                            
	)

	set(res_api
	../resonance-audio/resonance_audio/api/resonance_audio_api.cc
	../resonance-audio/resonance_audio/api/resonance_audio_api.h
	)

	set(res_base
	../resonance-audio/resonance_audio/base/aligned_allocator.h             
	../resonance-audio/resonance_audio/base/audio_buffer.cc                 
	../resonance-audio/resonance_audio/base/audio_buffer.h                  
	../resonance-audio/resonance_audio/base/channel_view.cc                 
	../resonance-audio/resonance_audio/base/channel_view.h                  
	../resonance-audio/resonance_audio/base/constants_and_types.h           
	../resonance-audio/resonance_audio/base/integral_types.h                
	../resonance-audio/resonance_audio/base/logging.h                       
	../resonance-audio/resonance_audio/base/misc_math.cc                    
	../resonance-audio/resonance_audio/base/misc_math.h                     
	../resonance-audio/resonance_audio/base/object_transform.h              
	../resonance-audio/resonance_audio/base/simd_macros.h                   
	../resonance-audio/resonance_audio/base/simd_utils.cc                   
	../resonance-audio/resonance_audio/base/simd_utils.h                    
	../resonance-audio/resonance_audio/base/source_parameters.h             
	../resonance-audio/resonance_audio/base/spherical_angle.cc              
	../resonance-audio/resonance_audio/base/spherical_angle.h               
	../resonance-audio/resonance_audio/base/unique_ptr_wrapper.h            
	)

	set(res_dsp
	../resonance-audio/resonance_audio/dsp/biquad_filter.cc                                  
	../resonance-audio/resonance_audio/dsp/biquad_filter.h                                   
	../resonance-audio/resonance_audio/dsp/channel_converter.cc                              
	../resonance-audio/resonance_audio/dsp/channel_converter.h                               
	../resonance-audio/resonance_audio/dsp/circular_buffer.cc                                
	../resonance-audio/resonance_audio/dsp/circular_buffer.h                                 
	../resonance-audio/resonance_audio/dsp/delay_filter.cc                                   
	../resonance-audio/resonance_audio/dsp/delay_filter.h                                    
	../resonance-audio/resonance_audio/dsp/distance_attenuation.cc                           
	../resonance-audio/resonance_audio/dsp/distance_attenuation.h                            
	../resonance-audio/resonance_audio/dsp/fft_manager.cc                                    
	../resonance-audio/resonance_audio/dsp/fft_manager.h                                     
	../resonance-audio/resonance_audio/dsp/filter_coefficient_generators.cc                  
	../resonance-audio/resonance_audio/dsp/filter_coefficient_generators.h                   
	../resonance-audio/resonance_audio/dsp/fir_filter.cc                                     
	../resonance-audio/resonance_audio/dsp/fir_filter.h                                      
	../resonance-audio/resonance_audio/dsp/gain.cc                                           
	../resonance-audio/resonance_audio/dsp/gain.h                                            
	../resonance-audio/resonance_audio/dsp/gain_mixer.cc                                     
	../resonance-audio/resonance_audio/dsp/gain_mixer.h                                      
	../resonance-audio/resonance_audio/dsp/gain_processor.cc                                 
	../resonance-audio/resonance_audio/dsp/gain_processor.h                                  
	../resonance-audio/resonance_audio/dsp/mixer.cc                                          
	../resonance-audio/resonance_audio/dsp/mixer.h                                           
	../resonance-audio/resonance_audio/dsp/mono_pole_filter.cc                               
	../resonance-audio/resonance_audio/dsp/mono_pole_filter.h                                
	../resonance-audio/resonance_audio/dsp/multi_channel_iir.cc                              
	../resonance-audio/resonance_audio/dsp/multi_channel_iir.h                               
	../resonance-audio/resonance_audio/dsp/near_field_processor.cc                           
	../resonance-audio/resonance_audio/dsp/near_field_processor.h                            
	../resonance-audio/resonance_audio/dsp/occlusion_calculator.cc                           
	../resonance-audio/resonance_audio/dsp/occlusion_calculator.h                            
	../resonance-audio/resonance_audio/dsp/partitioned_fft_filter.cc                         
	../resonance-audio/resonance_audio/dsp/partitioned_fft_filter.h                          
	../resonance-audio/resonance_audio/dsp/reflection.h                                      
	../resonance-audio/resonance_audio/dsp/reflections_processor.cc                          
	../resonance-audio/resonance_audio/dsp/reflections_processor.h                           
	../resonance-audio/resonance_audio/dsp/resampler.cc                                      
	../resonance-audio/resonance_audio/dsp/resampler.h                                       
	../resonance-audio/resonance_audio/dsp/reverb_onset_compensator.cc                       
	../resonance-audio/resonance_audio/dsp/reverb_onset_compensator.h                        
	../resonance-audio/resonance_audio/dsp/reverb_onset_update_processor.cc                  
	../resonance-audio/resonance_audio/dsp/reverb_onset_update_processor.h                   
	../resonance-audio/resonance_audio/dsp/shoe_box_room.cc                                  
	../resonance-audio/resonance_audio/dsp/shoe_box_room.h                                   
	../resonance-audio/resonance_audio/dsp/sh_hrir_creator.cc                                
	../resonance-audio/resonance_audio/dsp/sh_hrir_creator.h                                 
	../resonance-audio/resonance_audio/dsp/spectral_reverb.cc                                
	../resonance-audio/resonance_audio/dsp/spectral_reverb.h                                 
	../resonance-audio/resonance_audio/dsp/spectral_reverb_constants_and_tables.h            
	../resonance-audio/resonance_audio/dsp/stereo_panner.cc                                  
	../resonance-audio/resonance_audio/dsp/stereo_panner.h                                   
	../resonance-audio/resonance_audio/dsp/utils.cc                                          
	../resonance-audio/resonance_audio/dsp/utils.h                                           
	)

	set(res_graph
	../resonance-audio/resonance_audio/graph/ambisonic_binaural_decoder_node.cc          
	../resonance-audio/resonance_audio/graph/ambisonic_binaural_decoder_node.h           
	../resonance-audio/resonance_audio/graph/ambisonic_mixing_encoder_node.cc            
	../resonance-audio/resonance_audio/graph/ambisonic_mixing_encoder_node.h             
	../resonance-audio/resonance_audio/graph/binaural_surround_renderer_impl.cc          
	../resonance-audio/resonance_audio/graph/binaural_surround_renderer_impl.h           
	../resonance-audio/resonance_audio/graph/buffered_source_node.cc                     
	../resonance-audio/resonance_audio/graph/buffered_source_node.h                      
	../resonance-audio/resonance_audio/graph/foa_rotator_node.cc                         
	../resonance-audio/resonance_audio/graph/foa_rotator_node.h                          
	../resonance-audio/resonance_audio/graph/gain_mixer_node.cc                          
	../resonance-audio/resonance_audio/graph/gain_mixer_node.h                           
	../resonance-audio/resonance_audio/graph/gain_node.cc                                
	../resonance-audio/resonance_audio/graph/gain_node.h                                 
	../resonance-audio/resonance_audio/graph/graph_manager.cc                            
	../resonance-audio/resonance_audio/graph/graph_manager.h                             
	../resonance-audio/resonance_audio/graph/graph_manager_config.h                      
	../resonance-audio/resonance_audio/graph/hoa_rotator_node.cc                         
	../resonance-audio/resonance_audio/graph/hoa_rotator_node.h                          
	../resonance-audio/resonance_audio/graph/mixer_node.cc                               
	../resonance-audio/resonance_audio/graph/mixer_node.h                                
	../resonance-audio/resonance_audio/graph/mono_from_soundfield_node.cc                
	../resonance-audio/resonance_audio/graph/mono_from_soundfield_node.h                 
	../resonance-audio/resonance_audio/graph/near_field_effect_node.cc                   
	../resonance-audio/resonance_audio/graph/near_field_effect_node.h                    
	../resonance-audio/resonance_audio/graph/occlusion_node.cc                           
	../resonance-audio/resonance_audio/graph/occlusion_node.h                            
	../resonance-audio/resonance_audio/graph/reflections_node.cc                         
	../resonance-audio/resonance_audio/graph/reflections_node.h                          
	../resonance-audio/resonance_audio/graph/resonance_audio_api_impl.cc                 
	../resonance-audio/resonance_audio/graph/resonance_audio_api_impl.h                  
	../resonance-audio/resonance_audio/graph/reverb_node.cc                              
	../resonance-audio/resonance_audio/graph/reverb_node.h                               
	../resonance-audio/resonance_audio/graph/source_graph_config.h                       
	../resonance-audio/resonance_audio/graph/source_parameters_manager.cc                
	../resonance-audio/resonance_audio/graph/source_parameters_manager.h                 
	../resonance-audio/resonance_audio/graph/stereo_mixing_panner_node.cc                
	../resonance-audio/resonance_audio/graph/stereo_mixing_panner_node.h                 
	../resonance-audio/resonance_audio/graph/system_settings.h        
	)

	set(res_config
	../resonance-audio/resonance_audio/config/global_config.h
	../resonance-audio/resonance_audio/config/source_config.cc
	../resonance-audio/resonance_audio/config/source_config.h
	)

	set(res_hrtf
	../resonance-audio/third_party/SADIE_hrtf_database/generated/hrtf_assets.cc
	../resonance-audio/third_party/SADIE_hrtf_database/generated/hrtf_assets.h
	)

	set(res_pffft
	#../resonance-audio/third_party/pffft/fftpack.c
	#../resonance-audio/third_party/pffft/fftpack.h
	../resonance-audio/third_party/pffft/pffft.c
	../resonance-audio/third_party/pffft/pffft.h
	)

	set(res_platforms
	../resonance-audio/platforms/common/common_utils.cc
	../resonance-audio/platforms/common/room_effects_utils.cc         
	../resonance-audio/platforms/common/room_effects_utils.h          
	../resonance-audio/platforms/common/room_properties.h             
	../resonance-audio/platforms/common/utils.h                       
	)

	set(res_node
	../resonance-audio/resonance_audio/node/node.h                
	../resonance-audio/resonance_audio/node/processing_node.cc    
	../resonance-audio/resonance_audio/node/processing_node.h     
	../resonance-audio/resonance_audio/node/publisher_node.h      
	../resonance-audio/resonance_audio/node/sink_node.cc          
	../resonance-audio/resonance_audio/node/sink_node.h           
	../resonance-audio/resonance_audio/node/source_node.cc        
	../resonance-audio/resonance_audio/node/source_node.h         
	../resonance-audio/resonance_audio/node/subscriber_node.h     
	)

	set(res_utils
	../resonance-audio/resonance_audio/utils/buffer_crossfader.cc                            
	../resonance-audio/resonance_audio/utils/buffer_crossfader.h                             
	../resonance-audio/resonance_audio/utils/buffer_partitioner.cc                           
	../resonance-audio/resonance_audio/utils/buffer_partitioner.h                            
	../resonance-audio/resonance_audio/utils/buffer_unpartitioner.cc                         
	../resonance-audio/resonance_audio/utils/buffer_unpartitioner.h                          
	../resonance-audio/resonance_audio/utils/lockless_task_queue.cc                          
	../resonance-audio/resonance_audio/utils/lockless_task_queue.h                           
	#../resonance-audio/resonance_audio/utils/ogg_vorbis_recorder.cc                          
	#../resonance-audio/resonance_audio/utils/ogg_vorbis_recorder.h                           
	../resonance-audio/resonance_audio/utils/planar_interleaved_conversion.cc                
	../resonance-audio/resonance_audio/utils/planar_interleaved_conversion.h                 
	../resonance-audio/resonance_audio/utils/pseudoinverse.h                                 
	../resonance-audio/resonance_audio/utils/sample_type_conversion.cc                       
	../resonance-audio/resonance_audio/utils/sample_type_conversion.h                        
	../resonance-audio/resonance_audio/utils/semi_lockless_fifo.h                            
	../resonance-audio/resonance_audio/utils/sum_and_difference_processor.cc                 
	../resonance-audio/resonance_audio/utils/sum_and_difference_processor.h                  
	../resonance-audio/resonance_audio/utils/task_thread_pool.cc                             
	../resonance-audio/resonance_audio/utils/task_thread_pool.h                              
	#../resonance-audio/resonance_audio/utils/test_util.cc                                    
	#../resonance-audio/resonance_audio/utils/test_util.h                                     
	../resonance-audio/resonance_audio/utils/threadsafe_fifo.h                               
	#../resonance-audio/resonance_audio/utils/vorbis_stream_encoder.cc                        
	#../resonance-audio/resonance_audio/utils/vorbis_stream_encoder.h                         
	../resonance-audio/resonance_audio/utils/wav.cc                                          
	../resonance-audio/resonance_audio/utils/wav.h                                           
	../resonance-audio/resonance_audio/utils/wav_reader.cc                                   
	../resonance-audio/resonance_audio/utils/wav_reader.h       
	)


	set(resonance
	${res_ambisonics}
	${res_api}
	${res_base}
	${res_dsp}
	${res_graph}
	${res_config}
	${res_hrtf}
	${res_pffft}
	${res_platforms}
	${res_node}
	${res_utils}
	)

	include_directories(../resonance-audio)
	include_directories(../resonance-audio/resonance_audio)
	include_directories(../resonance-audio/third_party/eigen)
	include_directories(../resonance-audio/third_party/pffft)

	# Definitions that Resonance Audio needs:
	add_definitions(-D_USE_MATH_DEFINES)
	add_definitions(-DNOMINMAX)
	add_definitions(-DEIGEN_MPL2_ONLY)
endif() # end if not emscripten.
#===================================== End Resonance Audio =====================================


#============== OpenGL engine ==============
include(${GLARE_CORE_TRUNK_DIR_ENV}/opengl/opengl.cmake)

set(substrata_shaders
../shaders/parcel_frag_shader.glsl
../shaders/parcel_vert_shader.glsl
)

SOURCE_GROUP(opengl/substrata_shaders FILES ${substrata_shaders})


#===========================================


#============== Opus ==============

set(OPUS_ROOT "${CMAKE_SOURCE_DIR}/opus/opus-1.3.1")
include(../opus/opus.cmake)

include_directories(${OPUS_ROOT}/include)



include_directories(${cef_binary_dist_dir}/include)
include_directories(${cef_binary_dist_dir})


#include_directories(../meshoptimizer/src)


#============== ImGui ==============
if(USE_SDL)
	set(imguidir 	"${GLARE_CORE_TRUNK_DIR_ENV}/ImGui")

	set(imgui
	${imguidir}/imgui.cpp
	${imguidir}/imgui.h
	${imguidir}/imgui_demo.cpp
	${imguidir}/imgui_draw.cpp
	${imguidir}/imgui_internal.h
	${imguidir}/imgui_widgets.cpp
	${imguidir}/imgui_tables.cpp
	${imguidir}/imstb_rectpack.h
	${imguidir}/imstb_textedit.h
	${imguidir}/imstb_truetype.h
	${imguidir}/backends/imgui_impl_sdl2.cpp
	${imguidir}/backends/imgui_impl_sdl2.h
	${imguidir}/backends/imgui_impl_opengl3.cpp
	${imguidir}/backends/imgui_impl_opengl3.h
	)

	include_directories(${imguidir})
endif()


#================ FreeType =============
set(freetypedir "${GLARE_CORE_TRUNK_DIR_ENV}/freetype")

include_directories("${freetypedir}/include")
include_directories("${freetypedir}/include/freetype/internal")
add_definitions("-DFT2_BUILD_LIBRARY") # NOTE: not sure if needed

file(GLOB freetype_PUBLIC_HEADERS "${freetypedir}/include/ft2build.h" "${freetypedir}/include/freetype/*.h")
file(GLOB freetype_PUBLIC_CONFIG_HEADERS "${freetypedir}/include/freetype/config/*.h")
file(GLOB freetype_PRIVATE_HEADERS "${freetypedir}/include/freetype/internal/*.h")

set(freetype
${freetype_PUBLIC_HEADERS}
${freetype_PUBLIC_CONFIG_HEADERS}
${freetype_PRIVATE_HEADERS}
${freetypedir}/src/autofit/autofit.c
${freetypedir}/src/base/ftbase.c
${freetypedir}/src/base/ftbbox.c
${freetypedir}/src/base/ftbdf.c
${freetypedir}/src/base/ftbitmap.c
${freetypedir}/src/base/ftcid.c
${freetypedir}/src/base/ftfstype.c
${freetypedir}/src/base/ftgasp.c
${freetypedir}/src/base/ftglyph.c
${freetypedir}/src/base/ftgxval.c
${freetypedir}/src/base/ftinit.c
${freetypedir}/src/base/ftmm.c
${freetypedir}/src/base/ftotval.c
${freetypedir}/src/base/ftpatent.c
${freetypedir}/src/base/ftpfr.c
${freetypedir}/src/base/ftstroke.c
${freetypedir}/src/base/ftsynth.c
${freetypedir}/src/base/fttype1.c
${freetypedir}/src/base/ftwinfnt.c
#${freetypedir}/src/bdf/bdf.c
${freetypedir}/src/bzip2/ftbzip2.c
${freetypedir}/src/cache/ftcache.c
${freetypedir}/src/cff/cff.c # OpenType (.otf) driver component
#${freetypedir}/src/cid/type1cid.c
${freetypedir}/src/gzip/ftgzip.c
${freetypedir}/src/lzw/ftlzw.c
#${freetypedir}/src/pcf/pcf.c
#${freetypedir}/src/pfr/pfr.c
${freetypedir}/src/psaux/psaux.c
${freetypedir}/src/pshinter/pshinter.c
${freetypedir}/src/psnames/psnames.c
${freetypedir}/src/raster/raster.c
${freetypedir}/src/sdf/sdf.c
${freetypedir}/src/sfnt/sfnt.c
${freetypedir}/src/smooth/smooth.c
${freetypedir}/src/svg/svg.c
${freetypedir}/src/truetype/truetype.c
#${freetypedir}/src/type1/type1.c
#${freetypedir}/src/type42/type42.c
#${freetypedir}/src/winfonts/winfnt.c
)

if(UNIX)
	set(freetype ${freetype} "${freetypedir}/builds/unix/ftsystem.c")
elseif(WIN32)
	set(freetype ${freetype} "${freetypedir}/builds/windows/ftsystem.c")
else()
	set(freetype ${freetype} "${freetypedir}/src/base/ftsystem.c")
endif ()

if (WIN32)
	#enable_language(RC)
	#list(APPEND BASE_SRCS builds/windows/ftdebug.c
	#                      src/base/ftver.rc)

	set(freetype ${freetype} "${freetypedir}/builds/windows/ftdebug.c")
else ()
	set(freetype ${freetype} "${freetypedir}/src/base/ftdebug.c")
endif ()




#============== Basis Universal ==============

include_directories(${basisu_dir})

if(TARGET_ARM64)
else()
	add_definitions(-DBASISU_SUPPORT_SSE=1)
endif()
# Disable some basis universal stuff we don't use
add_definitions(-DBASISD_SUPPORT_UASTC=0)
add_definitions(-DBASISD_SUPPORT_BC7=0)
add_definitions(-DBASISD_SUPPORT_BC7_MODE5=0)
add_definitions(-DBASISD_SUPPORT_PVRTC1=0)
add_definitions(-DBASISD_SUPPORT_PVRTC2=0)
add_definitions(-DBASISD_SUPPORT_ASTC=0)
add_definitions(-DBASISD_SUPPORT_ATC=0)
add_definitions(-DBASISD_SUPPORT_ETC2_EAC_RG11=0)
add_definitions(-DBASISD_SUPPORT_FXT1=0)


add_definitions(-DBASIS_NO_IMAGE_READERS=1) # Disable PNG and JPG reading code in basis encoder.



#======================== Indigo ===========================
if(INDIGO_SUPPORT)
	add_definitions(-DINDIGO_SUPPORT=1)
endif()


SET(rc_stuff "../icons/substrata.rc")

SOURCE_GROUP(gui_client FILES ${gui_client})
SOURCE_GROUP(qt FILES ${qt})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(client_indigo_files FILES ${client_indigo_files})
SOURCE_GROUP(docs FILES ${doc_files})
SOURCE_GROUP(video FILES ${video})
SOURCE_GROUP(indigo_audio FILES ${indigo_audio})
SOURCE_GROUP(direct3d FILES ${direct3d})
SOURCE_GROUP(webserver_files FILES ${webserver_files})
SOURCE_GROUP(rtaudio FILES ${rtaudio})
SOURCE_GROUP(audio FILES ${audio})
SOURCE_GROUP(resonance-audio FILES ${resonance})
SOURCE_GROUP(meshoptimizer FILES ${meshoptimizer})
SOURCE_GROUP(opus FILES ${opus_src})
SOURCE_GROUP(tracy FILES ${tracy_files})
SOURCE_GROUP(freetype FILES ${freetype})
SOURCE_GROUP(imgui FILES ${imgui})
SOURCE_GROUP(ui FILES ${ui})
SOURCE_GROUP(basis_universal/transcoder FILES ${basisu_transcoder_files})
SOURCE_GROUP(settings FILES ${settings})



if(NOT USE_SDL)
	include_directories(${INDIGO_QT_INCLUDE_DIR})
	link_directories(${INDIGO_QT_LIB_DIR})
endif()


if(APPLE)
	SET(EXECUTABLE_TYPE MACOSX_BUNDLE)

	# Resource files.
	SET(MAC_ICON ../scripts/osx/Cyberspace.icns)
	SET_SOURCE_FILES_PROPERTIES(../scripts/osx/Cyberspace.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

	# Plist stuff.
	SET(MACOSX_BUNDLE_ICON_FILE Cyberspace.icns)
	SET(MACOSX_BUNDLE_COPYRIGHT "Copyright 2022 Glare Technologies Ltd.")
	SET(MACOSX_BUNDLE_BUNDLE_NAME "Substrata")
	SET(MACOSX_BUNDLE_BUNDLE_VERSION "${SUBSTRATA_VERSION}")
	SET(MACOSX_BUNDLE_SHORT_VERSION_STRING "${SUBSTRATA_VERSION}")
	SET(MACOSX_BUNDLE_GUI_IDENTIFIER com.glaretechnologies.substrata)
else()
	SET(EXECUTABLE_TYPE WIN32)
endif()


add_executable(${CURRENT_TARGET} ${EXECUTABLE_TYPE}
${MAC_ICON}
${graphics}
${gui_client_graphics}
${indigo_src}
${indigo_files_in_sdk_lib}
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
${WINTER_FILES}
${gui_client}
${shared_files}
${scripts}
${double_conversion}
${opengl}
${opengl_shaders}
${substrata_shaders}
${opengl_ui}
${direct3d}
${dll_src}
${fft2d}
${xxhash}
${qt}
${doc_files}
${lang}
${video}
${webserver_files}
${rtaudio}
${audio}
${indigo_audio}
${resonance}
${meshoptimizer}
${rc_stuff}
${natvis_files}
${opus_src}
${tracy_files}
${imgui}
${freetype}
${ui}
${luau_vm}
${luau_compiler}
${lua}
${basisu_transcoder_files}
${settings}
)

add_definitions(-DGUI_CLIENT=1)

include(../cmake/shared_target_settings.cmake)

if(USE_SDL)
	include_directories(${SDL_BUILD_DIR}/include/SDL2)
	
	if(WIN32)
		include_directories(${SDL_BUILD_DIR}/include-config-release/SDL2) # For SDL_config.h.  NOTE: SDL_config.h seems to be the same for both Debug and Release configs, at least on Windows.
	else()
		include_directories(${SDL_BUILD_DIR}/include-config-/SDL2)
	endif()
	
	add_definitions(-DUSE_SDL=1)
else()
	# Else use Qt:
	add_definitions(-DUSE_QT=1)
endif()
	

if(EMSCRIPTEN)
	target_link_libraries(${CURRENT_TARGET}
		${SDL_BUILD_DIR}/libSDL2.a
		Jolt # Jolt physics
		Opus
	)
	
	# Note that a bunch of Emscripten C++ options are set in shared_cxx_settings.cmake as well.
	
	# Emscripten worker threads:
	# --------------------------
	# main_task_manager:							8
	# high_priority_task_manager:					8
	# model_and_texture_loader_task_manager:		8
	# ClientSenderThread							1
	# ClientThread									1
	# ResonanceThread								1
	# StreamerThread								1
	# UploadResourceThread							1
	# 
	# Total:										29
	
	target_link_options(${CURRENT_TARGET} PRIVATE
		"-sMIN_WEBGL_VERSION=2"
		"-sMAX_WEBGL_VERSION=2"
		#"-gsource-map" # Generate a source map using LLVM debug information: https://emscripten.org/docs/tools_reference/emcc.html NOTE: doesn't seem to work, doesn't give line numbers in stack traces.
		#"--profiling"  			# https://emscripten.org/docs/tools_reference/emcc.html
		"-fwasm-exceptions"  # Enable WASM-based exception support: https://emscripten.org/docs/porting/exceptions.html
		"-sALLOW_MEMORY_GROWTH"
		"-sMAXIMUM_MEMORY=4000MB" # Default is 2GB.
		"-pthread"
		"-sENVIRONMENT=web,worker" # Specify that the JS code only supports running in web/web workers.(Not node.js etc.)
		"-sPTHREAD_POOL_SIZE=29" # See calculation above
		"-sSTACK_SIZE=262144"
		"-sEXPORTED_RUNTIME_METHODS=ccall" # Needed to be able to call into C++ from JS.  Used in emscripten_browser_clipboard.
		"-sEXPORTED_FUNCTIONS=_processAvatarModelFile,_malloc,_free,_main"
		"-lwebsocket.js" # To use emscripten websockets API: https://emscripten.org/docs/porting/networking.html#emscripten-websockets-api
		--use-preload-cache # Cache the preloaded data in IndexedDB for subsequent page visits.
		--preload-file data # Use preloaded data, read it from the 'data' directory in the Emscripten build dir.
	)
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_DEPENDS "data")
	
elseif(WIN32)

	add_definitions(-D__WINDOWS_DS__) # Add directsound config choice define for RTAudio
	
	if((NOT FUZZING) AND (NOT (USE_SANITIZER STREQUAL "")))
		SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			/ZI") # ZI = Set debug info format to 'Program Database for Edit & Continue'  for debug builds.
		# It slows down the code too much to use for RelWithDebInfo builds.
	endif()
	
	if(NOT (USE_SANITIZER STREQUAL ""))
		# To avoid "cl : command line  error D8016: '/ZI' and '/fsanitize=address' command-line options are incompatible"
		SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			/Zi") 
		SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			/Zi") 
		SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	/Zi") 
	endif()
	
	
	
	# Enable Tracy profiler for RELEASE and RELWITHDEBINFO configs.  Can't enable for Debug config because it clashes with Edit & Continue (/ZI).
	if(TRACY_ENABLED)
		add_definitions(-DTRACY_ENABLE=1)
	endif()
	
	if(BUGSPLAT_SUPPORT)
		add_definitions(-DBUGSPLAT_SUPPORT=1) # Enable BugSplat support
	endif()
		
	if(CEF_SUPPORT)
		add_definitions(-DCEF_SUPPORT=1) # Enable Chromium Embedded Framework (CEF) support (Used for WebView).
	endif()

	target_link_libraries(${CURRENT_TARGET}
		OpenGL32

		# Media foundation video reading:
		mfplat.lib 
		mfreadwrite.lib 
		mfuuid.lib
		Mf.lib
		d3d11.lib
		
		# RtAudio:
		ole32
		winmm 
		dsound
		
		# Jolt physics
		Jolt
		
		Opus
	)
	
	if(INDIGO_SUPPORT)
		target_link_libraries(${CURRENT_TARGET}
			debug 		C:/programming/indigo/output/vs2022/indigo_x64/Debug/indigo_sdk_lib.lib
			optimized 	C:/programming/indigo/output/vs2022/indigo_x64/RelWithDebInfo/indigo_sdk_lib.lib
		)
	endif()
	
	if(USE_SDL)
		target_link_libraries(${CURRENT_TARGET}
			debug "${SDL_BUILD_DIR}/Debug/SDL2d.lib" 
			debug "${SDL_BUILD_DIR}/Debug/SDL2maind.lib" 
			optimized "${SDL_BUILD_DIR}/RelWithDebInfo/SDL2.lib" 
			optimized "${SDL_BUILD_DIR}/RelWithDebInfo/SDL2main.lib" 
		)
	else() # Else use Qt:
		target_link_libraries(${CURRENT_TARGET}
		
				# Qt 6:
		#	optimized Qt6Gui
		#	optimized Qt6Core
		#	optimized Qt6OpenGL
		#	optimized Qt6Widgets
		#	optimized Qt6OpenGLWidgets
		#	optimized Qt6Core5Compat
		#	debug Qt6Guid
		#	debug Qt6Cored
		#	debug Qt6OpenGLd
		#	debug Qt6Widgetsd
		#	debug Qt6OpenGLWidgetsd
		#	debug Qt6Core5Compatd
			
			# Qt 5:
			optimized qtmain
			optimized Qt5Gui
			optimized Qt5Core
			optimized Qt5OpenGL
			optimized Qt5Widgets
			optimized Qt5Gamepad
			debug qtmaind
			debug Qt5Guid
			debug Qt5Cored
			debug Qt5OpenGLd
			debug Qt5Widgetsd
			debug Qt5Gamepadd
		)
	endif()
	
	# Chromium embedded framework
	if(CEF_SUPPORT)
		target_link_libraries(${CURRENT_TARGET}
			debug 		${cef_binary_dist_dir}/Debug/libcef.lib
			optimized 	${cef_binary_dist_dir}/Release/libcef.lib
			
			debug     	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/Debug/libcef_dll_wrapper.lib
			optimized 	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib
		)
	endif()
	
	if(BUGSPLAT_SUPPORT)
		target_link_libraries(${CURRENT_TARGET}
			optimized ${GLARE_CORE_LIBS_ENV}/BugSplat/BugSplat/x64/Release/BugSplat64.lib
			debug ${GLARE_CORE_LIBS_ENV}/BugSplat/BugSplat/x64/Debug/BugSplat64.lib
		)
	endif()

	# Superluminal profiler:
	if(PERFORMANCEAPI_ENABLED)
		target_link_libraries(${CURRENT_TARGET}
			optimized "C:/Program Files/Superluminal/Performance/API/lib/x64/PerformanceAPI_MD.lib"
			debug "C:/Program Files/Superluminal/Performance/API/lib/x64/PerformanceAPI_MDd.lib"
		)
	endif()
	
	# target_link_options(${CURRENT_TARGET} PRIVATE "/SUBSYSTEM:CONSOLE")

	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	
	if(CEF_SUPPORT)
		add_definitions(-DCEF_SUPPORT=1) # Enable Chromium Embedded Framework (CEF) support (Used for WebView).
	endif()

	add_definitions(-D__MACOSX_CORE__) # Add CoreAudio config choice define for RTAudio
	add_definitions(-DGL_SILENCE_DEPRECATION) # Tell Mac to shut up about OpenGL being deprecated
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/osx/CyberspaceInfo.plist.in")

	if(CEF_SUPPORT)
		target_link_libraries(${CURRENT_TARGET} PRIVATE
			# Chromium embedded framework
			#debug 		${cef_binary_dist_dir}/Debug/libcef.lib
			#optimized 	${cef_binary_dist_dir}/Release/libcef.lib
			
			debug     	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/libcef_dll_wrapper.a
			optimized 	${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/libcef_dll_wrapper.a
		)
	endif()
	
	if(USE_SDL)
		target_link_libraries(${CURRENT_TARGET} PRIVATE
			${SDL_BUILD_DIR}/libSDL2.a
			${SDL_BUILD_DIR}/libSDL2main.a
			iconv
		)
	endif()
	
	target_link_libraries(${CURRENT_TARGET} PRIVATE
		Jolt # Jolt physics
		Opus
		curses # Used by some LLVM code
	)

	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
 
	if(USE_SDL)
		set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -framework OpenGL -framework Security -framework CoreAudio -framework CoreFoundation -framework AudioToolbox -framework Cocoa -framework IOKit -framework GameController -framework CoreHaptics -framework CoreVideo -framework ForceFeedback -framework Carbon -framework Metal")
	else()
		# Qt 5:
		set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtMultimedia -framework OpenGL -framework QtGamepad -framework Security -framework CoreAudio -framework CoreFoundation -framework AudioToolbox")
	 
		#Qt 6:
		 #set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtOpenGLWidgets -framework QtMultimedia -framework OpenGL -framework Security -framework CoreAudio -framework CoreFoundation -framework AudioToolbox -framework QtCore5Compat")
	endif()
	
     #add_dependencies(${CURRENT_TARGET} "gui_client Helper.app")
     
else() # linux

	if(CEF_SUPPORT)
		add_definitions(-DCEF_SUPPORT=1) # Enable Chromium Embedded Framework (CEF) support (Used for WebView).
	endif()

	add_definitions(-D__LINUX_PULSE__) # Add config choice define for RTAudio

	target_link_libraries(${CURRENT_TARGET} PRIVATE
		GL
		Qt5Gui
		Qt5Core
		Qt5OpenGL
		Qt5Widgets
		Qt5Multimedia
		Qt5Network
		Qt5Gamepad
		dl
		#asound # ALSA lib required by RTAudio
		pulse # PulseAudio lib required by RTAudio
		pulse-simple # PulseAudio lib required by RTAudio
		Jolt # Jolt physics
		Opus
	)
	
	if(CEF_SUPPORT)
		target_link_libraries(${CURRENT_TARGET} PRIVATE
			${cef_binary_dist_dir}/libcef_dll_build/libcef_dll_wrapper/libcef_dll_wrapper.a

			debug		${cef_binary_dist_dir}/Debug/libcef.so
			optimized	${cef_binary_dist_dir}/Release/libcef.so
		)
	endif()	

	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -Xlinker -rpath='$ORIGIN/lib'")
endif()
