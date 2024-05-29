#
# Gets the source code for some libraries used by Substrata.
#

require 'fileutils'
require './script_utils.rb'


$glare_core_libs_dir = ENV['GLARE_CORE_LIBS']
if $glare_core_libs_dir.nil?
	STDERR.puts "GLARE_CORE_LIBS env var not defined."
	exit(1)
end

tag_dir = $glare_core_libs_dir + "/luau/0.627"

puts "Checking for already cloned source at '" + tag_dir + "'..."

if File.exist?(tag_dir)
	puts "Already cloned there, nothing to do."
else
	print_and_exec_command("git clone --depth 1 --branch 0.627 https://github.com/luau-lang/luau.git \"#{tag_dir}\"")
end
