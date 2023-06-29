#
# Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#
# Note that copyCyberspaceResources() is defined in dist_utils.rb
#

require './dist_utils.rb'


def copy_files(vs_version, substrata_repos_dir, glare_core_repos_dir)

	begin
		output_dir = getCmakeBuildDir(vs_version, "Debug")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
		copyQtRedistWindows(vs_version, output_dir, true)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
		copyQtRedistWindows(vs_version, output_dir, false)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "Release")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
		copyQtRedistWindows(vs_version, output_dir, false)
	end
end



substrata_repos_dir = ".."
glare_core_rev_path = "trunk"
glare_core_repos_dir = getAndCheckEnvVar('GLARE_CORE_TRUNK_DIR') + "/../" + glare_core_rev_path

if OS.windows?
	copy_files(2022, substrata_repos_dir, glare_core_repos_dir)
elsif OS.mac?
	begin
        build_dir = getCmakeBuildDir(0, "Debug")
		appdir = build_dir + "/gui_client.app"
		output_dir = build_dir + "/gui_client.app/Contents/MacOS/../Resources"

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistMac(build_dir, appdir)
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
	)
end
