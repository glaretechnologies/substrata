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
${INDIGO_GRAPHICS_DIR}/ImageMapSpectral.cpp
${INDIGO_GRAPHICS_DIR}/ImageMapSpectral.h
${INDIGO_GRAPHICS_DIR}/ImageMapTests.cpp
${INDIGO_GRAPHICS_DIR}/ImageMapTests.h
${INDIGO_GRAPHICS_DIR}/ImagingPipeline.cpp
${INDIGO_GRAPHICS_DIR}/ImagingPipeline.h
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
${INDIGO_GRAPHICS_DIR}/VideoWriter.cpp
${INDIGO_GRAPHICS_DIR}/VideoWriter.h
${INDIGO_GRAPHICS_DIR}/Voronoi.cpp
${INDIGO_GRAPHICS_DIR}/Voronoi.h
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
FILE(GLOB physics "${INDIGO_TRUNK_DIR_ENV}/physics/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/physics/*.h")
FILE(GLOB raytracing "${INDIGO_TRUNK_DIR_ENV}/raytracing/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/raytracing/*.h")
FILE(GLOB hdr "${INDIGO_TRUNK_DIR_ENV}/hdr/*.c" "${INDIGO_TRUNK_DIR_ENV}/hdr/*.h")
FILE(GLOB simpleraytracer "${INDIGO_TRUNK_DIR_ENV}/simpleraytracer/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/simpleraytracer/*.h")
FILE(GLOB utils "${INDIGO_TRUNK_DIR_ENV}/utils/*.cpp" "${INDIGO_TRUNK_DIR_ENV}/utils/*.h")
FILE(GLOB scripts "../scripts/*.rb")
FILE(GLOB double_conversion "${INDIGO_TRUNK_DIR_ENV}/double-conversion/*.cc" "${INDIGO_TRUNK_DIR_ENV}/double-conversion/*.h")

set(opengl "${INDIGO_TRUNK_DIR_ENV}/opengl/glew.c" "${INDIGO_TRUNK_DIR_ENV}/opengl/VBO.cpp" "${INDIGO_TRUNK_DIR_ENV}/opengl/VBO.h")

set(INDIGO_SRC_DIR "${INDIGO_TRUNK_DIR_ENV}/indigo")

set(indigo_src
${INDIGO_SRC_DIR}/TestUtils.cpp
${INDIGO_SRC_DIR}/TestUtils.h
)

set(INDIGO_DLL_DIR "${INDIGO_TRUNK_DIR_ENV}/dll")

set(dll_src
${INDIGO_DLL_DIR}/IndigoMesh.cpp
${INDIGO_DLL_DIR}/IndigoMesh.h
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
SOURCE_GROUP(dll FILES ${dll_src})

