#
# Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#
# Note that copyCyberspaceResources() is defined in dist_utils.rb
#

require './dist_utils.rb'
require './config-lib.rb'


$copy_cef = true
$copy_bugsplat = false


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
	elsif opt[0] == "--no_bugsplat"
		$copy_bugsplat = false
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
		copyCEFRedistWindows(output_dir, true) if $copy_cef
		copyBugSplatRedist(output_dir) if $copy_bugsplat
		copyQtRedistWindows(vs_version, output_dir, true)
		copySDLRedistWindows(vs_version, output_dir, true)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir) if $copy_cef
		copyBugSplatRedist(output_dir) if $copy_bugsplat
		copyQtRedistWindows(vs_version, output_dir, false)
		copySDLRedistWindows(vs_version, output_dir, false)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "Release")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir) if $copy_cef
		copyBugSplatRedist(output_dir) if $copy_bugsplat
		copyQtRedistWindows(vs_version, output_dir, false)
		copySDLRedistWindows(vs_version, output_dir, false)
	end
end



substrata_repos_dir = ".."
glare_core_repos_dir = getAndCheckEnvVar('GLARE_CORE_TRUNK_DIR')

if OS.windows?
	copy_files(2022, substrata_repos_dir, glare_core_repos_dir)
elsif OS.mac?
	#
	# For mac we need to populate the actual .app bundle that contains the built binary.
	# Previously the script always used the Debug build dir which resulted in populating
	# the skeleton app (e.g. test_builds) instead of the real Release app when BUILD_CONFIG=Release.
	#
	# Strategy:
	# 1) Prefer CYBERSPACE_OUTPUT/gui_client.app if it exists and contains the binary
	# 2) Prefer the config requested by the environment variable BUILD_CONFIG (if set)
	# 3) Search substrata_output and substrata_build for gui_client.app that actually contains the binary
	# 4) Copy resources/CEF into the first app bundle found
	# 5) As a last resort fall back to the old Debug location to preserve backward compatibility
	#
	begin
		# 1) Quick check: CYBERSPACE_OUTPUT (CI sets this)
		cs_out = ENV['CYBERSPACE_OUTPUT']
		if cs_out && !cs_out.empty?
			app_candidate = File.join(cs_out, "gui_client.app")
			bin_path = File.join(app_candidate, "Contents", "MacOS", "gui_client")
			if File.exist?(bin_path)
				puts "copy_files_to_output.rb: detected built app in CYBERSPACE_OUTPUT: #{app_candidate}"
				appdir = app_candidate
				output_dir = File.join(appdir, "Contents", "Resources")
				copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
				copyCEFRedistMac(File.dirname(appdir), appdir) if $copy_cef
				# done
				exit 0
			else
				puts "copy_files_to_output.rb: CYBERSPACE_OUTPUT set to #{cs_out} but no binary found at #{bin_path}"
			end
		else
			puts "copy_files_to_output.rb: CYBERSPACE_OUTPUT not set or empty; will search common build locations"
		end

		# 2) & 3) Search per-config and common locations
		preferred = ENV['BUILD_CONFIG'] ? ENV['BUILD_CONFIG'] : nil
		configs = []
		configs << preferred if preferred
		# common configs
		configs += ['Release', 'RelWithDebInfo', 'Debug']
		# uniq preserve order
		configs = configs.compact.uniq

		found_app = nil
		configs.each do |cfg|
			build_dir = getCmakeBuildDir(0, cfg)
			# There are a few places the .app may be placed; check common ones
			candidates = [
				File.join(build_dir, "gui_client.app"),
				File.join(build_dir, "gui_client.app/Contents/MacOS/../"), # legacy pattern
				File.join(File.dirname(build_dir), "substrata_output", "gui_client.app"),
				File.join(File.dirname(build_dir), "substrata_output", "test_builds", "gui_client.app"),
				File.join(File.dirname(build_dir), "substrata_output")
			]
			candidates.each do |cand|
				# normalize and check for real binary presence
				next if cand.nil? || cand.empty?
				cand_dir = cand.gsub(/\/+/, '/')
				bin_path = File.join(cand_dir, "Contents", "MacOS", "gui_client")
				if File.exist?(bin_path)
					found_app = cand_dir
					break
				end
			end
			break if found_app
		end

		if found_app
			appdir = found_app
			output_dir = File.join(appdir, "Contents", "Resources")
			puts "copy_files_to_output.rb: populating app at #{appdir} (detected binary present)"
			copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
			copyCEFRedistMac(File.dirname(appdir), appdir) if $copy_cef
		else
			# Last-resort fallback to previous Debug behavior to avoid surprising regressions.
			puts "copy_files_to_output.rb: no built gui_client.app with binary found for configs #{configs.inspect}; falling back to Debug build dir."
			build_dir = getCmakeBuildDir(0, "Debug")
			appdir = File.join(build_dir, "gui_client.app")
			output_dir = File.join(appdir, "Contents", "MacOS", "..", "Resources")
			copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
			copyCEFRedistMac(build_dir, appdir) if $copy_cef
		end
	end

	begin
		#output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		#copyCyberspaceResources(output_dir)
	end
else # else Linux:
	output_dir = getCmakeBuildDir(0, "Debug")
	puts "output_dir: #{output_dir}"
	copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
	
	copyCEFRedistLinux(output_dir,
		false # strip_symbols
	) if $copy_cef
end

