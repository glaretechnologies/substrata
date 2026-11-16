-- Numbers where the object/avatar/material metamethods and addEventListener expect string keys.
local ob = getObjectForUID(124)
local av = nil
function onUserUsedObject(a, o) av = a end

pcall(function() return this_object[1] end)
pcall(function() this_object[1] = 2 end)
pcall(function() return ob[1.5] end)
pcall(function() ob[0] = 'x' end)
pcall(function() return this_object:getMaterial(0)[1] end)
pcall(function() this_object:getMaterial(0)[2] = 1 end)
pcall(function() addEventListener(1, 124, function() end) end)
pcall(function() addEventListener(2, 124, onUserUsedObject) end)
