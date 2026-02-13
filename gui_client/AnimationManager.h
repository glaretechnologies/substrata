/*=====================================================================
AnimationManager.h
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <utils/Reference.h>
#include <map>
#include <string>
struct AnimationData;
class ResourceManager;
class RandomAccessInStream;
struct LoadedBuffer;


/*=====================================================================
AnimationManager
----------------

In general all methods are called on the main thread.
=====================================================================*/
class AnimationManager
{
public:
	AnimationManager();
	~AnimationManager();

	void clear();

	bool isAnimationPresent(const URLString& anim_url) const;
	Reference<AnimationData> getAnimationIfPresent(const URLString& anim_url, ResourceManager& resource_manager); // returns null if not found
	Reference<AnimationData> getAnimation(const URLString& anim_url, ResourceManager& resource_manager); // Throws excep if not found

	void loadAnimFromBuffer(const URLString& anim_url, const Reference<LoadedBuffer>& loaded_buffer);

	std::string getDiagnostics() const;

private:
	Reference<AnimationData> loadAnimFromStream(RandomAccessInStream& stream, const URLString& anim_url);
	void checkRunningOnMainThread() const;

	std::map<URLString, Reference<AnimationData>> animations;

	uint64 main_thread_id;
};
