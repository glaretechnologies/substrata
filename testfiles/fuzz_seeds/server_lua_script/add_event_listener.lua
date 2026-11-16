local function touched(av : Avatar, ob : Object)
	print('touched ' .. tostring(ob.uid))
	addEventListener('onUserTouchedObject', 124, function(av, ob) print('inner') end)
end
addEventListener('onUserTouchedObject', 124, touched)
addEventListener('onUserTouchedObject', 124, touched)
addEventListener('onUserTouchedObject', 123, touched)
addEventListener('onUserEnteredVehicle', 124, function(av, vehicle)
	local v = av.vehicle_inside
	print(tostring(vehicle.uid))
end)
addEventListener('onUserExitedVehicle', 124, function(av, vehicle) end)
addEventListener('onChatMessage', 124, function(av, msg) print(msg) end)
