
Substrata is an open-source metaverse, developed by Glare Technologies Limited, see https://substrata.info/.

You can build the Substrata client or Substrata server from this repository.

![main screenshot](https://github.com/glaretechnologies/substrata/assets/30285/38a6db93-e729-4f15-8561-5c848eb5391c)


## Get Involved

We welcome contributions from people!

Chat about Substrata on the Substrata discord here: https://discord.gg/3Ds9cxyEnZ

Feel free to drop a message on the discord if you are having trouble building Substrata, or have any questions about it.

## Building

See [docs/building.txt](docs/building.txt) for build instructions.

## Features

### High performance, physically-based rendering engine

Substrata uses the Glare engine (https://github.com/glaretechnologies/glare-core), which produces realistic graphics while rendering the entire Substrata world - e.g. over 12000 objects with user-generated content at 200 fps.

* The Glare engine is designed for metaverses, in particular large numbers of varied objects.
* Automatic level of detail generation
* Streaming loading and unloading of objects without hitches as the player moves around
* Physically-based rendering
* Highly realistic sun/sky/daylight system derived from a multiple scattering ray-tracing atmospheric simulation in https://www.indigorenderer.com/
* Skeletal animation system with procedural animations and animation retargetting for sharing animation data amongst avatars with varied sizes
* Runtime texture compression for making best use of GPU memory
* GLTF, OBJ import, plus supports many image formats
* Terrain and water rendering
* Particle system for rendering dust, water splashes, smoke etc.

![boat](https://github.com/glaretechnologies/substrata/assets/30285/0dde612a-ea95-49af-bc64-07f1a7114c7f)


### Networked physics simulation

We have integrated the Jolt physics engine (https://github.com/jrouwe/JoltPhysics), and have implemented a networked physics simulation on top of it. 
What that means is that multiple players can interact with objects in a world, drive vehicles, push objects etc. in a realistic way.

Physics-based vehicles: 

<a href="https://www.youtube.com/watch?v=Hc79sDi3f0U"><img src="https://github.com/glaretechnologies/substrata/assets/30285/18703540-58ae-4e18-bf28-635784cd6c9a" width="600"></a>

Networked physics:

<a href="https://www.youtube.com/watch?v=Hc79sDi3f0U"><img src="https://github.com/glaretechnologies/substrata/assets/30285/37eaacef-0f1b-48af-a820-1dcc9c17466e" width="600"></a>


Handling lots of interactive objects:

<a href="https://youtu.be/CzGz6voUE_8"><img src="https://github.com/glaretechnologies/substrata/assets/30285/6956d5a7-33f4-4c79-947c-951a2fe3cb18" width="600"></a>


### Spatial Audio and Voice Chat

Substrata has built-in spatial audio and voice chat, without using any third-party services or servers.


### In-world building

The substrata client has controls for creating and editing objects, as well as for editing voxels

<img src="https://github.com/glaretechnologies/substrata/assets/30285/6956d5a7-33f4-4c79-947c-951a2fe3cb18" width="600">


<img src="https://github.com/glaretechnologies/substrata/assets/30285/3e3fb2f5-de3a-4132-9b86-b275b89c5dbd" width="600">

You can add objects to the world from your local machine, and they will be automatically uploaded to the server and be visible to other users.


