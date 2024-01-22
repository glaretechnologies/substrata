# server

set(CURRENT_TARGET server)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
#include(../cmake/winter.cmake)
include_directories("${winterdir}") # Just put winter dir on include path so we can find wnt_SourceBuffer.h


FILE(GLOB docs "../docs/*.txt")
if(NOT SUBSTRATA_PRIVATE_REPO_PATH STREQUAL "")
	FILE(GLOB private_docs "${SUBSTRATA_PRIVATE_REPO_PATH}/docs/*.txt")
	SET(docs
		${docs}
		${private_docs}
	)
endif()

FILE(GLOB server "./*.cpp" "./*.h")
FILE(GLOB webserver "../webserver/*.cpp" "../webserver/*.h")
SET(shared_files
../shared/Avatar.cpp
../shared/Avatar.h
../shared/ImageDecoding.cpp
../shared/ImageDecoding.h
../shared/FileTypes.cpp
../shared/FileTypes.h
../shared/LODGeneration.cpp
../shared/LODGeneration.h
../shared/MessageUtils.h
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
../shared/VoxelMeshBuilding.cpp
../shared/VoxelMeshBuilding.h
../shared/WorldObject.cpp
../shared/WorldObject.h
../shared/WorldSettings.cpp
../shared/WorldSettings.h
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
)

########### Webserver ################
SET(webserver_core_files
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/Escaping.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/Escaping.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WebListenerThread.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WebListenerThread.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/RequestInfo.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/RequestInfo.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/ResponseUtils.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/ResponseUtils.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/Log.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/Log.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/StressTest.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/StressTest.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/TimeStamp.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/TimeStamp.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WebsiteExcep.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WorkerThread.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WorkerThread.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/RequestHandler.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/RequestHandler.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/UnsafeString.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/UnsafeString.h
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WebWorkerThreadTests.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/webserver/WebWorkerThreadTests.h
#${GLARE_CORE_TRUNK_DIR_ENV}/webserver/StaticAssetManager.cpp
#${GLARE_CORE_TRUNK_DIR_ENV}/webserver/StaticAssetManager.h
)

# Add eliptical curve stuff for Eth code.
SET(secp256k1_files
../secp256k1-master/src/assumptions.h                        
../secp256k1-master/src/basic-config.h                       
../secp256k1-master/src/ecdsa.h                              
../secp256k1-master/src/ecdsa_impl.h                         
../secp256k1-master/src/eckey.h                              
../secp256k1-master/src/eckey_impl.h                         
../secp256k1-master/src/ecmult.h                             
../secp256k1-master/src/ecmult_const.h                       
../secp256k1-master/src/ecmult_const_impl.h                  
../secp256k1-master/src/ecmult_gen.h                         
../secp256k1-master/src/ecmult_gen_impl.h                    
../secp256k1-master/src/ecmult_impl.h                        
../secp256k1-master/src/field.h                              
../secp256k1-master/src/field_10x26.h                        
../secp256k1-master/src/field_10x26_impl.h                   
../secp256k1-master/src/field_5x52.h                         
../secp256k1-master/src/field_5x52_asm_impl.h                
../secp256k1-master/src/field_5x52_impl.h                    
../secp256k1-master/src/field_5x52_int128_impl.h             
../secp256k1-master/src/field_impl.h                         
../secp256k1-master/src/group.h                              
../secp256k1-master/src/group_impl.h                         
../secp256k1-master/src/hash.h                               
../secp256k1-master/src/hash_impl.h                          
../secp256k1-master/src/modinv32.h                           
../secp256k1-master/src/modinv32_impl.h                      
../secp256k1-master/src/modinv64.h                           
../secp256k1-master/src/modinv64_impl.h                      
../secp256k1-master/src/scalar.h                             
../secp256k1-master/src/scalar_4x64.h                        
../secp256k1-master/src/scalar_4x64_impl.h                   
../secp256k1-master/src/scalar_8x32.h                        
../secp256k1-master/src/scalar_8x32_impl.h                   
../secp256k1-master/src/scalar_impl.h                        
../secp256k1-master/src/scalar_low.h                         
../secp256k1-master/src/scalar_low_impl.h                    
../secp256k1-master/src/scratch.h                            
../secp256k1-master/src/scratch_impl.h                       
../secp256k1-master/src/secp256k1.c                          
../secp256k1-master/src/selftest.h                           
../secp256k1-master/src/util.h                               

../secp256k1-master/src/modules/recovery/main_impl.h                
)


# Add eliptical curve stuff for Eth code.
SET(ethereum_files
../ethereum/Infura.cpp                     
../ethereum/Infura.h     
../ethereum/RLP.cpp                     
../ethereum/RLP.h                     
../ethereum/Signing.cpp                     
../ethereum/Signing.h       
../ethereum/EthAddress.h              
../ethereum/EthTransaction.h
../ethereum/UInt256.cpp
../ethereum/UInt256.h
)

include_directories(${basisu_dir})

if(TARGET_ARM64)
else()
	add_definitions(-DBASISU_SUPPORT_SSE=1)
endif()
# Disable some basis universal stuff we don't use
# add_definitions(-DBASISD_SUPPORT_UASTC=0) # This would be nice but it doesn't compile with it defined.
# add_definitions(-DBASISD_SUPPORT_BC7_MODE5=0)
# add_definitions(-DBASISD_SUPPORT_PVRTC1=0)
# add_definitions(-DBASISD_SUPPORT_ETC2_EAC_A8=0)
# add_definitions(-DBASISD_SUPPORT_UASTC=0)
# add_definitions(-DBASISD_SUPPORT_ASTC=0)

add_definitions(-DBASIS_NO_IMAGE_READERS=1)



include(../webclient/CMakeLists.txt)


include_directories(${GLARE_CORE_TRUNK_DIR_ENV}/webserver/)

include_directories(../webserver/)



if(ENABLE_GLARE_PARCEL_AUCTION_CODE)
	#MESSAGE("Server: enabling USE_GLARE_PARCEL_AUCTION_CODE")
	#MESSAGE("SUBSTRATA_PRIVATE_REPO_PATH: ${SUBSTRATA_PRIVATE_REPO_PATH}")
	
	add_definitions(-DUSE_GLARE_PARCEL_AUCTION_CODE=1)
	
	FILE(GLOB substrata_private_webserver 
	"${SUBSTRATA_PRIVATE_REPO_PATH}/webserver/*.cpp" 
	"${SUBSTRATA_PRIVATE_REPO_PATH}/webserver/*.h"
	)
	
	FILE(GLOB substrata_private_server
	"${SUBSTRATA_PRIVATE_REPO_PATH}/server/*.cpp" 
	"${SUBSTRATA_PRIVATE_REPO_PATH}/server/*.h"
	)
	
	include_directories(${SUBSTRATA_PRIVATE_REPO_PATH})
endif()



SOURCE_GROUP(server FILES ${server})
SOURCE_GROUP(webserver FILES ${webserver})
SOURCE_GROUP(substrata_private_webserver FILES ${substrata_private_webserver})
SOURCE_GROUP(substrata_private_server FILES ${substrata_private_server})
SOURCE_GROUP(webserver_core_files FILES ${webserver_core_files})
SOURCE_GROUP(docs FILES ${docs})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(secp256k1 FILES ${secp256k1_files})
SOURCE_GROUP(ethereum FILES ${ethereum_files})
SOURCE_GROUP(basis_universal FILES ${basisu_files})


# From secp256k1-master\configure.ac:
#[window size for ecmult precomputation for verification, specified as integer in range [2..24].]
#[Larger values result in possibly better performance at the cost of an exponentially larger precomputed table.]
#[The table will store 2^(SIZE-1) * 64 bytes of data but can be larger in memory due to platform-specific padding and alignment.]
#["auto" is a reasonable setting for desktop machines (currently 15). [default=auto]]

#[Precision bits to tune the precomputed table size for signing.]
#[The size of the table is 32kB for 2 bits, 64kB for 4 bits, 512kB for 8 bits of precision.]
#[A larger table size usually results in possible faster signing.]
#["auto" is a reasonable setting for desktop machines (currently 4). [default=auto]]

#NOTE: this table seems to be allocated on stack, so keep size down to avoid stack overflow.

add_definitions(-DECMULT_WINDOW_SIZE=8 -DECMULT_GEN_PREC_BITS=2 -DENABLE_MODULE_RECOVERY=1)


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
#${winter}
${server}
${webserver}
${webserver_core_files}
${shared_files}
${scripts}
${double_conversion}
${dll_src}
${fft2d}
${xxhash}
${docs}
${lang}
${secp256k1_files}
${ethereum_files}
${meshoptimizer}
${webclient_files}
${substrata_private_webserver}
${substrata_private_server}
${basisu_files}
)


#include(../cmake/shared_target_settings.cmake) # This has too much stuff we don't want.  Cherry-pick stuff from it instead.
include(../cmake/ssl.cmake)


if(WIN32)

	# /DEBUG /OPT:REF /OPT:ICF are for writing pdb files that can be used with minidumps.
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /OPT:REF /OPT:ICF")


	target_link_libraries(${CURRENT_TARGET}
		libs
		
		Iphlpapi # For GetAdaptersInfo() in SystemInfo::getMACAddresses().
		ws2_32 # Winsock

		debug     "${jpegturbodir}-debug/lib/turbojpeg-static.lib"
		optimized "${jpegturbodir}/lib/turbojpeg-static.lib"
	)

elseif(APPLE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework OpenGL -framework Security")
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++11 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework CoreServices -framework Security")
	
	target_link_libraries(${CURRENT_TARGET} PRIVATE
		libs
		${jpegturbodir}/lib/libjpeg.a
	)
	
else() # linux
	#SET(QT_LINUX_LIBS dl)

	# Add required Sanitizer link flags
	if(NOT USE_SANITIZER STREQUAL "")
		SET(SANITIZER_LINK_FLAGS "-fsanitize=${USE_SANITIZER} -fno-omit-frame-pointer -g -pie")
	endif()
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib'")
	
	target_link_libraries(${CURRENT_TARGET} PRIVATE
		libs
		${jpegturbodir}/lib/libjpeg.a
	)
endif()
