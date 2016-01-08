# indigo libs


FILE(GLOB ilmbase "${ilmbasedir}/Half/*.cpp" "${ilmbasedir}/Iex/*.cpp" "${ilmbasedir}/Imath/*.cpp")
SET(ilmthread "${ilmbasedir}/IlmThread/IlmThread.cpp"
"${ilmbasedir}/IlmThread/IlmThreadMutex.cpp"
"${ilmbasedir}/IlmThread/IlmThreadMutexPosix.cpp"
"${ilmbasedir}/IlmThread/IlmThreadPool.cpp"
"${ilmbasedir}/IlmThread/IlmThreadPosix.cpp"
"${ilmbasedir}/IlmThread/IlmThreadSemaphore.cpp"
"${ilmbasedir}/IlmThread/IlmThreadSemaphorePosix.cpp"
"${ilmbasedir}/IlmThread/IlmThreadSemaphorePosixCompat.cpp")
if(WIN32)
	SET(ilmthread ${ilmthread} "${ilmbasedir}/IlmThread/IlmThreadMutexWin32.cpp" "${ilmbasedir}/IlmThread/IlmThreadWin32.cpp" "${ilmbasedir}/IlmThread/IlmThreadSemaphoreWin32.cpp")
endif()
FILE(GLOB openexr "${openexrdir}/IlmImf/*.cpp")
# FILE(GLOB lib3ds "../../../lib3ds-1.2.0/lib3ds/*.c")


FILE(GLOB libjpg "${jpegdir}/ja*.c" "${jpegdir}/jc*.c" "${jpegdir}/jd*.c" "${jpegdir}/jf*.c" "${jpegdir}/ji*.c" "${jpegdir}/jq*.c"
"${jpegdir}/jutils.c"
"${jpegdir}/jmemmgr.c"
"${jpegdir}/jmemnobs.c"
"${jpegdir}/jerror.c"
)


if(WIN32 OR APPLE)
	set(libpng
	${pngdir}/png.c ${pngdir}/pngerror.c ${pngdir}/pngget.c ${pngdir}/pngmem.c ${pngdir}/pngpread.c ${pngdir}/pngread.c ${pngdir}/pngrio.c ${pngdir}/pngrtran.c
	${pngdir}/pngrutil.c ${pngdir}/pngset.c ${pngdir}/pngtrans.c ${pngdir}/pngwio.c ${pngdir}/pngwrite.c ${pngdir}/pngwtran.c ${pngdir}/pngwutil.c
	${pngdir}/png.h ${pngdir}/pngconf.h ${pngdir}/pngpriv.h 
	)
else()
	set(libpng "")
endif()


set(libtiff
${tiffdir}/tif_aux.c ${tiffdir}/tif_close.c 
${tiffdir}/tif_codec.c ${tiffdir}/tif_color.c ${tiffdir}/tif_compress.c ${tiffdir}/tif_dir.c 
${tiffdir}/tif_dirinfo.c ${tiffdir}/tif_dirread.c 
${tiffdir}/tif_dirwrite.c ${tiffdir}/tif_dumpmode.c ${tiffdir}/tif_error.c ${tiffdir}/tif_extension.c
${tiffdir}/tif_fax3.c ${tiffdir}/tif_fax3sm.c ${tiffdir}/tif_flush.c ${tiffdir}/tif_getimage.c ${tiffdir}/tif_jpeg.c 
${tiffdir}/tif_luv.c ${tiffdir}/tif_lzw.c ${tiffdir}/tif_next.c ${tiffdir}/tif_ojpeg.c 
${tiffdir}/tif_open.c ${tiffdir}/tif_packbits.c ${tiffdir}/tif_pixarlog.c ${tiffdir}/tif_predict.c ${tiffdir}/tif_print.c
${tiffdir}/tif_read.c ${tiffdir}/tif_strip.c ${tiffdir}/tif_swab.c ${tiffdir}/tif_thunder.c ${tiffdir}/tif_tile.c 
${tiffdir}/tif_unix.c ${tiffdir}/tif_version.c ${tiffdir}/tif_warning.c ${tiffdir}/tif_write.c ${tiffdir}/tif_zip.c
)

FILE(GLOB lua "${luadir}/src/*.c")

set(zlib
${zlibdir}/adler32.c
${zlibdir}/compress.c
${zlibdir}/crc32.c
${zlibdir}/deflate.c
${zlibdir}/infback.c
${zlibdir}/inffast.c
${zlibdir}/inflate.c
${zlibdir}/inftrees.c
${zlibdir}/trees.c
${zlibdir}/uncompr.c
${zlibdir}/zutil.c
)

SET(minizip "${zlibdir}/contrib/minizip/ioapi.c"
"${zlibdir}/contrib/minizip/unzip.c"
"${zlibdir}/contrib/minizip/zip.c")

#set(zlib
#${zlibdir}/adler32.c ${zlibdir}/compress.c ${zlibdir}/crc32.c ${zlibdir}/deflate.c ${zlibdir}/gzclose.c ${zlibdir}/gzlib.c ${zlibdir}/gzread.c ${zlibdir}/gzwrite.c
#${zlibdir}/infback.c ${zlibdir}/inffast.c ${zlibdir}/inflate.c ${zlibdir}/inftrees.c ${zlibdir}/trees.c ${zlibdir}/uncompr.c ${zlibdir}/zutil.c
#${zlibdir}/contrib/minizip/unzip.c ${zlibdir}/contrib/minizip/zip.c ${zlibdir}/contrib/minizip/ioapi.c
#)

set(pugixml
${pugixmldir}/pugixml.cpp
${pugixmldir}/pugixml.hpp
${pugixmldir}/pugiconfig.hpp
)

# MESSAGE(${pugixml})

FILE(GLOB giflib "${INDIGO_TRUNK_DIR_ENV}/giflib-5.0.4/lib/*.c" "${INDIGO_TRUNK_DIR_ENV}/giflib-5.0.4/lib/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/giflib-5.0.4/lib/*.h")
FILE(GLOB little_cms "${INDIGO_TRUNK_DIR_ENV}/little_cms/src/*.c"  "${INDIGO_TRUNK_DIR_ENV}/little_cms/include/*.h")
FILE(GLOB xxhash "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.c"  "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.h")

SOURCE_GROUP(fftss FILES ${fftss})
SOURCE_GROUP(ilmbase FILES ${ilmbase})
SOURCE_GROUP(ilmthread FILES ${ilmthread})
SOURCE_GROUP(openexr FILES ${openexr})
#SOURCE_GROUP(lib3ds FILES ${lib3ds})
SOURCE_GROUP(libjpg FILES ${libjpg})
SOURCE_GROUP(libpng FILES ${libpng})
SOURCE_GROUP(libtiff FILES ${libtiff})
#SOURCE_GROUP(lua FILES ${lua})
SOURCE_GROUP(zlib FILES ${zlib})
SOURCE_GROUP(zlib\\minizip FILES ${minizip})
SOURCE_GROUP(pugixml FILES ${pugixml})
SOURCE_GROUP(giflib FILES ${giflib})
SOURCE_GROUP(little_cms FILES ${little_cms})
SOURCE_GROUP(xxhash FILES ${xxhash})

include(../cmake/shared_cxx_settings.cmake)

#-DLIB3DS_EXPORTS
add_definitions(-DPNG_ALLOW_BENIGN_ERRORS -DTIXML_USE_STL)


if(APPLE)
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Xarch_x86_64 -D__x86_64__")
elseif(WIN32)

else()
	# Linux
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
endif()


add_library(indigo_libs
${fftss}
${ilmbase}
${ilmthread}
${openexr}
#${lib3ds}
${libjpg}
${libpng}
${libtiff}
#${lua}
${zlib}
${minizip}
${pugixml}
${giflib}
${little_cms}
${xxhash}
)


if(WIN32)
	set_target_properties(indigo_libs PROPERTIES STATIC_LIBRARY_FLAGS_RELEASE "/LTCG")
endif()
