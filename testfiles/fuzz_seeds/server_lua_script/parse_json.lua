local x = parseJSON('{"a": [1, 2.5, "s", true, null, {"b": {}}], "c": -1e300}')
print(tostring(x.a[2]) .. tostring(x.c))
assert(parseJSON('123.456') == 123.456)
