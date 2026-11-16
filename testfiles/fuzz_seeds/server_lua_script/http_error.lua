local function done(response)
	print(tostring(response.response_code) .. response.response_message .. response.mime_type)
	local body = buffer.tostring(response.body_data)
	local ok, data = pcall(parseJSON, body)
	if ok and type(data) == 'table' then
		objectstorage.setItem('data', data)
		doHTTPGetRequestAsync('https://example.com/next', {}, done, function(err) end)
	end
end
local function failed(err)
	print(tostring(err.error_code) .. err.error_description)
end
doHTTPGetRequestAsync('https://example.com/data.json', { ['X-Test'] = 'value' }, done, failed)
doHTTPPostRequestAsync('https://example.com/post', '{"a": 1}', 'application/json', {}, done, failed)
