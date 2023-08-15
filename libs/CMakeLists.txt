# libraries


include(${GLARE_CORE_TRUNK_DIR_ENV}/OpenEXR/openexr.cmake)


FILE(GLOB libjpg "${jpegdir}/ja*.c" "${jpegdir}/jc*.c" "${jpegdir}/jd*.c" "${jpegdir}/jf*.c" "${jpegdir}/ji*.c" "${jpegdir}/jq*.c"
"${jpegdir}/jutils.c"
"${jpegdir}/jmemmgr.c"
"${jpegdir}/jmemnobs.c"
"${jpegdir}/jerror.c"
)


set(libpng
${pngdir}/png.c ${pngdir}/pngerror.c ${pngdir}/pngget.c ${pngdir}/pngmem.c ${pngdir}/pngpread.c ${pngdir}/pngread.c ${pngdir}/pngrio.c ${pngdir}/pngrtran.c
${pngdir}/pngrutil.c ${pngdir}/pngset.c ${pngdir}/pngtrans.c ${pngdir}/pngwio.c ${pngdir}/pngwrite.c ${pngdir}/pngwtran.c ${pngdir}/pngwutil.c
${pngdir}/png.h
${pngdir}/pngconf.h
${pngdir}/pngdebug.h
${pngdir}/pnginfo.h
${pngdir}/pnglibconf.h
${pngdir}/pngpriv.h
${pngdir}/pngstruct.h

${pngdir}/intel/intel_init.c
${pngdir}/intel/filter_sse2_intrinsics.c

${pngdir}/arm/arm_init.c
${pngdir}/arm/filter_neon_intrinsics.c
${pngdir}/arm/palette_neon_intrinsics.c
)


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


set(zlib
${zlibdir}/adler32.c
${zlibdir}/compress.c
${zlibdir}/crc32.c
${zlibdir}/deflate.c
${zlibdir}/inffast.c
${zlibdir}/inflate.c
${zlibdir}/inftrees.c
${zlibdir}/trees.c
${zlibdir}/uncompr.c
${zlibdir}/zutil.c
${zlibdir}/crc32.h
${zlibdir}/deflate.h
${zlibdir}/inffast.h
${zlibdir}/inffixed.h
${zlibdir}/inflate.h
${zlibdir}/inftrees.h
${zlibdir}/trees.h
${zlibdir}/zconf.h
${zlibdir}/zlib.h
${zlibdir}/zutil.h
)


set(pugixml
${pugixmldir}/pugixml.cpp
${pugixmldir}/pugixml.hpp
${pugixmldir}/pugiconfig.hpp
)


enable_language(ASM) # For the .S file in zstd below.

FILE(GLOB giflib "${GLARE_CORE_TRUNK_DIR_ENV}/giflib/lib/*.c" "${GLARE_CORE_TRUNK_DIR_ENV}/giflib/lib/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/giflib/lib/*.h")
FILE(GLOB little_cms "${GLARE_CORE_TRUNK_DIR_ENV}/little_cms/src/*.c"  "${GLARE_CORE_TRUNK_DIR_ENV}/little_cms/include/*.h")
FILE(GLOB zstandard "${zstddir}/lib/*.h"  
"${zstddir}/lib/common/*.c"     "${zstddir}/lib/common/*.h"
"${zstddir}/lib/compress/*.c"   "${zstddir}/lib/compress/*.h"        
"${zstddir}/lib/decompress/*.c" "${zstddir}/lib/decompress/*.h"  "${zstddir}/lib/decompress/*.S"
)

SOURCE_GROUP(fftss FILES ${fftss})
SOURCE_GROUP(libjpg FILES ${libjpg})
SOURCE_GROUP(libpng FILES ${libpng})
SOURCE_GROUP(zlib FILES ${zlib})
SOURCE_GROUP(pugixml FILES ${pugixml})
SOURCE_GROUP(giflib FILES ${giflib})
SOURCE_GROUP(little_cms FILES ${little_cms})
SOURCE_GROUP(zstandard FILES ${zstandard})

include(../cmake/shared_cxx_settings.cmake)

#-DLIB3DS_EXPORTS
add_definitions(-DPNG_ALLOW_BENIGN_ERRORS -DTIXML_USE_STL)

add_library(libs
${fftss}
${openexr_all_files}
${libpng}
${zlib}
${pugixml}
${giflib}
${little_cms}
${zstandard}
)

if(WIN32)
	set_target_properties(libs PROPERTIES STATIC_LIBRARY_FLAGS_RELEASE "/LTCG")
endif()
