/*=====================================================================
AnimationManager.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "AnimationManager.h"


#include "../graphics/AnimationData.h"
#include "../shared/ResourceManager.h"
#include "../shared/Resource.h"
#include <utils/PlatformUtils.h>
#include <utils/FileInStream.h>





AnimationManager::AnimationManager()
{
#if !defined(EMSCRIPTEN)
	main_thread_id = PlatformUtils::getCurrentThreadID();
#endif
}


AnimationManager::~AnimationManager()
{
	checkRunningOnMainThread();

	clear();
}


void AnimationManager::clear()
{
	checkRunningOnMainThread();

	animations.clear();
}


bool AnimationManager::isAnimationPresent(const URLString& anim_url) const
{
	checkRunningOnMainThread();

	return animations.find(anim_url) != animations.end();
}


Reference<AnimationData> AnimationManager::getAnimationIfPresent(const URLString& anim_url, ResourceManager& resource_manager)
{
	checkRunningOnMainThread();

	auto res = animations.find(anim_url);
	if(res != animations.end())
	{
		return res->second;
	}
	else
	{
		ResourceRef anim_resource = resource_manager.getExistingResourceForURL(anim_url);
		if(anim_resource)
		{
			FileInStream file(resource_manager.getLocalAbsPathForResource(*anim_resource));

			// Read magic number
			char buf[4];
			file.readData(buf, 4); 
			// TODO: check
			if(buf[0] != 'S' || buf[1] != 'U' || buf[2] != 'B' || buf[3] != 'A')
				throw glare::Exception("Invalid magic number/string loading '" + toString(anim_url) + "'");

			Reference<AnimationData> anim = new AnimationData();
			anim->readFromStream(file);

			// Checks num anims = 1 (for prepareForMultipleUse())
			if(anim->animations.size() != 1)
				throw glare::Exception(".subanim file must have exactly one animation in it.");

			anim->prepareForMultipleUse(); // Copy keyframe_times and output_data from the AnimationData object to the AnimationDatum object, so it can be used by multiple different avatars

			// Insert into map
			animations[anim_url] = anim;

			return anim;
		}
		else
			return nullptr;
	}
}


Reference<AnimationData> AnimationManager::getAnimation(const URLString& anim_url, ResourceManager& resource_manager)
{
	checkRunningOnMainThread();

	Reference<AnimationData> anim = getAnimationIfPresent(anim_url, resource_manager);
	if(!anim)
		throw glare::Exception("Animation resource not found: '" + toString(anim_url) + "'");
	
	return anim;
}


std::string AnimationManager::getDiagnostics() const
{
	std::string msg;
	msg += "---AnimationManager Manager---\n";
	msg += "------------------------------\n";

	return msg;
}


inline void AnimationManager::checkRunningOnMainThread() const
{
#if !defined(EMSCRIPTEN)
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
#endif
}
