#
# Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#
# Note that copyCyberspaceResources() is defined in dist_utils.rb
#

require './dist_utils.rb'
require './config-lib.rb'


$copy_cef = true


def printUsage()
	puts "Usage: copy_files_to_output.rb [arguments]"
	puts ""
	puts "\t--no_cef, \t\tSkip copying CEF files."
	puts ""
	puts "\t--help, -h\t\tShows this help."
	puts ""
end


arg_parser = ArgumentParser.new(ARGV)
arg_parser.options.each do |opt|
	if opt[0] == "--no_cef"
		$copy_cef = false
	elsif opt[0] == "--help" || opt[0] == "-h"
		printUsage()
		exit 0
	else
		puts "Unrecognised argument: #{opt[0]}"
		exit 1
	end
end


def copy_files(vs_version, substrata_repos_dir, glare_core_repos_dir)

	begin
		output_dir = getCmakeBuildDir(vs_version, "Debug")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir) if $copy_cef
		copyBugSplatRedist(output_dir)
		copyQtRedistWindows(vs_version, output_dir, true)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir) if $copy_cef
		copyBugSplatRedist(output_dir)
		copyQtRedistWindows(vs_version, output_dir, false)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "Release")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir) if $copy_cef
		copyBugSplatRedist(output_dir)
		copyQtRedistWindows(vs_version, output_dir, false)
	end
end



substrata_repos_dir = ".."
glare_core_repos_dir = getAndCheckEnvVar('GLARE_CORE_TRUNK_DIR')

if OS.windows?
	copy_files(2022, substrata_repos_dir, glare_core_repos_dir)
elsif OS.mac?
	begin
        build_dir = getCmakeBuildDir(0, "Debug")
		appdir = build_dir + "/gui_client.app"
		output_dir = build_dir + "/gui_client.app/Contents/MacOS/../Resources"

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistMac(build_dir, appdir) if $copy_cef
	end

	begin
		#output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		#copyCyberspaceResources(output_dir)
	end
else # else Linux:
	output_dir = getCmakeBuildDir(0, "Debug")
	puts "output_dir: #{output_dir}"
	copyCyberspaceResources(output_dir)
	
	copyCEFRedistLinux(output_dir,
		false # strip_symbols
	) if $copy_cef
end
