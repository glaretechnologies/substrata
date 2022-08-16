# server

set(CURRENT_TARGET server)

include(../cmake/shared_settings.cmake)
include(../cmake/shared_cxx_settings.cmake)
#include(../cmake/winter.cmake)
include_directories("${winterdir}") # Just put winter dir on include path so we can find wnt_SourceBuffer.h


FILE(GLOB server "./*.cpp" "./*.h")
FILE(GLOB webserver "../webserver/*.cpp" "../webserver/*.h")
FILE(GLOB docs "../docs/*.txt")
SET(shared_files
../shared/Avatar.cpp
../shared/Avatar.h
../shared/ImageDecoding.cpp
../shared/ImageDecoding.h
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
../shared/WorldMaterial.cpp
../shared/WorldMaterial.h
)

########### Website Core ################
SET(website_core_files
${WEBSITE_CORE_TRUNK_DIR_ENV}/Escaping.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/Escaping.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/WebListenerThread.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/WebListenerThread.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/RequestInfo.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/RequestInfo.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/ResponseUtils.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/ResponseUtils.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/Log.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/Log.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/StressTest.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/StressTest.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/TimeStamp.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/TimeStamp.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/WebsiteExcep.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/WorkerThread.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/WorkerThread.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/RequestHandler.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/RequestHandler.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/UnsafeString.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/UnsafeString.h
${WEBSITE_CORE_TRUNK_DIR_ENV}/WorkerThreadTests.cpp
${WEBSITE_CORE_TRUNK_DIR_ENV}/WorkerThreadTests.h
#${WEBSITE_CORE_TRUNK_DIR_ENV}/StaticAssetManager.cpp
#${WEBSITE_CORE_TRUNK_DIR_ENV}/StaticAssetManager.h
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


include_directories(${WEBSITE_CORE_TRUNK_DIR_ENV})


SOURCE_GROUP(server FILES ${server})
SOURCE_GROUP(webserver FILES ${webserver})
SOURCE_GROUP(website_core FILES ${website_core_files})
SOURCE_GROUP(docs FILES ${docs})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(secp256k1 FILES ${secp256k1_files})
SOURCE_GROUP(ethereum FILES ${ethereum_files})


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
${website_core_files}
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
)


#include(../cmake/shared_target_settings.cmake) # This has too much stuff we don't want.  Cherry-pick stuff from it instead.
include(../cmake/ssl.cmake)


if(WIN32)
	target_link_libraries(${CURRENT_TARGET}
		indigo_libs
		
		Iphlpapi # For GetAdaptersInfo() in SystemInfo::getMACAddresses().
		ws2_32 # Winsock

		debug     "${jpegturbodir}-debug/lib/turbojpeg-static.lib"
		optimized "${jpegturbodir}/lib/turbojpeg-static.lib"
	)

elseif(APPLE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework OpenGL -framework Security")
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "-std=c++11 -stdlib=libc++ -dead_strip -F/Library/Frameworks -framework CoreServices -framework Security")
	
	target_link_libraries(${CURRENT_TARGET}
		indigo_libs
		${jpegturbodir}/lib/libjpeg.a
	)
	
else() # linux
	#SET(QT_LINUX_LIBS dl)

	# Add required Sanitizer link flags
	if(NOT USE_SANITIZER STREQUAL "")
		SET(SANITIZER_LINK_FLAGS "-fsanitize=${USE_SANITIZER} -fno-omit-frame-pointer -g -pie")
	endif()
	
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS     "${SANITIZER_LINK_FLAGS} -Xlinker -rpath='$ORIGIN/lib'")
	
	target_link_libraries(${CURRENT_TARGET}
		indigo_libs
		${jpegturbodir}/lib/libjpeg.a
	)
endif()
