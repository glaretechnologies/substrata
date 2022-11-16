# webclient

set(webclient_root_files
../webclient/cameraController.ts
../webclient/bmeshloading.ts
../webclient/bufferin.ts
../webclient/bufferout.ts
../webclient/client.html
../webclient/CMakeLists.txt
../webclient/downloadqueue.ts
../webclient/proximityloader.ts
../webclient/fzstd.js
../webclient/tsconfig.json
../webclient/voxelloading.ts
../webclient/webclient.ts
../webclient/avatar.ts
../webclient/worldmaterial.ts
../webclient/types.ts
../webclient/parcel.ts
../webclient/worldobject.ts
../webclient/utils.ts
)

set(webclient_maths_files
../webclient/maths/defs.ts
../webclient/maths/functions.ts
../webclient/maths/generators.ts
../webclient/maths/geometry.ts
../webclient/maths/plane.ts
../webclient/maths/triangle.ts
../webclient/maths/vec3.ts
)

set(webclient_physics_files
../webclient/physics/bvh.ts
../webclient/physics/caster.ts
../webclient/physics/debug.ts
../webclient/physics/ground.ts
../webclient/physics/player.ts
../webclient/physics/types.ts
../webclient/physics/world.ts
)

set(webclient_graphics_files
../webclient/graphics/loaderWorker.ts
../webclient/graphics/stb_dxt.wasm
../webclient/graphics/textureLoader.ts
../webclient/graphics/wasm/dxt_compress.c
../webclient/graphics/wasm/dxt_compress.h
../webclient/graphics/wasm/stb_dxt.c
../webclient/graphics/wasm/stb_dxt.h
../webclient/graphics/wasm/stb_dxt.js
)


SOURCE_GROUP(webclient FILES ${webclient_root_files})
SOURCE_GROUP(webclient/maths FILES ${webclient_maths_files})
SOURCE_GROUP(webclient/physics FILES ${webclient_physics_files})
SOURCE_GROUP(webclient/graphics FILES ${webclient_graphics_files})

set(webclient_files 
${webclient_root_files} 
${webclient_maths_files} 
${webclient_physics_files}
${webclient_graphics_files}
)
