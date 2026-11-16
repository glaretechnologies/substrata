function onUserTouchedObject(av : Avatar, ob : Object)
	print('Avatar ' .. tostring(av.uid) .. ' touched object ' .. tostring(ob.uid) .. ' ' .. av.name)
end
function onUserUsedObject(av : Avatar, ob : Object)
	print(tostring(av.linear_velocity))
end
function onUserMovedNearToObject(av : Avatar, ob : Object) end
function onUserMovedAwayFromObject(av : Avatar, ob : Object) end
function onUserEnteredParcel(av : Avatar, ob : Object, parcel : Parcel)
	print(tostring(parcel.uid))
end
function onUserExitedParcel(av : Avatar, ob : Object, parcel : Parcel) end
function onChatMessage(av : Avatar, msg : string)
	print(msg)
end
