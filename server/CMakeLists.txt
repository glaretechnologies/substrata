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

include_directories(${WEBSITE_CORE_TRUNK_DIR_ENV})


SOURCE_GROUP(server FILES ${server})
SOURCE_GROUP(webserver FILES ${webserver})
SOURCE_GROUP(website_core FILES ${website_core_files})
SOURCE_GROUP(docs FILES ${docs})
SOURCE_GROUP(shared_files FILES ${shared_files})
SOURCE_GROUP(secp256k1 FILES ${secp256k1_files})


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
${secp256k1_files}
)


include(../cmake/shared_target_settings.cmake)


if(WIN32)
	SET(QT_WIN32_LIBS)

	# Disable ASLR (/DYNAMICBASE)
	#get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	#set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} /DYNAMICBASE:NO")
elseif(APPLE)
	get_target_property(OLD_TARGET_PROPERTIES ${CURRENT_TARGET} LINK_FLAGS)
	set_target_properties(${CURRENT_TARGET} PROPERTIES LINK_FLAGS "${OLD_TARGET_PROPERTIES} -F${INDIGO_QT_LIB_DIR} -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork -framework QtOpenGL -framework QtSql -framework OpenGL -framework Security")
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
