include_directories("${winterdir}")

# Set variables that embed_winter.cmake requires
set(WINTER_DIR ${winterdir})
set(WINTER_LLVM_VERSION ${SUBSTRATA_LLVM_VERSION})
set(WINTER_LLVM_DIR ${llvmdir})
set(WINTER_USE_OPENCL FALSE)
set(WINTER_INCLUDE_TEST TRUE)

include("${winterdir}/embed_winter.cmake")

SOURCE_GROUP(winter FILES ${WINTER_FILES})
