
#NOTE: We are going to rely on some variables that embed_winter.cmake (from the Winter repo) sets.

# LLVM include path and compiler settings.
if(WIN32)
	# Append LLVM paths for the configurations
	SET(CMAKE_CXX_FLAGS_RELEASE			"${CMAKE_CXX_FLAGS_RELEASE}			/I\"${llvmdir}/include\"")
	SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO	"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}	/I\"${llvmdir}/include\"")
	SET(CMAKE_CXX_FLAGS_DEBUG			"${CMAKE_CXX_FLAGS_DEBUG}			/I\"${llvmdir}_debug/include\"")
	SET(CMAKE_CXX_FLAGS_SDKDEBUG		"${CMAKE_CXX_FLAGS_SDKDEBUG}		/I\"${llvmdir}_debug/include\"")
else()
	SET(CMAKE_CXX_FLAGS	"${CMAKE_CXX_FLAGS} -I${llvmdir}/include -I/usr/local/include")
endif()


# LLVM linker settings.
if(WIN32)

	SET(LLVM_LINK_FLAGS_RELEASE			"/LIBPATH:\"${llvmdir}/lib\"")
	SET(LLVM_LINK_FLAGS_RELWITHDEBINFO	"/LIBPATH:\"${llvmdir}/lib\"")
	SET(LLVM_LINK_FLAGS_DEBUG			"/LIBPATH:\"${llvmdir}_debug/lib\"")

	SET(CMAKE_EXE_LINKER_FLAGS_RELEASE				"${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${LLVM_LINK_FLAGS_RELEASE}")
	SET(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO		"${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} ${LLVM_LINK_FLAGS_RELWITHDEBINFO}")
	SET(CMAKE_EXE_LINKER_FLAGS_DEBUG				"${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${LLVM_LINK_FLAGS_DEBUG}")

	SET(CMAKE_MODULE_LINKER_FLAGS_RELEASE			"${CMAKE_MODULE_LINKER_FLAGS_RELEASE} ${LLVM_LINK_FLAGS_RELEASE}")
	SET(CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO	"${CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO} ${LLVM_LINK_FLAGS_RELWITHDEBINFO}")
	SET(CMAKE_MODULE_LINKER_FLAGS_DEBUG				"${CMAKE_MODULE_LINKER_FLAGS_DEBUG} ${LLVM_LINK_FLAGS_DEBUG}")

elseif(APPLE)
	# get the llvm libs
	# NOTE: LLVM 3.6+ requires --system-libs also.
	#execute_process(COMMAND "${llvmdir}/bin/llvm-config" "--ldflags" "--system-libs" "--libs" "all" OUTPUT_VARIABLE LLVM_LIBS_OUT OUTPUT_STRIP_TRAILING_WHITESPACE)
	#string(REPLACE "\n" " " LLVM_LIBS_FINAL ${LLVM_LIBS_OUT})
	#	
	#MESSAGE("LLVM_LIBS_FINAL: ${LLVM_LIBS_FINAL}")
	#	
	#target_link_libraries(${CURRENT_TARGET} 
	#	${LLVM_LIBS_FINAL})

else()
	# get the llvm libs
	# NOTE: LLVM 3.6+ requires --system-libs also.
	#execute_process(COMMAND "${llvmdir}/bin/llvm-config" "--ldflags" "--system-libs" OUTPUT_VARIABLE LLVM_LIBS_OUT OUTPUT_STRIP_TRAILING_WHITESPACE)
	#string(REPLACE "\n" " " LLVM_LIBS_FINAL ${LLVM_LIBS_OUT})
	#
	## Manually append LLVM library name (We will link against libLLVM-6.0.so)
	#set(LLVM_LIBS_FINAL "${LLVM_LIBS_FINAL} -lLLVM-6.0")
	#
	#MESSAGE("LLVM_LIBS_FINAL: ${LLVM_LIBS_FINAL}")
	#	
	#target_link_libraries(${CURRENT_TARGET} 
	#	${LLVM_LIBS_FINAL})

endif()

#MESSAGE("---------------Adding link dir ${WINTER_LLVM_DIR}/lib--------------------")
#link_directories(${WINTER_LLVM_DIR}/lib)

if(WIN32)
	target_link_libraries(${CURRENT_TARGET}
		${WINTER_LLVM_LIBS}  # WINTER_LLVM_LIBS is set by embed_winter.cmake.
	)
else()
	target_link_libraries(${CURRENT_TARGET}
		PRIVATE ${WINTER_LLVM_LIBS}  # WINTER_LLVM_LIBS is set by embed_winter.cmake.
	)
endif()
