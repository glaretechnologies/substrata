# shared settings for console sdk and gui

set(INDIGO_GRAPHICS_DIR "${INDIGO_TRUNK_DIR_ENV}/graphics")
set(graphics
${INDIGO_GRAPHICS_DIR}/bitmap.cpp
${INDIGO_GRAPHICS_DIR}/bitmap.h
${INDIGO_GRAPHICS_DIR}/bmpdecoder.cpp
${INDIGO_GRAPHICS_DIR}/bmpdecoder.h
${INDIGO_GRAPHICS_DIR}/BoxFilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/BoxFilterFunction.h
${INDIGO_GRAPHICS_DIR}/colour3.h
${INDIGO_GRAPHICS_DIR}/Colour3f.cpp
${INDIGO_GRAPHICS_DIR}/Colour4f.cpp
${INDIGO_GRAPHICS_DIR}/Colour4f.h
${INDIGO_GRAPHICS_DIR}/Drawing.cpp
${INDIGO_GRAPHICS_DIR}/Drawing.h
${INDIGO_GRAPHICS_DIR}/EXRDecoder.cpp
${INDIGO_GRAPHICS_DIR}/EXRDecoder.h
${INDIGO_GRAPHICS_DIR}/FilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/FilterFunction.h
${INDIGO_GRAPHICS_DIR}/FFTPlan.cpp
${INDIGO_GRAPHICS_DIR}/FFTPlan.h
${INDIGO_GRAPHICS_DIR}/FloatDecoder.cpp
${INDIGO_GRAPHICS_DIR}/FloatDecoder.h
${INDIGO_GRAPHICS_DIR}/formatdecoderobj.cpp
${INDIGO_GRAPHICS_DIR}/formatdecoderobj.h
${INDIGO_GRAPHICS_DIR}/GaussianFilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/GaussianFilterFunction.h
${INDIGO_GRAPHICS_DIR}/GaussianImageFilter.cpp
${INDIGO_GRAPHICS_DIR}/GaussianImageFilter.h
${INDIGO_GRAPHICS_DIR}/GifDecoder.cpp
${INDIGO_GRAPHICS_DIR}/GifDecoder.h
${INDIGO_GRAPHICS_DIR}/GridNoise.cpp
${INDIGO_GRAPHICS_DIR}/GridNoise.h
${INDIGO_GRAPHICS_DIR}/HaarWavelet.cpp
${INDIGO_GRAPHICS_DIR}/HaarWavelet.h
${INDIGO_GRAPHICS_DIR}/image.cpp
${INDIGO_GRAPHICS_DIR}/image.h
${INDIGO_GRAPHICS_DIR}/Image4f.cpp
${INDIGO_GRAPHICS_DIR}/Image4f.h
${INDIGO_GRAPHICS_DIR}/ImageDiff.cpp
${INDIGO_GRAPHICS_DIR}/ImageDiff.h
${INDIGO_GRAPHICS_DIR}/ImageErrorMetric.cpp
${INDIGO_GRAPHICS_DIR}/ImageErrorMetric.h
${INDIGO_GRAPHICS_DIR}/ImageFilter.cpp
${INDIGO_GRAPHICS_DIR}/ImageFilter.h
${INDIGO_GRAPHICS_DIR}/ImageMap.cpp
${INDIGO_GRAPHICS_DIR}/ImageMap.h
${INDIGO_GRAPHICS_DIR}/ImageMapTests.cpp
${INDIGO_GRAPHICS_DIR}/ImageMapTests.h
#${INDIGO_GRAPHICS_DIR}/ImagingPipeline.cpp
#${INDIGO_GRAPHICS_DIR}/ImagingPipeline.h
${INDIGO_GRAPHICS_DIR}/imformatdecoder.cpp
${INDIGO_GRAPHICS_DIR}/imformatdecoder.h
${INDIGO_GRAPHICS_DIR}/jpegdecoder.cpp
${INDIGO_GRAPHICS_DIR}/jpegdecoder.h
${INDIGO_GRAPHICS_DIR}/Map2D.cpp
${INDIGO_GRAPHICS_DIR}/Map2D.h
${INDIGO_GRAPHICS_DIR}/MitchellNetravali.cpp
${INDIGO_GRAPHICS_DIR}/MitchellNetravali.h
${INDIGO_GRAPHICS_DIR}/MitchellNetravaliFilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/MitchellNetravaliFilterFunction.h
${INDIGO_GRAPHICS_DIR}/NoiseTests.cpp
${INDIGO_GRAPHICS_DIR}/NoiseTests.h
${INDIGO_GRAPHICS_DIR}/noise_notes.txt
${INDIGO_GRAPHICS_DIR}/PerlinNoise.cpp
${INDIGO_GRAPHICS_DIR}/PerlinNoise.h
${INDIGO_GRAPHICS_DIR}/PNGDecoder.cpp
${INDIGO_GRAPHICS_DIR}/PNGDecoder.h
${INDIGO_GRAPHICS_DIR}/RGBEDecoder.cpp
${INDIGO_GRAPHICS_DIR}/RGBEDecoder.h
${INDIGO_GRAPHICS_DIR}/SharpFilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/SharpFilterFunction.h
${INDIGO_GRAPHICS_DIR}/spherehammersly.h
${INDIGO_GRAPHICS_DIR}/TextDrawer.cpp
${INDIGO_GRAPHICS_DIR}/TextDrawer.h
${INDIGO_GRAPHICS_DIR}/tgadecoder.cpp
${INDIGO_GRAPHICS_DIR}/tgadecoder.h
${INDIGO_GRAPHICS_DIR}/TIFFDecoder.cpp
${INDIGO_GRAPHICS_DIR}/TIFFDecoder.h
${INDIGO_GRAPHICS_DIR}/TriBoxIntersection.cpp
${INDIGO_GRAPHICS_DIR}/TriBoxIntersection.h
${INDIGO_GRAPHICS_DIR}/Voronoi.cpp
${INDIGO_GRAPHICS_DIR}/Voronoi.h
)


set(INDIGO_UTILS_DIR "${INDIGO_TRUNK_DIR_ENV}/utils")
set(utils
${INDIGO_UTILS_DIR}/AESEncryption.cpp
${INDIGO_UTILS_DIR}/AESEncryption.h
${INDIGO_UTILS_DIR}/ArgumentParser.cpp
${INDIGO_UTILS_DIR}/ArgumentParser.h
${INDIGO_UTILS_DIR}/Array.h
${INDIGO_UTILS_DIR}/Array2D.h
${INDIGO_UTILS_DIR}/Array3D.h
${INDIGO_UTILS_DIR}/Array4D.cpp
${INDIGO_UTILS_DIR}/Array4D.h
${INDIGO_UTILS_DIR}/Base64.cpp
${INDIGO_UTILS_DIR}/Base64.h
${INDIGO_UTILS_DIR}/BitField.cpp
${INDIGO_UTILS_DIR}/BitField.h
${INDIGO_UTILS_DIR}/BitUtils.cpp
${INDIGO_UTILS_DIR}/BitUtils.h
${INDIGO_UTILS_DIR}/BufferInStream.cpp
${INDIGO_UTILS_DIR}/BufferInStream.h
${INDIGO_UTILS_DIR}/BufferOutStream.cpp
${INDIGO_UTILS_DIR}/BufferOutStream.h
${INDIGO_UTILS_DIR}/CameraController.cpp
${INDIGO_UTILS_DIR}/CameraController.h
${INDIGO_UTILS_DIR}/CheckDLLSignature.cpp
${INDIGO_UTILS_DIR}/CheckDLLSignature.h
${INDIGO_UTILS_DIR}/Checksum.cpp
${INDIGO_UTILS_DIR}/Checksum.h
${INDIGO_UTILS_DIR}/CircularBuffer.cpp
${INDIGO_UTILS_DIR}/CircularBuffer.h
${INDIGO_UTILS_DIR}/Clock.cpp
${INDIGO_UTILS_DIR}/Clock.h
${INDIGO_UTILS_DIR}/Compression.cpp
${INDIGO_UTILS_DIR}/Compression.h
${INDIGO_UTILS_DIR}/Condition.cpp
${INDIGO_UTILS_DIR}/Condition.h
${INDIGO_UTILS_DIR}/ConPrint.cpp
${INDIGO_UTILS_DIR}/ConPrint.h
${INDIGO_UTILS_DIR}/ContainerUtils.h
${INDIGO_UTILS_DIR}/CycleTimer.cpp
${INDIGO_UTILS_DIR}/CycleTimer.h
${INDIGO_UTILS_DIR}/DeltaSigmaSampler.cpp
${INDIGO_UTILS_DIR}/DeltaSigmaSampler.h
${INDIGO_UTILS_DIR}/DynamicLib.cpp
${INDIGO_UTILS_DIR}/DynamicLib.h
${INDIGO_UTILS_DIR}/EventFD.cpp
${INDIGO_UTILS_DIR}/EventFD.h
${INDIGO_UTILS_DIR}/Exception.cpp
${INDIGO_UTILS_DIR}/Exception.h
${INDIGO_UTILS_DIR}/FileChecksum.cpp
${INDIGO_UTILS_DIR}/FileChecksum.h
${INDIGO_UTILS_DIR}/FileHandle.cpp
${INDIGO_UTILS_DIR}/FileHandle.h
${INDIGO_UTILS_DIR}/FileInStream.cpp
${INDIGO_UTILS_DIR}/FileInStream.h
${INDIGO_UTILS_DIR}/FileOutStream.cpp
${INDIGO_UTILS_DIR}/FileOutStream.h
${INDIGO_UTILS_DIR}/FileUtils.cpp
${INDIGO_UTILS_DIR}/FileUtils.h
${INDIGO_UTILS_DIR}/Hilbert.cpp
${INDIGO_UTILS_DIR}/Hilbert.h
${INDIGO_UTILS_DIR}/Icosahedron.h
${INDIGO_UTILS_DIR}/ImmutableVector.cpp
${INDIGO_UTILS_DIR}/ImmutableVector.h
${INDIGO_UTILS_DIR}/IncludeWindows.h
${INDIGO_UTILS_DIR}/IndigoAtomic.cpp
${INDIGO_UTILS_DIR}/IndigoAtomic.h
${INDIGO_UTILS_DIR}/IndigoXMLDoc.cpp
${INDIGO_UTILS_DIR}/IndigoXMLDoc.h
${INDIGO_UTILS_DIR}/InStream.cpp
${INDIGO_UTILS_DIR}/InStream.h
${INDIGO_UTILS_DIR}/InterpolatedTable.cpp
${INDIGO_UTILS_DIR}/InterpolatedTable.h
${INDIGO_UTILS_DIR}/InterpolatedTable1D.cpp
${INDIGO_UTILS_DIR}/InterpolatedTable1D.h
${INDIGO_UTILS_DIR}/InterpolatedTable3D.cpp
${INDIGO_UTILS_DIR}/InterpolatedTable3D.h
${INDIGO_UTILS_DIR}/KillThreadMessage.cpp
${INDIGO_UTILS_DIR}/KillThreadMessage.h
${INDIGO_UTILS_DIR}/License.cpp
${INDIGO_UTILS_DIR}/License.h
${INDIGO_UTILS_DIR}/Lock.cpp
${INDIGO_UTILS_DIR}/Lock.h
${INDIGO_UTILS_DIR}/Maybe.h
${INDIGO_UTILS_DIR}/MemMappedFile.cpp
${INDIGO_UTILS_DIR}/MemMappedFile.h
${INDIGO_UTILS_DIR}/MessageableThread.cpp
${INDIGO_UTILS_DIR}/MessageableThread.h
${INDIGO_UTILS_DIR}/MiniDump.cpp
${INDIGO_UTILS_DIR}/MiniDump.h
${INDIGO_UTILS_DIR}/MTwister.cpp
${INDIGO_UTILS_DIR}/MTwister.h
${INDIGO_UTILS_DIR}/Mutex.cpp
${INDIGO_UTILS_DIR}/Mutex.h
${INDIGO_UTILS_DIR}/MyThread.cpp
${INDIGO_UTILS_DIR}/MyThread.h
${INDIGO_UTILS_DIR}/NameMap.cpp
${INDIGO_UTILS_DIR}/NameMap.h
${INDIGO_UTILS_DIR}/Numeric.cpp
${INDIGO_UTILS_DIR}/Numeric.h
${INDIGO_UTILS_DIR}/Obfuscator.cpp
${INDIGO_UTILS_DIR}/Obfuscator.h
${INDIGO_UTILS_DIR}/OutStream.cpp
${INDIGO_UTILS_DIR}/OutStream.h
${INDIGO_UTILS_DIR}/OpenSSL.cpp
${INDIGO_UTILS_DIR}/OpenSSL.h
${INDIGO_UTILS_DIR}/PackedVector.h
${INDIGO_UTILS_DIR}/ParallelFor.cpp
${INDIGO_UTILS_DIR}/ParallelFor.h
${INDIGO_UTILS_DIR}/ParkMillerCartaRNG.cpp
${INDIGO_UTILS_DIR}/ParkMillerCartaRNG.h
${INDIGO_UTILS_DIR}/Parser.cpp
${INDIGO_UTILS_DIR}/Parser.h
${INDIGO_UTILS_DIR}/ParseUtils.cpp
${INDIGO_UTILS_DIR}/ParseUtils.h
${INDIGO_UTILS_DIR}/PauseThreadMessage.cpp
${INDIGO_UTILS_DIR}/PauseThreadMessage.h
${INDIGO_UTILS_DIR}/Platform.h
${INDIGO_UTILS_DIR}/PlatformUtils.cpp
${INDIGO_UTILS_DIR}/PlatformUtils.h
${INDIGO_UTILS_DIR}/Plotter.cpp
${INDIGO_UTILS_DIR}/Plotter.h
${INDIGO_UTILS_DIR}/prebuild_repos_info.h
${INDIGO_UTILS_DIR}/RefCounted.cpp
${INDIGO_UTILS_DIR}/RefCounted.h
${INDIGO_UTILS_DIR}/Reference.cpp
${INDIGO_UTILS_DIR}/Reference.h
${INDIGO_UTILS_DIR}/ReferenceTest.cpp
${INDIGO_UTILS_DIR}/ReferenceTest.h
${INDIGO_UTILS_DIR}/RunningAverage.cpp
${INDIGO_UTILS_DIR}/RunningAverage.h
${INDIGO_UTILS_DIR}/SHA256.cpp
${INDIGO_UTILS_DIR}/SHA256.h
${INDIGO_UTILS_DIR}/Singleton.cpp
${INDIGO_UTILS_DIR}/Singleton.h
${INDIGO_UTILS_DIR}/SmallVector.cpp
${INDIGO_UTILS_DIR}/SmallVector.h
${INDIGO_UTILS_DIR}/SocketBufferOutStream.cpp
${INDIGO_UTILS_DIR}/SocketBufferOutStream.h
${INDIGO_UTILS_DIR}/Sort.cpp
${INDIGO_UTILS_DIR}/Sort.h
${INDIGO_UTILS_DIR}/SphereUnitVecPool.cpp
${INDIGO_UTILS_DIR}/SphereUnitVecPool.h
${INDIGO_UTILS_DIR}/StreamShouldAbortCallback.cpp
${INDIGO_UTILS_DIR}/StreamShouldAbortCallback.h
${INDIGO_UTILS_DIR}/StreamUtils.cpp
${INDIGO_UTILS_DIR}/StreamUtils.h
${INDIGO_UTILS_DIR}/StringUtils.cpp
${INDIGO_UTILS_DIR}/StringUtils.h
${INDIGO_UTILS_DIR}/string_view.cpp
${INDIGO_UTILS_DIR}/string_view.h
${INDIGO_UTILS_DIR}/SystemInfo.cpp
${INDIGO_UTILS_DIR}/SystemInfo.h
${INDIGO_UTILS_DIR}/Task.cpp
${INDIGO_UTILS_DIR}/Task.h
${INDIGO_UTILS_DIR}/TaskManager.cpp
${INDIGO_UTILS_DIR}/TaskManager.h
${INDIGO_UTILS_DIR}/TaskRunnerThread.cpp
${INDIGO_UTILS_DIR}/TaskRunnerThread.h
${INDIGO_UTILS_DIR}/TaskTests.cpp
${INDIGO_UTILS_DIR}/TaskTests.h
${INDIGO_UTILS_DIR}/ThreadManager.cpp
${INDIGO_UTILS_DIR}/ThreadManager.h
${INDIGO_UTILS_DIR}/ThreadMessage.cpp
${INDIGO_UTILS_DIR}/ThreadMessage.h
${INDIGO_UTILS_DIR}/ThreadMessageSink.cpp
${INDIGO_UTILS_DIR}/ThreadMessageSink.h
${INDIGO_UTILS_DIR}/ThreadSafeQueue.h
${INDIGO_UTILS_DIR}/ThreadSafeRefCounted.cpp
${INDIGO_UTILS_DIR}/ThreadSafeRefCounted.h
${INDIGO_UTILS_DIR}/ThreadShouldAbortCallback.cpp
${INDIGO_UTILS_DIR}/ThreadShouldAbortCallback.h
${INDIGO_UTILS_DIR}/ThreadTerminatingMessage.cpp
${INDIGO_UTILS_DIR}/ThreadTerminatingMessage.h
${INDIGO_UTILS_DIR}/ThreadTests.cpp
${INDIGO_UTILS_DIR}/ThreadTests.h
${INDIGO_UTILS_DIR}/Timer.cpp
${INDIGO_UTILS_DIR}/Timer.h
${INDIGO_UTILS_DIR}/Transmungify.cpp
${INDIGO_UTILS_DIR}/Transmungify.h
${INDIGO_UTILS_DIR}/UTF8Utils.cpp
${INDIGO_UTILS_DIR}/UTF8Utils.h
${INDIGO_UTILS_DIR}/UtilTests.cpp
${INDIGO_UTILS_DIR}/UtilTests.h
${INDIGO_UTILS_DIR}/Vector.cpp
${INDIGO_UTILS_DIR}/Vector.h
${INDIGO_UTILS_DIR}/VectorUnitTests.cpp
${INDIGO_UTILS_DIR}/VectorUnitTests.h
${INDIGO_UTILS_DIR}/VRef.h
${INDIGO_UTILS_DIR}/X509Certificate.cpp
${INDIGO_UTILS_DIR}/X509Certificate.h
)


set(INDIGO_PHYSICS_DIR "${INDIGO_TRUNK_DIR_ENV}/physics")
set(physics
${INDIGO_PHYSICS_DIR}/BVH.cpp
${INDIGO_PHYSICS_DIR}/BVH.h
${INDIGO_PHYSICS_DIR}/BVHBuilder.cpp
${INDIGO_PHYSICS_DIR}/BVHBuilder.h
${INDIGO_PHYSICS_DIR}/BVHBuilderTests.cpp
${INDIGO_PHYSICS_DIR}/BVHBuilderTests.h
${INDIGO_PHYSICS_DIR}/BVHImpl.cpp
${INDIGO_PHYSICS_DIR}/BVHImpl.h
${INDIGO_PHYSICS_DIR}/BVHNode.cpp
${INDIGO_PHYSICS_DIR}/BVHNode.h
#${INDIGO_PHYSICS_DIR}/BVHObjectTree.cpp
#${INDIGO_PHYSICS_DIR}/BVHObjectTree.h
${INDIGO_PHYSICS_DIR}/BVH_Notes.txt
#${INDIGO_PHYSICS_DIR}/FastKDTreeBuilder.cpp
#${INDIGO_PHYSICS_DIR}/FastKDTreeBuilder.h
${INDIGO_PHYSICS_DIR}/HashedGrid.cpp
${INDIGO_PHYSICS_DIR}/HashedGrid.h
${INDIGO_PHYSICS_DIR}/HashedGridTests.cpp
${INDIGO_PHYSICS_DIR}/HashedGridTests.h
${INDIGO_PHYSICS_DIR}/jscol_aabbox.cpp
${INDIGO_PHYSICS_DIR}/jscol_aabbox.h
${INDIGO_PHYSICS_DIR}/jscol_BadouelTri.cpp
${INDIGO_PHYSICS_DIR}/jscol_BadouelTri.h
${INDIGO_PHYSICS_DIR}/jscol_BIHTree.cpp
${INDIGO_PHYSICS_DIR}/jscol_BIHTree.h
${INDIGO_PHYSICS_DIR}/jscol_BIHTreeNode.cpp
${INDIGO_PHYSICS_DIR}/jscol_BIHTreeNode.h
${INDIGO_PHYSICS_DIR}/jscol_boundingsphere.h
#${INDIGO_PHYSICS_DIR}/jscol_ObjectTree.cpp
#${INDIGO_PHYSICS_DIR}/jscol_ObjectTree.h
${INDIGO_PHYSICS_DIR}/jscol_ObjectTreePerThreadData.cpp
${INDIGO_PHYSICS_DIR}/jscol_ObjectTreePerThreadData.h
${INDIGO_PHYSICS_DIR}/jscol_StackFrame.cpp
${INDIGO_PHYSICS_DIR}/jscol_StackFrame.h
${INDIGO_PHYSICS_DIR}/jscol_Tree.cpp
${INDIGO_PHYSICS_DIR}/jscol_Tree.h
${INDIGO_PHYSICS_DIR}/jscol_triangle.cpp
${INDIGO_PHYSICS_DIR}/jscol_triangle.h
${INDIGO_PHYSICS_DIR}/jscol_TriHash.cpp
${INDIGO_PHYSICS_DIR}/jscol_TriHash.h
${INDIGO_PHYSICS_DIR}/jscol_trimesh.h
${INDIGO_PHYSICS_DIR}/jscol_TriTreePerThreadData.cpp
${INDIGO_PHYSICS_DIR}/jscol_TriTreePerThreadData.h
${INDIGO_PHYSICS_DIR}/KDTree.cpp
${INDIGO_PHYSICS_DIR}/KDTree.h
${INDIGO_PHYSICS_DIR}/KDTreeImpl.cpp
${INDIGO_PHYSICS_DIR}/KDTreeImpl.h
${INDIGO_PHYSICS_DIR}/KDTreeNode.cpp
${INDIGO_PHYSICS_DIR}/KDTreeNode.h
${INDIGO_PHYSICS_DIR}/MollerTrumboreTri.cpp
${INDIGO_PHYSICS_DIR}/MollerTrumboreTri.h
${INDIGO_PHYSICS_DIR}/MollerTrumboreTriNotes.txt
${INDIGO_PHYSICS_DIR}/MultiLevelGrid.cpp
${INDIGO_PHYSICS_DIR}/MultiLevelGrid.h
${INDIGO_PHYSICS_DIR}/MultiLevelGridBuilder.cpp
${INDIGO_PHYSICS_DIR}/MultiLevelGridBuilder.h
${INDIGO_PHYSICS_DIR}/MultiLevelGridNode.cpp
${INDIGO_PHYSICS_DIR}/MultiLevelGridNode.h
${INDIGO_PHYSICS_DIR}/MultiLevelGridTests.cpp
${INDIGO_PHYSICS_DIR}/MultiLevelGridTests.h
${INDIGO_PHYSICS_DIR}/NBVH.cpp
${INDIGO_PHYSICS_DIR}/NBVH.h
${INDIGO_PHYSICS_DIR}/NBVHNode.cpp
${INDIGO_PHYSICS_DIR}/NBVHNode.h
${INDIGO_PHYSICS_DIR}/NLogNKDTreeBuilder.cpp
${INDIGO_PHYSICS_DIR}/NLogNKDTreeBuilder.h
${INDIGO_PHYSICS_DIR}/ObjectMLG.cpp
${INDIGO_PHYSICS_DIR}/ObjectMLG.h
${INDIGO_PHYSICS_DIR}/ObjectTreeNode.cpp
${INDIGO_PHYSICS_DIR}/ObjectTreeNode.h
#${INDIGO_PHYSICS_DIR}/ObjectTreeTest.cpp
#${INDIGO_PHYSICS_DIR}/ObjectTreeTest.h
${INDIGO_PHYSICS_DIR}/OldKDTreeBuilder.cpp
${INDIGO_PHYSICS_DIR}/OldKDTreeBuilder.h
${INDIGO_PHYSICS_DIR}/PointKDTree.cpp
${INDIGO_PHYSICS_DIR}/PointKDTree.h
${INDIGO_PHYSICS_DIR}/PointKDTreeNode.cpp
${INDIGO_PHYSICS_DIR}/PointKDTreeNode.h
${INDIGO_PHYSICS_DIR}/PointMLG.cpp
${INDIGO_PHYSICS_DIR}/PointMLG.h
${INDIGO_PHYSICS_DIR}/PointTreeTest.cpp
${INDIGO_PHYSICS_DIR}/PointTreeTest.h
${INDIGO_PHYSICS_DIR}/SimpleBVH.cpp_
${INDIGO_PHYSICS_DIR}/SimpleBVH.h
${INDIGO_PHYSICS_DIR}/SimpleBVHNode.cpp
${INDIGO_PHYSICS_DIR}/SimpleBVHNode.h
${INDIGO_PHYSICS_DIR}/SimpleBVHNotes.txt
${INDIGO_PHYSICS_DIR}/ThreadedKDTreeBuilder.cpp_
${INDIGO_PHYSICS_DIR}/ThreadedKDTreeBuilder.h
${INDIGO_PHYSICS_DIR}/ThreadedNLogNKDTreeBuilder.cpp
${INDIGO_PHYSICS_DIR}/ThreadedNLogNKDTreeBuilder.h
#${INDIGO_PHYSICS_DIR}/TreeTest.cpp
#${INDIGO_PHYSICS_DIR}/TreeTest.h
${INDIGO_PHYSICS_DIR}/TreeUtils.cpp
${INDIGO_PHYSICS_DIR}/TreeUtils.h
${INDIGO_PHYSICS_DIR}/TriangleTest.cpp
${INDIGO_PHYSICS_DIR}/TriangleTest.h
)



set(raytracing 
"${INDIGO_TRUNK_DIR_ENV}/raytracing/hitinfo.cpp"
"${INDIGO_TRUNK_DIR_ENV}/raytracing/hitinfo.h"
)

#if(WIN32) # Need .rc file on windows
#	FILE(GLOB indigo_src "${INDIGO_TRUNK_DIR_ENV}/indigo/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/indigo/*.h" "${INDIGO_TRUNK_DIR_ENV}/indigo/*.txt" "${INDIGO_TRUNK_DIR_ENV}/indigo/version.rc")
#else()
#	FILE(GLOB indigo_src "${INDIGO_TRUNK_DIR_ENV}/indigo/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/indigo/*.h" "${INDIGO_TRUNK_DIR_ENV}/indigo/*.txt")
#endif()
FILE(GLOB lang "${INDIGO_TRUNK_DIR_ENV}/lang/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/lang/*.h" "${INDIGO_TRUNK_DIR_ENV}/lang/*.txt")
FILE(GLOB maths "${INDIGO_TRUNK_DIR_ENV}/maths/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/maths/*.h")
FILE(GLOB networking "${INDIGO_TRUNK_DIR_ENV}/networking/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/networking/*.h")
FILE(GLOB opencl "${INDIGO_TRUNK_DIR_ENV}/opencl/*.cl" "${INDIGO_TRUNK_DIR_ENV}/opencl/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/opencl/*.h")
#FILE(GLOB physics "${INDIGO_TRUNK_DIR_ENV}/physics/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/physics/*.h")
#FILE(GLOB raytracing "${INDIGO_TRUNK_DIR_ENV}/raytracing/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/raytracing/*.h")
FILE(GLOB hdr "${INDIGO_TRUNK_DIR_ENV}/hdr/*.c" "${INDIGO_TRUNK_DIR_ENV}/hdr/*.h")
FILE(GLOB simpleraytracer "${INDIGO_TRUNK_DIR_ENV}/simpleraytracer/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/simpleraytracer/*.h")
#FILE(GLOB utils "${INDIGO_TRUNK_DIR_ENV}/utils/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/utils/*.h")
FILE(GLOB scripts "../scripts/*.rb")
FILE(GLOB double_conversion "${INDIGO_TRUNK_DIR_ENV}/double-conversion/*.cc" "${INDIGO_TRUNK_DIR_ENV}/double-conversion/*.h")
FILE(GLOB xxhash "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.c"  "${INDIGO_TRUNK_DIR_ENV}/xxHash-r39/*.h")

set(fft2d "../libs/fft2d/fft4f2d.c")

set(opengl 
${INDIGO_TRUNK_DIR_ENV}/opengl/gl3w.c 
${INDIGO_TRUNK_DIR_ENV}/opengl/VAO.cpp 
${INDIGO_TRUNK_DIR_ENV}/opengl/VAO.h
${INDIGO_TRUNK_DIR_ENV}/opengl/VBO.cpp 
${INDIGO_TRUNK_DIR_ENV}/opengl/VBO.h
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLTexture.cpp 
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLTexture.h
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLEngine.cpp 
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLEngine.h
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLProgram.cpp 
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLProgram.h
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLShader.cpp 
${INDIGO_TRUNK_DIR_ENV}/opengl/OpenGLShader.h
)

set(opengl_shaders
${INDIGO_TRUNK_DIR_ENV}/opengl/shaders/env_frag_shader.glsl
${INDIGO_TRUNK_DIR_ENV}/opengl/shaders/env_vert_shader.glsl
${INDIGO_TRUNK_DIR_ENV}/opengl/shaders/phong_frag_shader.glsl
${INDIGO_TRUNK_DIR_ENV}/opengl/shaders/phong_vert_shader.glsl
${INDIGO_TRUNK_DIR_ENV}/opengl/shaders/transparent_frag_shader.glsl
${INDIGO_TRUNK_DIR_ENV}/opengl/shaders/transparent_vert_shader.glsl
)

set(INDIGO_SRC_DIR "${INDIGO_TRUNK_DIR_ENV}/indigo")

set(indigo_src
${INDIGO_SRC_DIR}/DisplacementUtils.cpp
${INDIGO_SRC_DIR}/DisplacementUtils.h
#${INDIGO_SRC_DIR}/object.cpp
#${INDIGO_SRC_DIR}/object.h
${INDIGO_SRC_DIR}/StandardPrintOutput.cpp
${INDIGO_SRC_DIR}/TestUtils.cpp
${INDIGO_SRC_DIR}/TestUtils.h
${INDIGO_SRC_DIR}/ThreadContext.cpp
${INDIGO_SRC_DIR}/ThreadContext.h
)

set(INDIGO_DLL_DIR "${INDIGO_TRUNK_DIR_ENV}/dll")

set(dll_src
${INDIGO_DLL_DIR}/IndigoMesh.cpp
${INDIGO_DLL_DIR}/include/IndigoMesh.h
)


SOURCE_GROUP(graphics FILES ${graphics})
SOURCE_GROUP(indigo FILES ${indigo_src})
SOURCE_GROUP(lang FILES ${lang})
SOURCE_GROUP(maths FILES ${maths})
SOURCE_GROUP(networking FILES ${networking})
SOURCE_GROUP(physics FILES ${physics})
SOURCE_GROUP(raytracing FILES ${raytracing})
SOURCE_GROUP(hdr FILES ${hdr})
SOURCE_GROUP(simpleraytracer FILES ${simpleraytracer})
SOURCE_GROUP(utils FILES ${utils})
SOURCE_GROUP(scripts FILES ${scripts})
SOURCE_GROUP(double_conversion FILES ${double_conversion})
SOURCE_GROUP(opengl FILES ${opengl})
SOURCE_GROUP(opengl\\shaders FILES ${opengl_shaders})
SOURCE_GROUP(dll FILES ${dll_src})
SOURCE_GROUP(fft2d FILES ${fft2d})
SOURCE_GROUP(xxhash FILES ${xxhash})
