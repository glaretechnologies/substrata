# Copy some resource files, shaders etc. to the 'data' directory.
# This directory will be packaged up and used for preloading files into the Emscripten build.

require 'fileutils'


def printUsage()
	puts "Usage: make_emscripten_preload_data.rb substrata_src_dir"
end


if(ARGV.length < 1)
	printUsage()
	exit(1)
end

substrata_dir = ARGV[0]


glare_core = ENV['GLARE_CORE_TRUNK_DIR']
if glare_core.nil?
	puts "GLARE_CORE_TRUNK_DIR env var must be defined"
	exit(1)
end

puts "------------Using directories------------"
puts "glare_core: #{glare_core}"
puts "substrata_dir: #{substrata_dir}"
puts "-----------------------------------------"

FileUtils.rm_r("data", :verbose=>true) if File.exists?("data")
FileUtils.mkdir_p("data", :verbose => true)

FileUtils.cp_r(substrata_dir + "/resources", "data", :verbose=>true)
FileUtils.rm_r("data/resources/materials", :verbose=>true) # We don't need most of the materials

FileUtils.mkdir_p("data/resources/materials/white marble", :verbose => true)
FileUtils.cp(substrata_dir + "/resources/materials/white marble/carrara1.jpg", "data/resources/materials/white marble/carrara1.jpg", :verbose => true) # We need this marble texture for the Portals.

FileUtils.rm_r("data/resources/models", :verbose=>true)

# We just need foam_sprite_front.basis out of the sprites (used in TerrainDecalManager)
FileUtils.rm_r("data/resources/sprites", :verbose=>true)
#FileUtils.mkdir_p("data/resources/sprites", :verbose=>true)
#FileUtils.cp(substrata_dir + "/resources/sprites/foam_sprite_front.basis", "data/resources/sprites")

FileUtils.rm("data/resources/grass clump billboard 2 normals.png", :verbose=>true) # Grass is not used in Web build currently.
FileUtils.rm("data/resources/grass clump billboard 2.png", :verbose=>true) # Grass is not used in Web build currently.
FileUtils.rm("data/resources/foam_windowed.basis", :verbose=>true) # This file is explictly downloaded in TerrainDecalManager.

FileUtils.cp_r(glare_core + "/opengl/shaders", "data", :verbose=>true)
FileUtils.cp_r(substrata_dir + "/shaders/.", "data/shaders", :verbose=>true) # Copy contents of shaders dir to data/shaders

FileUtils.cp_r(glare_core + "/opengl/gl_data", "data", :verbose=>true)

# Remove sky dir, just copy in a few files that we need for the default sun elevation angle.
FileUtils.rm_r("data/gl_data/sky", :verbose=>true) # Remove sky dir
FileUtils.mkdir_p("data/gl_data/sky", :verbose => true)
for i in 0..5 do
	FileUtils.cp(glare_core + "/opengl/gl_data/sky/diffuse_sky_no_sun_24_#{i}.exr", "data/gl_data/sky", :verbose => true)
	FileUtils.cp(glare_core + "/opengl/gl_data/sky/diffuse_sky_no_sun_25_#{i}.exr", "data/gl_data/sky", :verbose => true)
end
FileUtils.cp(glare_core + "/opengl/gl_data/sky/sky_no_sun_24.exr", "data/gl_data/sky", :verbose => true)
FileUtils.cp(glare_core + "/opengl/gl_data/sky/sky_no_sun_25.exr", "data/gl_data/sky", :verbose => true)
FileUtils.cp(glare_core + "/opengl/gl_data/sky/specular_refl_sky_no_sun_combined_24.exr", "data/gl_data/sky", :verbose => true)
FileUtils.cp(glare_core + "/opengl/gl_data/sky/specular_refl_sky_no_sun_combined_25.exr", "data/gl_data/sky", :verbose => true)


FileUtils.rm_r("data/gl_data/caustics", :verbose=>true) # These files are explictly downloaded in OpenGLEngine::startAsyncLoadingData().


# Copy some resources to build output dir while we're at it (used for testing)
# These resources are served one by one to the webclient instead of being packaged and preloaded.
cyberspace_output = ENV['CYBERSPACE_OUTPUT']
if cyberspace_output.nil?
	puts "CYBERSPACE_OUTPUT env var must be defined"
	exit(1)
end

cyb_output_resources_dir             = cyberspace_output + "/data/resources"
cyb_output_test_builds_resources_dir = cyberspace_output + "/test_builds/data/resources"

FileUtils.mkdir_p(cyb_output_resources_dir, :verbose => true)             if !File.exists?(cyb_output_resources_dir)             # Make cyberspace_output + "/data" dir if it doesn't exist already.
FileUtils.mkdir_p(cyb_output_test_builds_resources_dir, :verbose => true) if !File.exists?(cyb_output_test_builds_resources_dir) # Make cyberspace_output + "/test_builds/data" dir if it doesn't exist already.

FileUtils.cp_r(substrata_dir + "/webclient/webclient.html",            cyberspace_output,                                                   :verbose=>true)
FileUtils.cp_r(substrata_dir + "/webclient/webclient.html",            cyberspace_output + "/test_builds",                                  :verbose=>true)
FileUtils.cp_r(substrata_dir + "/resources/sprites",                   cyb_output_resources_dir,                                            :verbose=>true)
FileUtils.cp_r(substrata_dir + "/resources/sprites",                   cyb_output_test_builds_resources_dir,                                :verbose=>true)
FileUtils.cp_r(substrata_dir + "/resources/foam_windowed.basis",       cyb_output_resources_dir             + "/foam_windowed.basis",       :verbose=>true)
FileUtils.cp_r(substrata_dir + "/resources/foam_windowed.basis",       cyb_output_test_builds_resources_dir + "/foam_windowed.basis",       :verbose=>true)

FileUtils.cp_r(glare_core + "/opengl/gl_data", cyberspace_output + "/data",             :verbose=>true)
FileUtils.cp_r(glare_core + "/opengl/gl_data", cyberspace_output + "/test_builds/data", :verbose=>true)

# Change/touch a file to trigger a relink.
FileUtils.touch(glare_core + '/graphics/TextureData.cpp')

puts "Done."
