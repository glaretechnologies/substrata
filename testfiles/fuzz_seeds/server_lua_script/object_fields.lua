local ob = this_object
local p = ob.pos
ob.pos = p + Vec3d(1, 2, 3)
ob.scale = Vec3d(2, 2, 2)
ob.axis = Vec3d(0, 0, 1)
ob.angle = 1.5
ob.model_url = 'model.bmesh'
ob.content = 'hello'
ob.target_url = 'https://example.com'
ob.collidable = false
ob.dynamic = true
ob.mass = 10
ob.friction = 0.5
ob.restitution = 0.2
ob.audio_volume = 0.7
print(tostring(ob.pos.x) .. tostring(ob.angle) .. ob.model_url)
