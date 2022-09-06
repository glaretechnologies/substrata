
set(INDIGO_GRAPHICS_DIR "${GLARE_CORE_TRUNK_DIR_ENV}/graphics")
set(graphics
${INDIGO_GRAPHICS_DIR}/AnimationData.cpp
${INDIGO_GRAPHICS_DIR}/AnimationData.h
${INDIGO_GRAPHICS_DIR}/BatchedMesh.cpp
${INDIGO_GRAPHICS_DIR}/BatchedMesh.h
${INDIGO_GRAPHICS_DIR}/BatchedMeshTests.cpp
${INDIGO_GRAPHICS_DIR}/BatchedMeshTests.h
${INDIGO_GRAPHICS_DIR}/bitmap.cpp
${INDIGO_GRAPHICS_DIR}/bitmap.h
#${INDIGO_GRAPHICS_DIR}/bmpdecoder.cpp
#${INDIGO_GRAPHICS_DIR}/bmpdecoder.h
${INDIGO_GRAPHICS_DIR}/BoxFilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/BoxFilterFunction.h
${INDIGO_GRAPHICS_DIR}/colour3.h
${INDIGO_GRAPHICS_DIR}/Colour3f.cpp
${INDIGO_GRAPHICS_DIR}/Colour4f.cpp
${INDIGO_GRAPHICS_DIR}/Colour4f.h
${INDIGO_GRAPHICS_DIR}/CompressedImage.cpp
${INDIGO_GRAPHICS_DIR}/CompressedImage.h
${INDIGO_GRAPHICS_DIR}/Drawing.cpp
${INDIGO_GRAPHICS_DIR}/Drawing.h
${INDIGO_GRAPHICS_DIR}/DXTCompression.cpp
${INDIGO_GRAPHICS_DIR}/DXTCompression.h
${INDIGO_GRAPHICS_DIR}/EXRDecoder.cpp
${INDIGO_GRAPHICS_DIR}/EXRDecoder.h
${INDIGO_GRAPHICS_DIR}/FilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/FilterFunction.h
${INDIGO_GRAPHICS_DIR}/FFTPlan.cpp
${INDIGO_GRAPHICS_DIR}/FFTPlan.h
#${INDIGO_GRAPHICS_DIR}/FloatDecoder.cpp
#${INDIGO_GRAPHICS_DIR}/FloatDecoder.h
${INDIGO_GRAPHICS_DIR}/FormatDecoderGLTF.cpp
${INDIGO_GRAPHICS_DIR}/FormatDecoderGLTF.h
${INDIGO_GRAPHICS_DIR}/formatdecoderobj.cpp
${INDIGO_GRAPHICS_DIR}/formatdecoderobj.h
${INDIGO_GRAPHICS_DIR}/FormatDecoderSTL.cpp
${INDIGO_GRAPHICS_DIR}/FormatDecoderSTL.h
${INDIGO_GRAPHICS_DIR}/FormatDecoderVox.cpp
${INDIGO_GRAPHICS_DIR}/FormatDecoderVox.h
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
${INDIGO_GRAPHICS_DIR}/KTXDecoder.cpp
${INDIGO_GRAPHICS_DIR}/KTXDecoder.h
${INDIGO_GRAPHICS_DIR}/Map2D.cpp
${INDIGO_GRAPHICS_DIR}/Map2D.h
${INDIGO_GRAPHICS_DIR}/MeshSimplification.cpp
${INDIGO_GRAPHICS_DIR}/MeshSimplification.h
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
#${INDIGO_GRAPHICS_DIR}/RGBEDecoder.cpp
#${INDIGO_GRAPHICS_DIR}/RGBEDecoder.h
${INDIGO_GRAPHICS_DIR}/SharpFilterFunction.cpp
${INDIGO_GRAPHICS_DIR}/SharpFilterFunction.h
${INDIGO_GRAPHICS_DIR}/spherehammersly.h
${INDIGO_GRAPHICS_DIR}/TextDrawer.cpp
${INDIGO_GRAPHICS_DIR}/TextDrawer.h
#${INDIGO_GRAPHICS_DIR}/tgadecoder.cpp
#${INDIGO_GRAPHICS_DIR}/tgadecoder.h
#${INDIGO_GRAPHICS_DIR}/TIFFDecoder.cpp
#${INDIGO_GRAPHICS_DIR}/TIFFDecoder.h
${INDIGO_GRAPHICS_DIR}/TriBoxIntersection.cpp
${INDIGO_GRAPHICS_DIR}/TriBoxIntersection.h
${INDIGO_GRAPHICS_DIR}/Voronoi.cpp
${INDIGO_GRAPHICS_DIR}/Voronoi.h
)

set(MESHOPTIMIZER_DIR "${GLARE_CORE_TRUNK_DIR_ENV}/meshoptimizer/src")

set(meshoptimizer
${MESHOPTIMIZER_DIR}/allocator.cpp
${MESHOPTIMIZER_DIR}/meshoptimizer.h
${MESHOPTIMIZER_DIR}/allocator.cpp
${MESHOPTIMIZER_DIR}/clusterizer.cpp
${MESHOPTIMIZER_DIR}/indexcodec.cpp
${MESHOPTIMIZER_DIR}/indexgenerator.cpp
${MESHOPTIMIZER_DIR}/overdrawanalyzer.cpp
${MESHOPTIMIZER_DIR}/overdrawoptimizer.cpp
${MESHOPTIMIZER_DIR}/simplifier.cpp
${MESHOPTIMIZER_DIR}/spatialorder.cpp
${MESHOPTIMIZER_DIR}/stripifier.cpp
${MESHOPTIMIZER_DIR}/vcacheanalyzer.cpp
${MESHOPTIMIZER_DIR}/vcacheoptimizer.cpp
${MESHOPTIMIZER_DIR}/vertexcodec.cpp
${MESHOPTIMIZER_DIR}/vertexfilter.cpp
${MESHOPTIMIZER_DIR}/vfetchanalyzer.cpp
${MESHOPTIMIZER_DIR}/vfetchoptimizer.cpp   
)



set(INDIGO_UTILS_DIR "${GLARE_CORE_TRUNK_DIR_ENV}/utils")
set(utils
${INDIGO_UTILS_DIR}/AESEncryption.cpp
${INDIGO_UTILS_DIR}/AESEncryption.h
${INDIGO_UTILS_DIR}/ArgumentParser.cpp
${INDIGO_UTILS_DIR}/ArgumentParser.h
${INDIGO_UTILS_DIR}/Array2D.h
${INDIGO_UTILS_DIR}/Array3D.h
${INDIGO_UTILS_DIR}/Base64.cpp
${INDIGO_UTILS_DIR}/Base64.h
${INDIGO_UTILS_DIR}/BestFitAllocator.cpp
${INDIGO_UTILS_DIR}/BestFitAllocator.h
${INDIGO_UTILS_DIR}/BitField.cpp
${INDIGO_UTILS_DIR}/BitField.h
${INDIGO_UTILS_DIR}/BitUtils.cpp
${INDIGO_UTILS_DIR}/BitUtils.h
${INDIGO_UTILS_DIR}/BufferInStream.cpp
${INDIGO_UTILS_DIR}/BufferInStream.h
${INDIGO_UTILS_DIR}/BufferOutStream.cpp
${INDIGO_UTILS_DIR}/BufferOutStream.h
${INDIGO_UTILS_DIR}/CircularBuffer.cpp
${INDIGO_UTILS_DIR}/CircularBuffer.h
${INDIGO_UTILS_DIR}/Checksum.cpp
${INDIGO_UTILS_DIR}/Checksum.h
${INDIGO_UTILS_DIR}/Clock.cpp
${INDIGO_UTILS_DIR}/Clock.h
${INDIGO_UTILS_DIR}/Condition.cpp
${INDIGO_UTILS_DIR}/Condition.h
${INDIGO_UTILS_DIR}/ConPrint.cpp
${INDIGO_UTILS_DIR}/ConPrint.h
${INDIGO_UTILS_DIR}/ContainerUtils.h
${INDIGO_UTILS_DIR}/CryptoRNG.cpp
${INDIGO_UTILS_DIR}/CryptoRNG.h
${INDIGO_UTILS_DIR}/CycleTimer.cpp
${INDIGO_UTILS_DIR}/CycleTimer.h
${INDIGO_UTILS_DIR}/EventFD.cpp
${INDIGO_UTILS_DIR}/EventFD.h
${INDIGO_UTILS_DIR}/Exception.cpp
${INDIGO_UTILS_DIR}/Exception.h
${INDIGO_UTILS_DIR}/FastIterMap.cpp
${INDIGO_UTILS_DIR}/FastIterMap.h
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
${INDIGO_UTILS_DIR}/IncludeWindows.h
${INDIGO_UTILS_DIR}/AtomicInt.cpp
${INDIGO_UTILS_DIR}/AtomicInt.h
${INDIGO_UTILS_DIR}/IndigoXMLDoc.cpp
${INDIGO_UTILS_DIR}/IndigoXMLDoc.h
${INDIGO_UTILS_DIR}/InStream.cpp
${INDIGO_UTILS_DIR}/InStream.h
${INDIGO_UTILS_DIR}/JSONParser.cpp
${INDIGO_UTILS_DIR}/JSONParser.h
${INDIGO_UTILS_DIR}/KillThreadMessage.cpp
${INDIGO_UTILS_DIR}/KillThreadMessage.h
${INDIGO_UTILS_DIR}/Keccak256.cpp
${INDIGO_UTILS_DIR}/Keccak256.h
${INDIGO_UTILS_DIR}/Lock.cpp
${INDIGO_UTILS_DIR}/Lock.h
${INDIGO_UTILS_DIR}/Maybe.h
${INDIGO_UTILS_DIR}/MemAlloc.cpp
${INDIGO_UTILS_DIR}/MemAlloc.h
${INDIGO_UTILS_DIR}/MemMappedFile.cpp
${INDIGO_UTILS_DIR}/MemMappedFile.h
${INDIGO_UTILS_DIR}/MessageableThread.cpp
${INDIGO_UTILS_DIR}/MessageableThread.h
${INDIGO_UTILS_DIR}/Mutex.cpp
${INDIGO_UTILS_DIR}/Mutex.h
${INDIGO_UTILS_DIR}/MyThread.cpp
${INDIGO_UTILS_DIR}/MyThread.h
${INDIGO_UTILS_DIR}/NameMap.cpp
${INDIGO_UTILS_DIR}/NameMap.h
${INDIGO_UTILS_DIR}/Numeric.cpp
${INDIGO_UTILS_DIR}/Numeric.h
${INDIGO_UTILS_DIR}/OutStream.cpp
${INDIGO_UTILS_DIR}/OutStream.h
${INDIGO_UTILS_DIR}/OpenSSL.cpp
${INDIGO_UTILS_DIR}/OpenSSL.h
${INDIGO_UTILS_DIR}/Parser.cpp
${INDIGO_UTILS_DIR}/Parser.h
${INDIGO_UTILS_DIR}/Platform.h
${INDIGO_UTILS_DIR}/PlatformUtils.cpp
${INDIGO_UTILS_DIR}/PlatformUtils.h
${INDIGO_UTILS_DIR}/Plotter.cpp
${INDIGO_UTILS_DIR}/Plotter.h
${INDIGO_UTILS_DIR}/PoolAllocator.cpp
${INDIGO_UTILS_DIR}/PoolAllocator.h
${INDIGO_UTILS_DIR}/PoolMap.cpp
${INDIGO_UTILS_DIR}/PoolMap.h
${INDIGO_UTILS_DIR}/GlareProcess.cpp
${INDIGO_UTILS_DIR}/GlareProcess.h
#${INDIGO_UTILS_DIR}/prebuild_repos_info.h
${INDIGO_UTILS_DIR}/RefCounted.h
${INDIGO_UTILS_DIR}/Reference.h
${INDIGO_UTILS_DIR}/ReferenceTest.cpp
${INDIGO_UTILS_DIR}/ReferenceTest.h
${INDIGO_UTILS_DIR}/SHA256.cpp
${INDIGO_UTILS_DIR}/SHA256.h
${INDIGO_UTILS_DIR}/Singleton.h
${INDIGO_UTILS_DIR}/SmallVector.cpp
${INDIGO_UTILS_DIR}/SmallVector.h
${INDIGO_UTILS_DIR}/SocketBufferOutStream.cpp
${INDIGO_UTILS_DIR}/SocketBufferOutStream.h
${INDIGO_UTILS_DIR}/Sort.cpp
${INDIGO_UTILS_DIR}/Sort.h
${INDIGO_UTILS_DIR}/StandardPrintOutput.cpp
${INDIGO_UTILS_DIR}/StandardPrintOutput.h
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
${INDIGO_UTILS_DIR}/ThreadSafeRefCounted.h
${INDIGO_UTILS_DIR}/ThreadTests.cpp
${INDIGO_UTILS_DIR}/ThreadTests.h
${INDIGO_UTILS_DIR}/Timer.cpp
${INDIGO_UTILS_DIR}/Timer.h
${INDIGO_UTILS_DIR}/UTF8Utils.cpp
${INDIGO_UTILS_DIR}/UTF8Utils.h
${INDIGO_UTILS_DIR}/Vector.cpp
${INDIGO_UTILS_DIR}/Vector.h
${INDIGO_UTILS_DIR}/VRef.h
${INDIGO_UTILS_DIR}/XMLParseUtils.cpp
${INDIGO_UTILS_DIR}/XMLParseUtils.h
${INDIGO_UTILS_DIR}/TestUtils.cpp
${INDIGO_UTILS_DIR}/TestUtils.h
${INDIGO_UTILS_DIR}/ManagerWithCache.cpp
${INDIGO_UTILS_DIR}/ManagerWithCache.h
${INDIGO_UTILS_DIR}/Database.cpp
${INDIGO_UTILS_DIR}/Database.h
${INDIGO_UTILS_DIR}/DatabaseTests.cpp
${INDIGO_UTILS_DIR}/DatabaseTests.h
${INDIGO_UTILS_DIR}/BufferViewInStream.cpp
${INDIGO_UTILS_DIR}/BufferViewInStream.h
${INDIGO_UTILS_DIR}/HashSet.cpp
${INDIGO_UTILS_DIR}/HashSet.h
${INDIGO_UTILS_DIR}/HashSetIterators.h
${INDIGO_UTILS_DIR}/HashMap.cpp
${INDIGO_UTILS_DIR}/HashMap.h
${INDIGO_UTILS_DIR}/HashMapIterators.h
${INDIGO_UTILS_DIR}/HashMapInsertOnly2.cpp
${INDIGO_UTILS_DIR}/HashMapInsertOnly2.h
${INDIGO_UTILS_DIR}/HashMapInsertOnly2Iterators.h
)


set(INDIGO_PHYSICS_DIR "${GLARE_CORE_TRUNK_DIR_ENV}/physics")
set(physics
${INDIGO_PHYSICS_DIR}/BVH.cpp
${INDIGO_PHYSICS_DIR}/BVH.h
${INDIGO_PHYSICS_DIR}/BVHBuilder.cpp
${INDIGO_PHYSICS_DIR}/BVHBuilder.h
${INDIGO_PHYSICS_DIR}/BVHBuilderTests.cpp
${INDIGO_PHYSICS_DIR}/BVHBuilderTests.h
${INDIGO_PHYSICS_DIR}/jscol_aabbox.cpp
${INDIGO_PHYSICS_DIR}/jscol_aabbox.h
${INDIGO_PHYSICS_DIR}/jscol_boundingsphere.h
${INDIGO_PHYSICS_DIR}/jscol_Tree.cpp
${INDIGO_PHYSICS_DIR}/jscol_Tree.h
${INDIGO_PHYSICS_DIR}/jscol_triangle.cpp
${INDIGO_PHYSICS_DIR}/jscol_triangle.h
${INDIGO_PHYSICS_DIR}/MollerTrumboreTri.cpp
${INDIGO_PHYSICS_DIR}/MollerTrumboreTri.h
${INDIGO_PHYSICS_DIR}/MollerTrumboreTriNotes.txt
${INDIGO_PHYSICS_DIR}/BinningBVHBuilder.cpp
${INDIGO_PHYSICS_DIR}/BinningBVHBuilder.h
${INDIGO_PHYSICS_DIR}/NonBinningBVHBuilder.cpp
${INDIGO_PHYSICS_DIR}/NonBinningBVHBuilder.h
${INDIGO_PHYSICS_DIR}/SBVHBuilder.cpp
${INDIGO_PHYSICS_DIR}/SBVHBuilder.h
${INDIGO_PHYSICS_DIR}/SmallBVH.cpp
${INDIGO_PHYSICS_DIR}/SmallBVH.h
)



set(raytracing 
"${GLARE_CORE_TRUNK_DIR_ENV}/raytracing/hitinfo.h"
)

#if(WIN32) # Need .rc file on windows
#	FILE(GLOB indigo_src "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/*.h" "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/*.txt" "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/version.rc")
#else()
#	FILE(GLOB indigo_src "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/*.h" "${GLARE_CORE_TRUNK_DIR_ENV}/indigo/*.txt")
#endif()
FILE(GLOB maths "${GLARE_CORE_TRUNK_DIR_ENV}/maths/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/maths/*.h")
FILE(GLOB networking "${GLARE_CORE_TRUNK_DIR_ENV}/networking/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/networking/*.h")
FILE(GLOB opencl "${GLARE_CORE_TRUNK_DIR_ENV}/opencl/*.cl" "${GLARE_CORE_TRUNK_DIR_ENV}/opencl/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/opencl/*.h")
#FILE(GLOB physics "${GLARE_CORE_TRUNK_DIR_ENV}/physics/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/physics/*.h")
#FILE(GLOB raytracing "${GLARE_CORE_TRUNK_DIR_ENV}/raytracing/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/raytracing/*.h")
#FILE(GLOB hdr "${GLARE_CORE_TRUNK_DIR_ENV}/hdr/*.c" "${GLARE_CORE_TRUNK_DIR_ENV}/hdr/*.h")
#FILE(GLOB simpleraytracer "${GLARE_CORE_TRUNK_DIR_ENV}/simpleraytracer/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/simpleraytracer/*.h")
#FILE(GLOB utils "${GLARE_CORE_TRUNK_DIR_ENV}/utils/*.cpp" "${GLARE_CORE_TRUNK_DIR_ENV}/utils/*.h")
FILE(GLOB scripts "../scripts/*.rb")
FILE(GLOB double_conversion "${GLARE_CORE_TRUNK_DIR_ENV}/double-conversion/*.cc" "${GLARE_CORE_TRUNK_DIR_ENV}/double-conversion/*.h")
FILE(GLOB xxhash "${GLARE_CORE_TRUNK_DIR_ENV}/xxHash-r39/*.c"  "${GLARE_CORE_TRUNK_DIR_ENV}/xxHash-r39/*.h")

set(lang
${GLARE_CORE_TRUNK_DIR_ENV}/lang/WinterEnv.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/lang/WinterEnv.h
)


set(simpleraytracer
${GLARE_CORE_TRUNK_DIR_ENV}/simpleraytracer/geometry.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/simpleraytracer/geometry.h
${GLARE_CORE_TRUNK_DIR_ENV}/simpleraytracer/raymesh.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/simpleraytracer/raymesh.h
)

set(fft2d "../libs/fft2d/fft4f2d.c")

set(opengl 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/gl3w.c 
#${GLARE_CORE_TRUNK_DIR_ENV}/opengl/EnvMapProcessing.cpp 
#${GLARE_CORE_TRUNK_DIR_ENV}/opengl/EnvMapProcessing.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/DrawIndirectBuffer.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/DrawIndirectBuffer.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/FrameBuffer.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/FrameBuffer.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/GLMeshBuilding.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/GLMeshBuilding.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/MeshPrimitiveBuilding.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/MeshPrimitiveBuilding.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/VAO.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/VAO.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/VBO.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/VBO.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/VertexBufferAllocator.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/VertexBufferAllocator.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLTexture.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLTexture.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLEngine.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLEngine.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLEngineTests.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLEngineTests.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLProgram.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLProgram.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLShader.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/OpenGLShader.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ShadowMapping.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ShadowMapping.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/SSBO.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/SSBO.h
#${GLARE_CORE_TRUNK_DIR_ENV}/opengl/TerrainSystem.cpp 
#${GLARE_CORE_TRUNK_DIR_ENV}/opengl/TerrainSystem.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/TextureLoading.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/TextureLoading.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/TextureLoadingTests.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/TextureLoadingTests.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/UniformBufOb.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/UniformBufOb.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/WGL.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/WGL.h
)

set(video 
${GLARE_CORE_TRUNK_DIR_ENV}/video/VideoReader.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/video/VideoReader.h
)
if(WIN32)
set(video 
${video}
${GLARE_CORE_TRUNK_DIR_ENV}/video/WMFVideoReader.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/video/WMFVideoReader.h
${GLARE_CORE_TRUNK_DIR_ENV}/video/WMFVideoReaderCallback.cpp
${GLARE_CORE_TRUNK_DIR_ENV}/video/WMFVideoReaderCallback.h
)
endif()



set(opengl_shaders
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/clear_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/clear_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/depth_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/depth_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/downsize_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/downsize_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/edge_extract_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/edge_extract_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/env_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/env_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/final_imaging_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/final_imaging_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/gaussian_blur_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/gaussian_blur_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/outline_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/outline_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/overlay_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/overlay_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/phong_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/phong_vert_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/transparent_frag_shader.glsl
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/transparent_vert_shader.glsl
#${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/water_frag_shader.glsl
#${GLARE_CORE_TRUNK_DIR_ENV}/opengl/shaders/water_vert_shader.glsl

../shaders/parcel_frag_shader.glsl
../shaders/parcel_vert_shader.glsl
../shaders/imposter_frag_shader.glsl
../shaders/imposter_vert_shader.glsl
)


set(opengl_ui
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUIButton.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUIButton.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUI.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUI.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUITextView.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUITextView.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUIWidget.cpp 
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUIWidget.h
${GLARE_CORE_TRUNK_DIR_ENV}/opengl/ui/GLUICallbackHandler.h
)


set(INDIGO_SRC_DIR "${GLARE_CORE_TRUNK_DIR_ENV}/indigo")

set(indigo_src
#${INDIGO_SRC_DIR}/DisplacementUtils.cpp
#${INDIGO_SRC_DIR}/DisplacementUtils.h
#${INDIGO_SRC_DIR}/object.cpp
#${INDIGO_SRC_DIR}/object.h
${INDIGO_SRC_DIR}/ThreadContext.cpp
${INDIGO_SRC_DIR}/ThreadContext.h
${INDIGO_SRC_DIR}/UVUnwrapper.cpp
${INDIGO_SRC_DIR}/UVUnwrapper.h
)

set(indigo_files_in_sdk_lib
#${INDIGO_SRC_DIR}/TransformPath.cpp
#${INDIGO_SRC_DIR}/TransformPath.h
)


set(INDIGO_DLL_DIR "${GLARE_CORE_TRUNK_DIR_ENV}/dll")

set(dll_src
${INDIGO_DLL_DIR}/IndigoMesh.cpp
${INDIGO_DLL_DIR}/include/IndigoMesh.h
${INDIGO_DLL_DIR}/IndigoAllocation.cpp
${INDIGO_DLL_DIR}/include/IndigoAllocation.h
)

include_directories(${INDIGO_DLL_DIR})
include_directories(${INDIGO_DLL_DIR}/include)


SOURCE_GROUP(graphics FILES ${graphics})
SOURCE_GROUP(indigo FILES ${indigo_src})
SOURCE_GROUP(lang FILES ${lang})
SOURCE_GROUP(maths FILES ${maths})
SOURCE_GROUP(networking FILES ${networking})
SOURCE_GROUP(physics FILES ${physics})
SOURCE_GROUP(raytracing FILES ${raytracing})
#SOURCE_GROUP(hdr FILES ${hdr})
SOURCE_GROUP(simpleraytracer FILES ${simpleraytracer})
SOURCE_GROUP(utils FILES ${utils})
SOURCE_GROUP(scripts FILES ${scripts})
SOURCE_GROUP(double_conversion FILES ${double_conversion})
SOURCE_GROUP(opengl FILES ${opengl})
SOURCE_GROUP(opengl\\shaders FILES ${opengl_shaders})
SOURCE_GROUP(opengl\\ui FILES ${opengl_ui})
SOURCE_GROUP(dll FILES ${dll_src})
SOURCE_GROUP(fft2d FILES ${fft2d})
SOURCE_GROUP(xxhash FILES ${xxhash})
SOURCE_GROUP(meshoptimizer FILES ${meshoptimizer})
