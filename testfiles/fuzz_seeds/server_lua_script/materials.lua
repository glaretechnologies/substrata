local n = this_object:getNumMaterials()
for i = 0, n - 1 do
	local mat = this_object:getMaterial(i)
	mat.roughness_val = 0.25
	mat.opacity_val = 0.5
	mat.colour = Vec3d(1, 0, 0)
	print(tostring(mat.roughness_val) .. tostring(mat.hologram) .. tostring(mat.double_sided))
end
this_object:getMaterial(n)
