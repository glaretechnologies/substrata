-- @resource door_open.mp3
this_object:playAudio()
print(tostring(this_object:isPlayingAudio()))
this_object.audio_source_url = 'sound.mp3'
this_object.audio_loop = true
function onUserUsedObject(av : Avatar, ob : Object)
	ob:playSound('door_open.mp3')
	ob:playSound('not_declared.mp3')
end
local idx = this_object:getAnimationIndex('walk')
this_object:startPlayingAnimation(0)
