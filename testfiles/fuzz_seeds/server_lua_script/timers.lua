local count = 0
local handle
local function onTimer()
	count += 1
	print('timer ' .. tostring(count))
	if count == 2 then destroyTimer(handle) end
	createTimer(function() print('one shot') end, 0.1, false)
end
handle = createTimer(onTimer, 0.1, true)
local h2 = createTimer(onTimer, 0.5, false)
destroyTimer(h2)
destroyTimer(h2)
destroyTimer(-1)
