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
../webclient/proximityloader.ts
../webclient/loaditemqueue.ts
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

set(webclient_loader_files
../webclient/loader/buildBVH.ts
../webclient/loader/MeshBuilder.ts
../webclient/loader/MeshLoader.ts
../webclient/loader/MeshLoaderWorker.ts
../webclient/loader/message.ts
)


SOURCE_GROUP(webclient FILES ${webclient_root_files})
SOURCE_GROUP(webclient/maths FILES ${webclient_maths_files})
SOURCE_GROUP(webclient/physics FILES ${webclient_physics_files})
SOURCE_GROUP(webclient/loader FILES ${webclient_loader_files})

set(webclient_files 
${webclient_root_files} 
${webclient_maths_files} 
${webclient_physics_files}
${webclient_loader_files}
)
