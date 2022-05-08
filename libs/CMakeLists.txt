# indigo libs




# OpenEXR's math lib
set(imath
${imathdir}/src/Imath/half.cpp
${imathdir}/src/Imath/ImathColorAlgo.cpp
${imathdir}/src/Imath/ImathFun.cpp
${imathdir}/src/Imath/ImathMatrixAlgo.cpp
${imathdir}/src/Imath/ImathRandom.cpp
${imathdir}/src/Imath/toFloat.h
${imathdir}/src/Imath/half.h
${imathdir}/src/Imath/halfFunction.h
${imathdir}/src/Imath/halfLimits.h
${imathdir}/src/Imath/ImathBox.h
${imathdir}/src/Imath/ImathBoxAlgo.h
${imathdir}/src/Imath/ImathColor.h
${imathdir}/src/Imath/ImathColorAlgo.h
${imathdir}/src/Imath/ImathEuler.h
${imathdir}/src/Imath/ImathExport.h
${imathdir}/src/Imath/ImathForward.h
${imathdir}/src/Imath/ImathFrame.h
${imathdir}/src/Imath/ImathFrustum.h
${imathdir}/src/Imath/ImathFrustumTest.h
${imathdir}/src/Imath/ImathFun.h
${imathdir}/src/Imath/ImathGL.h
${imathdir}/src/Imath/ImathGLU.h
${imathdir}/src/Imath/ImathInt64.h
${imathdir}/src/Imath/ImathInterval.h
${imathdir}/src/Imath/ImathLine.h
${imathdir}/src/Imath/ImathLineAlgo.h
${imathdir}/src/Imath/ImathMath.h
${imathdir}/src/Imath/ImathMatrix.h
${imathdir}/src/Imath/ImathMatrixAlgo.h
${imathdir}/src/Imath/ImathNamespace.h
${imathdir}/src/Imath/ImathPlane.h
${imathdir}/src/Imath/ImathPlatform.h
${imathdir}/src/Imath/ImathQuat.h
${imathdir}/src/Imath/ImathRandom.h
${imathdir}/src/Imath/ImathRoots.h
${imathdir}/src/Imath/ImathShear.h
${imathdir}/src/Imath/ImathSphere.h
${imathdir}/src/Imath/ImathTypeTraits.h
${imathdir}/src/Imath/ImathVec.h
${imathdir}/src/Imath/ImathVecAlgo.h
)

# OpenEXR exception classes.
SET(iex
	${openexrdir}/src/lib/Iex/IexBaseExc.cpp
    ${openexrdir}/src/lib/Iex/IexMathFloatExc.cpp
    ${openexrdir}/src/lib/Iex/IexMathFpu.cpp
    ${openexrdir}/src/lib/Iex/IexThrowErrnoExc.cpp

    ${openexrdir}/src/lib/Iex/Iex.h
    ${openexrdir}/src/lib/Iex/IexBaseExc.h
    ${openexrdir}/src/lib/Iex/IexErrnoExc.h
    ${openexrdir}/src/lib/Iex/IexExport.h
    ${openexrdir}/src/lib/Iex/IexForward.h
    ${openexrdir}/src/lib/Iex/IexMacros.h
    ${openexrdir}/src/lib/Iex/IexMathExc.h
    ${openexrdir}/src/lib/Iex/IexMathFloatExc.h
    ${openexrdir}/src/lib/Iex/IexMathIeeeExc.h
    ${openexrdir}/src/lib/Iex/IexNamespace.h
    ${openexrdir}/src/lib/Iex/IexThrowErrnoExc.h
)


SET(ilmthread
${openexrdir}/src/lib/IlmThread/IlmThread.cpp
${openexrdir}/src/lib/IlmThread/IlmThread.h
${openexrdir}/src/lib/IlmThread/IlmThreadExport.h
${openexrdir}/src/lib/IlmThread/IlmThreadForward.h
#$openexrdirr}src/lib//IlmThread/IlmThreadMutex.cpp   		# Effectively empty
${openexrdir}/src/lib/IlmThread/IlmThreadMutex.h
#$openexrdirr}src/lib//IlmThread/IlmThreadMutexPosix.cpp   	# Effectively empty
${openexrdir}/src/lib/IlmThread/IlmThreadNamespace.h
${openexrdir}/src/lib/IlmThread/IlmThreadPool.cpp
${openexrdir}/src/lib/IlmThread/IlmThreadPool.h
#$openexrdirr}src/lib//IlmThread/IlmThreadPosix.cpp     	# Effectively empty
#$openexrdirr}src/lib//IlmThread/IlmThreadSemaphore.cpp		# Effectively empty
${openexrdir}/src/lib/IlmThread/IlmThreadSemaphore.h
${openexrdir}/src/lib/IlmThread/IlmThreadSemaphorePosix.cpp
${openexrdir}/src/lib/IlmThread/IlmThreadSemaphorePosixCompat.cpp
)
if(WIN32)
	SET(ilmthread
	${ilmthread}
	${openexrdir}/src/lib/IlmThread/IlmThreadSemaphoreWin32.cpp)
endif()

SET(openexr
${openexrdir}/src/lib/OpenEXR/b44ExpLogTable.h
${openexrdir}/src/lib/OpenEXR/dwaLookups.h
${openexrdir}/src/lib/OpenEXR/ImfAcesFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfB44Compressor.cpp
${openexrdir}/src/lib/OpenEXR/ImfBoxAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfChannelList.cpp
${openexrdir}/src/lib/OpenEXR/ImfChannelListAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfChromaticities.cpp
${openexrdir}/src/lib/OpenEXR/ImfChromaticitiesAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfCompositeDeepScanLine.cpp
${openexrdir}/src/lib/OpenEXR/ImfCompressionAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfCompressor.cpp
${openexrdir}/src/lib/OpenEXR/ImfConvert.cpp
${openexrdir}/src/lib/OpenEXR/ImfCRgbaFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepCompositing.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepFrameBuffer.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepImageStateAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineInputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineOutputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineOutputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledInputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledOutputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledOutputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfDoubleAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfDwaCompressor.cpp
${openexrdir}/src/lib/OpenEXR/ImfEnvmap.cpp
${openexrdir}/src/lib/OpenEXR/ImfEnvmapAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfFastHuf.cpp
${openexrdir}/src/lib/OpenEXR/ImfFloatAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfFloatVectorAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfFrameBuffer.cpp
${openexrdir}/src/lib/OpenEXR/ImfFramesPerSecond.cpp
${openexrdir}/src/lib/OpenEXR/ImfGenericInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfGenericOutputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfHeader.cpp
${openexrdir}/src/lib/OpenEXR/ImfHuf.cpp
${openexrdir}/src/lib/OpenEXR/ImfIDManifest.cpp
${openexrdir}/src/lib/OpenEXR/ImfIDManifestAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfInputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfInputPartData.cpp
${openexrdir}/src/lib/OpenEXR/ImfIntAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfIO.cpp
${openexrdir}/src/lib/OpenEXR/ImfKeyCode.cpp
${openexrdir}/src/lib/OpenEXR/ImfKeyCodeAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfLineOrderAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfLut.cpp
${openexrdir}/src/lib/OpenEXR/ImfMatrixAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfMisc.cpp
${openexrdir}/src/lib/OpenEXR/ImfMultiPartInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfMultiPartOutputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfMultiView.cpp
${openexrdir}/src/lib/OpenEXR/ImfOpaqueAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfOutputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfOutputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfOutputPartData.cpp
${openexrdir}/src/lib/OpenEXR/ImfPartType.cpp
${openexrdir}/src/lib/OpenEXR/ImfPizCompressor.cpp
${openexrdir}/src/lib/OpenEXR/ImfPreviewImage.cpp
${openexrdir}/src/lib/OpenEXR/ImfPreviewImageAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfPxr24Compressor.cpp
${openexrdir}/src/lib/OpenEXR/ImfRational.cpp
${openexrdir}/src/lib/OpenEXR/ImfRationalAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfRgbaFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfRgbaYca.cpp
${openexrdir}/src/lib/OpenEXR/ImfRle.cpp
${openexrdir}/src/lib/OpenEXR/ImfRleCompressor.cpp
${openexrdir}/src/lib/OpenEXR/ImfScanLineInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfStandardAttributes.cpp
${openexrdir}/src/lib/OpenEXR/ImfStdIO.cpp
${openexrdir}/src/lib/OpenEXR/ImfStringAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfStringVectorAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfSystemSpecific.cpp
${openexrdir}/src/lib/OpenEXR/ImfTestFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfThreading.cpp
${openexrdir}/src/lib/OpenEXR/ImfTileDescriptionAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfTiledInputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfTiledInputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfTiledMisc.cpp
${openexrdir}/src/lib/OpenEXR/ImfTiledOutputFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfTiledOutputPart.cpp
${openexrdir}/src/lib/OpenEXR/ImfTiledRgbaFile.cpp
${openexrdir}/src/lib/OpenEXR/ImfTileOffsets.cpp
${openexrdir}/src/lib/OpenEXR/ImfTimeCode.cpp
${openexrdir}/src/lib/OpenEXR/ImfTimeCodeAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfVecAttribute.cpp
${openexrdir}/src/lib/OpenEXR/ImfVersion.cpp
${openexrdir}/src/lib/OpenEXR/ImfWav.cpp
${openexrdir}/src/lib/OpenEXR/ImfZip.cpp
${openexrdir}/src/lib/OpenEXR/ImfZipCompressor.cpp

${openexrdir}/src/lib/OpenEXR/ImfAcesFile.h
${openexrdir}/src/lib/OpenEXR/ImfArray.h
${openexrdir}/src/lib/OpenEXR/ImfAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfBoxAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfChannelList.h
${openexrdir}/src/lib/OpenEXR/ImfChannelListAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfChromaticities.h
${openexrdir}/src/lib/OpenEXR/ImfChromaticitiesAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfCompositeDeepScanLine.h
${openexrdir}/src/lib/OpenEXR/ImfCompression.h
${openexrdir}/src/lib/OpenEXR/ImfCompressionAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfConvert.h
${openexrdir}/src/lib/OpenEXR/ImfCRgbaFile.h
${openexrdir}/src/lib/OpenEXR/ImfDeepCompositing.h
${openexrdir}/src/lib/OpenEXR/ImfDeepFrameBuffer.h
${openexrdir}/src/lib/OpenEXR/ImfDeepImageState.h
${openexrdir}/src/lib/OpenEXR/ImfDeepImageStateAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineInputFile.h
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineInputPart.h
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineOutputFile.h
${openexrdir}/src/lib/OpenEXR/ImfDeepScanLineOutputPart.h
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledInputFile.h
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledInputPart.h
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledOutputFile.h
${openexrdir}/src/lib/OpenEXR/ImfDeepTiledOutputPart.h
${openexrdir}/src/lib/OpenEXR/ImfDoubleAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfEnvmap.h
${openexrdir}/src/lib/OpenEXR/ImfEnvmapAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfExport.h
${openexrdir}/src/lib/OpenEXR/ImfFloatAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfFloatVectorAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfForward.h
${openexrdir}/src/lib/OpenEXR/ImfFrameBuffer.h
${openexrdir}/src/lib/OpenEXR/ImfFramesPerSecond.h
${openexrdir}/src/lib/OpenEXR/ImfGenericInputFile.h
${openexrdir}/src/lib/OpenEXR/ImfGenericOutputFile.h
${openexrdir}/src/lib/OpenEXR/ImfHeader.h
${openexrdir}/src/lib/OpenEXR/ImfHuf.h
${openexrdir}/src/lib/OpenEXR/ImfIDManifest.h
${openexrdir}/src/lib/OpenEXR/ImfIDManifestAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfInputFile.h
${openexrdir}/src/lib/OpenEXR/ImfInputPart.h
${openexrdir}/src/lib/OpenEXR/ImfInt64.h
${openexrdir}/src/lib/OpenEXR/ImfIntAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfIO.h
${openexrdir}/src/lib/OpenEXR/ImfKeyCode.h
${openexrdir}/src/lib/OpenEXR/ImfKeyCodeAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfLineOrder.h
${openexrdir}/src/lib/OpenEXR/ImfLineOrderAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfLut.h
${openexrdir}/src/lib/OpenEXR/ImfMatrixAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfMultiPartInputFile.h
${openexrdir}/src/lib/OpenEXR/ImfMultiPartOutputFile.h
${openexrdir}/src/lib/OpenEXR/ImfMultiView.h
${openexrdir}/src/lib/OpenEXR/ImfName.h
${openexrdir}/src/lib/OpenEXR/ImfNamespace.h
${openexrdir}/src/lib/OpenEXR/ImfOpaqueAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfOutputFile.h
${openexrdir}/src/lib/OpenEXR/ImfOutputPart.h
${openexrdir}/src/lib/OpenEXR/ImfPartHelper.h
${openexrdir}/src/lib/OpenEXR/ImfPartType.h
${openexrdir}/src/lib/OpenEXR/ImfPixelType.h
${openexrdir}/src/lib/OpenEXR/ImfPreviewImage.h
${openexrdir}/src/lib/OpenEXR/ImfPreviewImageAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfRational.h
${openexrdir}/src/lib/OpenEXR/ImfRationalAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfRgba.h
${openexrdir}/src/lib/OpenEXR/ImfRgbaFile.h
${openexrdir}/src/lib/OpenEXR/ImfRgbaYca.h
${openexrdir}/src/lib/OpenEXR/ImfStandardAttributes.h
${openexrdir}/src/lib/OpenEXR/ImfStdIO.h
${openexrdir}/src/lib/OpenEXR/ImfStringAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfStringVectorAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfTestFile.h
${openexrdir}/src/lib/OpenEXR/ImfThreading.h
${openexrdir}/src/lib/OpenEXR/ImfTileDescription.h
${openexrdir}/src/lib/OpenEXR/ImfTileDescriptionAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfTiledInputFile.h
${openexrdir}/src/lib/OpenEXR/ImfTiledInputPart.h
${openexrdir}/src/lib/OpenEXR/ImfTiledOutputFile.h
${openexrdir}/src/lib/OpenEXR/ImfTiledOutputPart.h
${openexrdir}/src/lib/OpenEXR/ImfTiledRgbaFile.h
${openexrdir}/src/lib/OpenEXR/ImfTimeCode.h
${openexrdir}/src/lib/OpenEXR/ImfTimeCodeAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfVecAttribute.h
${openexrdir}/src/lib/OpenEXR/ImfVersion.h
${openexrdir}/src/lib/OpenEXR/ImfWav.h
${openexrdir}/src/lib/OpenEXR/ImfXdr.h
)






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

#FILE(GLOB lua "${luadir}/src/*.c")

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

SET(minizip 
${zlibdir}/contrib/minizip/ioapi.c
${zlibdir}/contrib/minizip/unzip.c
${zlibdir}/contrib/minizip/zip.c
${zlibdir}/contrib/minizip/crypt.h
${zlibdir}/contrib/minizip/ioapi.h
${zlibdir}/contrib/minizip/unzip.h
${zlibdir}/contrib/minizip/zip.h
)

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

enable_language(ASM) # For the .S file in zstd below.

FILE(GLOB giflib "${INDIGO_TRUNK_DIR_ENV}/giflib/lib/*.c" "${INDIGO_TRUNK_DIR_ENV}/giflib/lib/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/giflib/lib/*.h")
FILE(GLOB little_cms "${INDIGO_TRUNK_DIR_ENV}/little_cms/src/*.c"  "${INDIGO_TRUNK_DIR_ENV}/little_cms/include/*.h")
#FILE(GLOB xxhash "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.c"  "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.h")
FILE(GLOB zstandard "${zstddir}/lib/*.h"  
"${zstddir}/lib/common/*.c"     "${zstddir}/lib/common/*.h"
"${zstddir}/lib/compress/*.c"   "${zstddir}/lib/compress/*.h"        
"${zstddir}/lib/decompress/*.c" "${zstddir}/lib/decompress/*.h"  "${zstddir}/lib/decompress/*.S"
)

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
#SOURCE_GROUP(xxhash FILES ${xxhash})
SOURCE_GROUP(zstandard FILES ${zstandard})

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

${imath}
${iex}
${ilmthread}
${openexr}

#${lib3ds}
#${libjpg}
${libpng}
${libtiff}
#${lua}
${zlib}
${minizip}
${pugixml}
${giflib}
${little_cms}
#${xxhash}
${zstandard}
)


if(WIN32)
	set_target_properties(indigo_libs PROPERTIES STATIC_LIBRARY_FLAGS_RELEASE "/LTCG")
endif()
