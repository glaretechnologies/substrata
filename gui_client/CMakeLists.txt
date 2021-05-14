# gui_client

set(CURRENT_TARGET gui_client)


programAvailable(${INDIGO_QT_DIR}/bin/moc)

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

addMocFileRule(qt FileSelectWidget)
addMocFileRule(qt RealControl)
addMocFileRule(qt IndigoDoubleSpinBox)
#addMocFileRule(qt RgbColourPicker)

# Rules to compile the .ui files into ui_xx.h, using 'uic'.
addUIFileRule(gui_client AddObjectDialog)
addUIFileRule(gui_client AvatarSettingsDialog)
addUIFileRule(gui_client MainWindow)
addUIFileRule(gui_client ObjectEditor)
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

${CMAKE_SOURCE_DIR}/gui_client/ui_AddObjectDialog.h
${CMAKE_SOURCE_DIR}/gui_client/ui_AvatarSettingsDialog.h
${CMAKE_SOURCE_DIR}/gui_client/ui_ObjectEditor.h
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
)

SET(qt
../qt/QtUtils.cpp
../qt/QtUtils.h
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
../shared/Version.h
../shared/WorldObject.cpp
../shared/WorldObject.h
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
)

SET(client_indigo_files
${INDIGO_TRUNK_DIR_ENV}/indigo/TextureServer.cpp
${INDIGO_TRUNK_DIR_ENV}/indigo/TextureServer.h
)

SET(website_core_files
${WEBSITE_CORE_TRUNK_DIR_ENV}/Escaping.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/Escaping.h
)

SET(direct3d
${INDIGO_TRUNK_DIR_ENV}/direct3d/Direct3DUtils.cpp
${INDIGO_TRUNK_DIR_ENV}/direct3d/Direct3DUtils.h
)

SET(rtaudio
../rtaudio/RtAudio.cpp
../rtaudio/RtAudio.h
)

SET(audio
../audio/AudioEngine.cpp
../audio/AudioEngine.h
)

include_directories(${WEBSITE_CORE_TRUNK_DIR_ENV})

SET(doc_files
../docs/notes.txt
../docs/todo.txt
../docs/deploying.txt
)


#===================================== Resonance Audio =====================================

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

#===================================== End Resonance Audio =====================================


# SET(rc_stuff "../vs2008/indigo_icon.rc")

SOURCE_GROUP(gui_client FILES ${gui_client})
SOURCE_GROUP(qt FILES ${qt})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(client_indigo_files FILES ${client_indigo_files})
SOURCE_GROUP(docs FILES ${doc_files})
SOURCE_GROUP(video FILES ${video})
SOURCE_GROUP(direct3d FILES ${direct3d})
SOURCE_GROUP(website_core_files FILES ${website_core_files})
SOURCE_GROUP(rtaudio FILES ${rtaudio})
SOURCE_GROUP(audio FILES ${audio})
SOURCE_GROUP(resonance-audio FILES ${resonance})


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


if(APPLE)
	SET(EXECUTABLE_TYPE MACOSX_BUNDLE)

	# Resource files.
	SET(MAC_ICON ../scripts/osx/Cyberspace.icns)
	SET_SOURCE_FILES_PROPERTIES(../scripts/osx/Cyberspace.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

	# Plist stuff.
	SET(MACOSX_BUNDLE_ICON_FILE Cyberspace.icns)
	SET(MACOSX_BUNDLE_COPYRIGHT "Copyright 2021 Glare Technologies Ltd.")
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
${winter}
${gui_client}
${shared_files}
${scripts}
${double_conversion}
${opengl}
${opengl_shaders}
${direct3d}
${dll_src}
${fft2d}
${xxhash}
${qt}
${doc_files}
${lang}
${video}
${website_core_files}
${rtaudio}
${audio}
${resonance}
)

add_definitions(-DGUI_CLIENT=1)

include(../cmake/shared_target_settings.cmake)


if(WIN32)

	add_definitions(-D__WINDOWS_DS__) # Add directsound config choice define for RTAudio

	SET(QT_WIN32_LIBS
		OpenGL32
		optimized qtmain
		optimized Qt5Gui
		optimized Qt5Core
		optimized Qt5OpenGL
		optimized Qt5Widgets
#		optimized Qt5Multimedia
		debug qtmaind
		debug Qt5Guid
		debug Qt5Cored
		debug Qt5OpenGLd
		debug Qt5Widgetsd
#		debug Qt5Multimediad
#		debug "C:/programming/indigo/output/vs2015/indigo_x64/Debug/indigo_sdk_lib.lib" # TEMP
#		optimized "C:/programming/indigo/output/vs2015/indigo_x64/RelWithDebInfo/indigo_sdk_lib.lib" # TEMP

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
	)

	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	
	add_definitions(-D__MACOSX_CORE__) # Add CoreAudio config choice define for RTAudio
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/osx/CyberspaceInfo.plist.in")


	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui
		-framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtSql -framework OpenGL -framework Security
		-framework CoreAudio -framework CoreFoundation")
else() # linux

	add_definitions(-D__LINUX_ALSA__) # Add ALSA config choice define for RTAudio

	SET(QT_LINUX_LIBS GL
		Qt5Gui
		Qt5Core
		Qt5OpenGL
		Qt5Widgets
		dl
		asound # ALSA lib required by RTAudio
	)

	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -Xlinker -rpath='$ORIGIN/lib'")
endif()


target_link_libraries(${CURRENT_TARGET}
${QT_WIN32_LIBS}
${QT_LINUX_LIBS}
#debug 		C:/programming/indigo/output/vs2019/indigo_x64/Debug/indigo_sdk_lib.lib
#optimized 	C:/programming/indigo/output/vs2019/indigo_x64/RelWithDebInfo/indigo_sdk_lib.lib
)
