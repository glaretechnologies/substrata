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

FileUtils.mkdir_p("data", :verbose => true) if !File.exists?("data") # Make data dir if it doesn't exist already.

FileUtils.cp_r(substrata_dir + "/resources", "data", :verbose=>true)
FileUtils.rm_r("data/resources/materials", :verbose=>true)
FileUtils.rm_r("data/resources/models", :verbose=>true)
FileUtils.rm_r("data/resources/sounds", :verbose=>true)
FileUtils.rm("data/resources/grass clump billboard 2 normals.png", :verbose=>true)
FileUtils.rm("data/resources/grass clump billboard 2.png", :verbose=>true)

FileUtils.cp_r(glare_core + "/opengl/shaders", "data", :verbose=>true)
FileUtils.cp_r(substrata_dir + "/shaders/.", "data/shaders", :verbose=>true) # Copy contents of shaders dir to data/shaders

FileUtils.cp_r(glare_core + "/opengl/gl_data", "data", :verbose=>true)

puts "Done."