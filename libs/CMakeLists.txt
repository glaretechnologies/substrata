# indigo libs


SET(ilmbase
${ilmbasedir}/Half/half.cpp
${ilmbasedir}/Half/half.h
${ilmbasedir}/Iex/Iex.h
${ilmbasedir}/Iex/IexBaseExc.cpp
${ilmbasedir}/Iex/IexBaseExc.h
${ilmbasedir}/Iex/IexErrnoExc.h
${ilmbasedir}/Iex/IexMacros.h
${ilmbasedir}/Iex/IexMathExc.h
${ilmbasedir}/Iex/IexThrowErrnoExc.cpp
${ilmbasedir}/Iex/IexThrowErrnoExc.h
# EMPTY: ${ilmbasedir}/Imath/ImathBox.cpp

${ilmbasedir}/IexMath/IexMathFloatExc.cpp
${ilmbasedir}/IexMath/IexMathFloatExc.h
${ilmbasedir}/IexMath/IexMathFpu.cpp
${ilmbasedir}/IexMath/IexMathFpu.h
${ilmbasedir}/IexMath/IexMathIeeeExc.h

#${ilmbasedir}/Imath/ImathBox.cpp
${ilmbasedir}/Imath/ImathBox.h
${ilmbasedir}/Imath/ImathBoxAlgo.h
${ilmbasedir}/Imath/ImathColor.h
${ilmbasedir}/Imath/ImathColorAlgo.cpp
${ilmbasedir}/Imath/ImathColorAlgo.h
${ilmbasedir}/Imath/ImathEuler.h
${ilmbasedir}/Imath/ImathExc.h
${ilmbasedir}/Imath/ImathExport.h
${ilmbasedir}/Imath/ImathForward.h
${ilmbasedir}/Imath/ImathFrame.h
${ilmbasedir}/Imath/ImathFrustum.h
${ilmbasedir}/Imath/ImathFrustumTest.h
${ilmbasedir}/Imath/ImathFun.cpp
${ilmbasedir}/Imath/ImathFun.h
${ilmbasedir}/Imath/ImathGL.h
${ilmbasedir}/Imath/ImathGLU.h
${ilmbasedir}/Imath/ImathHalfLimits.h
${ilmbasedir}/Imath/ImathInt64.h
${ilmbasedir}/Imath/ImathInterval.h
${ilmbasedir}/Imath/ImathLimits.h
${ilmbasedir}/Imath/ImathLine.h
${ilmbasedir}/Imath/ImathLineAlgo.h
${ilmbasedir}/Imath/ImathMath.h
${ilmbasedir}/Imath/ImathMatrix.h
${ilmbasedir}/Imath/ImathMatrixAlgo.cpp
${ilmbasedir}/Imath/ImathMatrixAlgo.h
${ilmbasedir}/Imath/ImathNamespace.h
${ilmbasedir}/Imath/ImathPlane.h
${ilmbasedir}/Imath/ImathPlatform.h
${ilmbasedir}/Imath/ImathQuat.h
${ilmbasedir}/Imath/ImathRandom.cpp
${ilmbasedir}/Imath/ImathRandom.h
${ilmbasedir}/Imath/ImathRoots.h
#${ilmbasedir}/Imath/ImathShear.cpp
${ilmbasedir}/Imath/ImathShear.h
${ilmbasedir}/Imath/ImathSphere.h
${ilmbasedir}/Imath/ImathVec.cpp
${ilmbasedir}/Imath/ImathVec.h
${ilmbasedir}/Imath/ImathVecAlgo.h
)


SET(ilmthread
${ilmbasedir}/IlmThread/IlmThread.cpp
${ilmbasedir}/IlmThread/IlmThread.h
${ilmbasedir}/IlmThread/IlmThreadExport.h
${ilmbasedir}/IlmThread/IlmThreadForward.h
${ilmbasedir}/IlmThread/IlmThreadMutex.cpp
${ilmbasedir}/IlmThread/IlmThreadMutex.h
${ilmbasedir}/IlmThread/IlmThreadMutexPosix.cpp
${ilmbasedir}/IlmThread/IlmThreadNamespace.h
${ilmbasedir}/IlmThread/IlmThreadPool.cpp
${ilmbasedir}/IlmThread/IlmThreadPool.h
${ilmbasedir}/IlmThread/IlmThreadPosix.cpp
${ilmbasedir}/IlmThread/IlmThreadSemaphore.cpp
${ilmbasedir}/IlmThread/IlmThreadSemaphore.h
${ilmbasedir}/IlmThread/IlmThreadSemaphorePosix.cpp
${ilmbasedir}/IlmThread/IlmThreadSemaphorePosixCompat.cpp
)
if(WIN32)
	SET(ilmthread
	${ilmthread}
	${ilmbasedir}/IlmThread/IlmThreadMutexWin32.cpp
	${ilmbasedir}/IlmThread/IlmThreadWin32.cpp
	${ilmbasedir}/IlmThread/IlmThreadSemaphoreWin32.cpp)
endif()

SET(openexr
${openexrdir}/IlmImf/b44ExpLogTable.cpp
${openexrdir}/IlmImf/b44ExpLogTable.h
${openexrdir}/IlmImf/ImfAcesFile.cpp
${openexrdir}/IlmImf/ImfAcesFile.h
${openexrdir}/IlmImf/ImfArray.h
${openexrdir}/IlmImf/ImfAttribute.cpp
${openexrdir}/IlmImf/ImfAttribute.h
${openexrdir}/IlmImf/ImfAutoArray.h
${openexrdir}/IlmImf/ImfB44Compressor.cpp
${openexrdir}/IlmImf/ImfB44Compressor.h
${openexrdir}/IlmImf/ImfBoxAttribute.cpp
${openexrdir}/IlmImf/ImfBoxAttribute.h
${openexrdir}/IlmImf/ImfChannelList.cpp
${openexrdir}/IlmImf/ImfChannelList.h
${openexrdir}/IlmImf/ImfChannelListAttribute.cpp
${openexrdir}/IlmImf/ImfChannelListAttribute.h
${openexrdir}/IlmImf/ImfCheckedArithmetic.h
${openexrdir}/IlmImf/ImfChromaticities.cpp
${openexrdir}/IlmImf/ImfChromaticities.h
${openexrdir}/IlmImf/ImfChromaticitiesAttribute.cpp
${openexrdir}/IlmImf/ImfChromaticitiesAttribute.h
${openexrdir}/IlmImf/ImfCompositeDeepScanLine.cpp
${openexrdir}/IlmImf/ImfCompositeDeepScanLine.h
${openexrdir}/IlmImf/ImfCompression.h
${openexrdir}/IlmImf/ImfCompressionAttribute.cpp
${openexrdir}/IlmImf/ImfCompressionAttribute.h
${openexrdir}/IlmImf/ImfCompressor.cpp
${openexrdir}/IlmImf/ImfCompressor.h
${openexrdir}/IlmImf/ImfConvert.cpp
${openexrdir}/IlmImf/ImfConvert.h
${openexrdir}/IlmImf/ImfCRgbaFile.cpp
${openexrdir}/IlmImf/ImfCRgbaFile.h
${openexrdir}/IlmImf/ImfDeepCompositing.cpp
${openexrdir}/IlmImf/ImfDeepCompositing.h
${openexrdir}/IlmImf/ImfDeepFrameBuffer.cpp
${openexrdir}/IlmImf/ImfDeepFrameBuffer.h
${openexrdir}/IlmImf/ImfDeepImageState.h
${openexrdir}/IlmImf/ImfDeepImageStateAttribute.cpp
${openexrdir}/IlmImf/ImfDeepImageStateAttribute.h
${openexrdir}/IlmImf/ImfDeepScanLineInputFile.cpp
${openexrdir}/IlmImf/ImfDeepScanLineInputFile.h
${openexrdir}/IlmImf/ImfDeepScanLineInputPart.cpp
${openexrdir}/IlmImf/ImfDeepScanLineInputPart.h
${openexrdir}/IlmImf/ImfDeepScanLineOutputFile.cpp
${openexrdir}/IlmImf/ImfDeepScanLineOutputFile.h
${openexrdir}/IlmImf/ImfDeepScanLineOutputPart.cpp
${openexrdir}/IlmImf/ImfDeepScanLineOutputPart.h
${openexrdir}/IlmImf/ImfDeepTiledInputFile.cpp
${openexrdir}/IlmImf/ImfDeepTiledInputFile.h
${openexrdir}/IlmImf/ImfDeepTiledInputPart.cpp
${openexrdir}/IlmImf/ImfDeepTiledInputPart.h
${openexrdir}/IlmImf/ImfDeepTiledOutputFile.cpp
${openexrdir}/IlmImf/ImfDeepTiledOutputFile.h
${openexrdir}/IlmImf/ImfDeepTiledOutputPart.cpp
${openexrdir}/IlmImf/ImfDeepTiledOutputPart.h
${openexrdir}/IlmImf/ImfDoubleAttribute.cpp
${openexrdir}/IlmImf/ImfDoubleAttribute.h
${openexrdir}/IlmImf/ImfDwaCompressor.cpp
${openexrdir}/IlmImf/ImfDwaCompressor.h
${openexrdir}/IlmImf/ImfDwaCompressorSimd.h
${openexrdir}/IlmImf/ImfEnvmap.cpp
${openexrdir}/IlmImf/ImfEnvmap.h
${openexrdir}/IlmImf/ImfEnvmapAttribute.cpp
${openexrdir}/IlmImf/ImfEnvmapAttribute.h
${openexrdir}/IlmImf/ImfExport.h
${openexrdir}/IlmImf/ImfFastHuf.cpp
${openexrdir}/IlmImf/ImfFastHuf.h
${openexrdir}/IlmImf/ImfFloatAttribute.cpp
${openexrdir}/IlmImf/ImfFloatAttribute.h
${openexrdir}/IlmImf/ImfFloatVectorAttribute.cpp
${openexrdir}/IlmImf/ImfFloatVectorAttribute.h
${openexrdir}/IlmImf/ImfForward.h
${openexrdir}/IlmImf/ImfFrameBuffer.cpp
${openexrdir}/IlmImf/ImfFrameBuffer.h
${openexrdir}/IlmImf/ImfFramesPerSecond.cpp
${openexrdir}/IlmImf/ImfFramesPerSecond.h
${openexrdir}/IlmImf/ImfGenericInputFile.cpp
${openexrdir}/IlmImf/ImfGenericInputFile.h
${openexrdir}/IlmImf/ImfGenericOutputFile.cpp
${openexrdir}/IlmImf/ImfGenericOutputFile.h
${openexrdir}/IlmImf/ImfHeader.cpp
${openexrdir}/IlmImf/ImfHeader.h
${openexrdir}/IlmImf/ImfHuf.cpp
${openexrdir}/IlmImf/ImfHuf.h
${openexrdir}/IlmImf/ImfInputFile.cpp
${openexrdir}/IlmImf/ImfInputFile.h
${openexrdir}/IlmImf/ImfInputPart.cpp
${openexrdir}/IlmImf/ImfInputPart.h
${openexrdir}/IlmImf/ImfInputPartData.cpp
${openexrdir}/IlmImf/ImfInputPartData.h
${openexrdir}/IlmImf/ImfInputStreamMutex.h
${openexrdir}/IlmImf/ImfInt64.h
${openexrdir}/IlmImf/ImfIntAttribute.cpp
${openexrdir}/IlmImf/ImfIntAttribute.h
${openexrdir}/IlmImf/ImfIO.cpp
${openexrdir}/IlmImf/ImfIO.h
${openexrdir}/IlmImf/ImfKeyCode.cpp
${openexrdir}/IlmImf/ImfKeyCode.h
${openexrdir}/IlmImf/ImfKeyCodeAttribute.cpp
${openexrdir}/IlmImf/ImfKeyCodeAttribute.h
${openexrdir}/IlmImf/ImfLineOrder.h
${openexrdir}/IlmImf/ImfLineOrderAttribute.cpp
${openexrdir}/IlmImf/ImfLineOrderAttribute.h
${openexrdir}/IlmImf/ImfLut.cpp
${openexrdir}/IlmImf/ImfLut.h
${openexrdir}/IlmImf/ImfMatrixAttribute.cpp
${openexrdir}/IlmImf/ImfMatrixAttribute.h
${openexrdir}/IlmImf/ImfMisc.cpp
${openexrdir}/IlmImf/ImfMisc.h
${openexrdir}/IlmImf/ImfMultiPartInputFile.cpp
${openexrdir}/IlmImf/ImfMultiPartInputFile.h
${openexrdir}/IlmImf/ImfMultiPartOutputFile.cpp
${openexrdir}/IlmImf/ImfMultiPartOutputFile.h
${openexrdir}/IlmImf/ImfMultiView.cpp
${openexrdir}/IlmImf/ImfMultiView.h
${openexrdir}/IlmImf/ImfName.h
${openexrdir}/IlmImf/ImfNamespace.h
${openexrdir}/IlmImf/ImfOpaqueAttribute.cpp
${openexrdir}/IlmImf/ImfOpaqueAttribute.h
${openexrdir}/IlmImf/ImfOptimizedPixelReading.h
${openexrdir}/IlmImf/ImfOutputFile.cpp
${openexrdir}/IlmImf/ImfOutputFile.h
${openexrdir}/IlmImf/ImfOutputPart.cpp
${openexrdir}/IlmImf/ImfOutputPart.h
${openexrdir}/IlmImf/ImfOutputPartData.cpp
${openexrdir}/IlmImf/ImfOutputPartData.h
${openexrdir}/IlmImf/ImfOutputStreamMutex.h
${openexrdir}/IlmImf/ImfPartHelper.h
${openexrdir}/IlmImf/ImfPartType.cpp
${openexrdir}/IlmImf/ImfPartType.h
${openexrdir}/IlmImf/ImfPixelType.h
${openexrdir}/IlmImf/ImfPizCompressor.cpp
${openexrdir}/IlmImf/ImfPizCompressor.h
${openexrdir}/IlmImf/ImfPreviewImage.cpp
${openexrdir}/IlmImf/ImfPreviewImage.h
${openexrdir}/IlmImf/ImfPreviewImageAttribute.cpp
${openexrdir}/IlmImf/ImfPreviewImageAttribute.h
${openexrdir}/IlmImf/ImfPxr24Compressor.cpp
${openexrdir}/IlmImf/ImfPxr24Compressor.h
${openexrdir}/IlmImf/ImfRational.cpp
${openexrdir}/IlmImf/ImfRational.h
${openexrdir}/IlmImf/ImfRationalAttribute.cpp
${openexrdir}/IlmImf/ImfRationalAttribute.h
${openexrdir}/IlmImf/ImfRgba.h
${openexrdir}/IlmImf/ImfRgbaFile.cpp
${openexrdir}/IlmImf/ImfRgbaFile.h
${openexrdir}/IlmImf/ImfRgbaYca.cpp
${openexrdir}/IlmImf/ImfRgbaYca.h
${openexrdir}/IlmImf/ImfRle.cpp
${openexrdir}/IlmImf/ImfRle.h
${openexrdir}/IlmImf/ImfRleCompressor.cpp
${openexrdir}/IlmImf/ImfRleCompressor.h
${openexrdir}/IlmImf/ImfScanLineInputFile.cpp
${openexrdir}/IlmImf/ImfScanLineInputFile.h
${openexrdir}/IlmImf/ImfSimd.h
${openexrdir}/IlmImf/ImfStandardAttributes.cpp
${openexrdir}/IlmImf/ImfStandardAttributes.h
${openexrdir}/IlmImf/ImfStdIO.cpp
${openexrdir}/IlmImf/ImfStdIO.h
${openexrdir}/IlmImf/ImfStringAttribute.cpp
${openexrdir}/IlmImf/ImfStringAttribute.h
${openexrdir}/IlmImf/ImfStringVectorAttribute.cpp
${openexrdir}/IlmImf/ImfStringVectorAttribute.h
${openexrdir}/IlmImf/ImfSystemSpecific.cpp
${openexrdir}/IlmImf/ImfSystemSpecific.h
${openexrdir}/IlmImf/ImfTestFile.cpp
${openexrdir}/IlmImf/ImfTestFile.h
${openexrdir}/IlmImf/ImfThreading.cpp
${openexrdir}/IlmImf/ImfThreading.h
${openexrdir}/IlmImf/ImfTileDescription.h
${openexrdir}/IlmImf/ImfTileDescriptionAttribute.cpp
${openexrdir}/IlmImf/ImfTileDescriptionAttribute.h
${openexrdir}/IlmImf/ImfTiledInputFile.cpp
${openexrdir}/IlmImf/ImfTiledInputFile.h
${openexrdir}/IlmImf/ImfTiledInputPart.cpp
${openexrdir}/IlmImf/ImfTiledInputPart.h
${openexrdir}/IlmImf/ImfTiledMisc.cpp
${openexrdir}/IlmImf/ImfTiledMisc.h
${openexrdir}/IlmImf/ImfTiledOutputFile.cpp
${openexrdir}/IlmImf/ImfTiledOutputFile.h
${openexrdir}/IlmImf/ImfTiledOutputPart.cpp
${openexrdir}/IlmImf/ImfTiledOutputPart.h
${openexrdir}/IlmImf/ImfTiledRgbaFile.cpp
${openexrdir}/IlmImf/ImfTiledRgbaFile.h
${openexrdir}/IlmImf/ImfTileOffsets.cpp
${openexrdir}/IlmImf/ImfTileOffsets.h
${openexrdir}/IlmImf/ImfTimeCode.cpp
${openexrdir}/IlmImf/ImfTimeCode.h
${openexrdir}/IlmImf/ImfTimeCodeAttribute.cpp
${openexrdir}/IlmImf/ImfTimeCodeAttribute.h
${openexrdir}/IlmImf/ImfVecAttribute.cpp
${openexrdir}/IlmImf/ImfVecAttribute.h
${openexrdir}/IlmImf/ImfVersion.cpp
${openexrdir}/IlmImf/ImfVersion.h
${openexrdir}/IlmImf/ImfWav.cpp
${openexrdir}/IlmImf/ImfWav.h
${openexrdir}/IlmImf/ImfXdr.h
${openexrdir}/IlmImf/ImfZip.cpp
${openexrdir}/IlmImf/ImfZip.h
${openexrdir}/IlmImf/ImfZipCompressor.cpp
${openexrdir}/IlmImf/ImfZipCompressor.h


${openexrdir}/IlmImfUtil/ImfDeepImage.cpp
${openexrdir}/IlmImfUtil/ImfDeepImage.h
${openexrdir}/IlmImfUtil/ImfDeepImageChannel.cpp
${openexrdir}/IlmImfUtil/ImfDeepImageChannel.h
${openexrdir}/IlmImfUtil/ImfDeepImageIO.cpp
${openexrdir}/IlmImfUtil/ImfDeepImageIO.h
${openexrdir}/IlmImfUtil/ImfDeepImageLevel.cpp
${openexrdir}/IlmImfUtil/ImfDeepImageLevel.h
${openexrdir}/IlmImfUtil/ImfFlatImage.cpp
${openexrdir}/IlmImfUtil/ImfFlatImage.h
${openexrdir}/IlmImfUtil/ImfFlatImageChannel.cpp
${openexrdir}/IlmImfUtil/ImfFlatImageChannel.h
${openexrdir}/IlmImfUtil/ImfFlatImageIO.cpp
${openexrdir}/IlmImfUtil/ImfFlatImageIO.h
${openexrdir}/IlmImfUtil/ImfFlatImageLevel.cpp
${openexrdir}/IlmImfUtil/ImfFlatImageLevel.h
${openexrdir}/IlmImfUtil/ImfImage.cpp
${openexrdir}/IlmImfUtil/ImfImage.h
${openexrdir}/IlmImfUtil/ImfImageChannel.cpp
${openexrdir}/IlmImfUtil/ImfImageChannel.h
${openexrdir}/IlmImfUtil/ImfImageChannelRenaming.h
${openexrdir}/IlmImfUtil/ImfImageDataWindow.cpp
${openexrdir}/IlmImfUtil/ImfImageDataWindow.h
${openexrdir}/IlmImfUtil/ImfImageIO.cpp
${openexrdir}/IlmImfUtil/ImfImageIO.h
${openexrdir}/IlmImfUtil/ImfImageLevel.cpp
${openexrdir}/IlmImfUtil/ImfImageLevel.h
${openexrdir}/IlmImfUtil/ImfSampleCountChannel.cpp
${openexrdir}/IlmImfUtil/ImfSampleCountChannel.h
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

FILE(GLOB giflib "${INDIGO_TRUNK_DIR_ENV}/giflib/lib/*.c" "${INDIGO_TRUNK_DIR_ENV}/giflib/lib/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/giflib/lib/*.h")
FILE(GLOB little_cms "${INDIGO_TRUNK_DIR_ENV}/little_cms/src/*.c"  "${INDIGO_TRUNK_DIR_ENV}/little_cms/include/*.h")
#FILE(GLOB xxhash "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.c"  "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.h")
FILE(GLOB zstandard "${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/*.h"  "${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/common/*.c" "${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/common/*.h"
"${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/compress/*.c" "${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/compress/*.h"
"${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/decompress/*.c" "${INDIGO_TRUNK_DIR_ENV}/zstd-1.0.0/lib/decompress/*.h"
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
${ilmbase}
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
