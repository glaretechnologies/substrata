#
# Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#
# Note that copyCyberspaceResources() is defined dist_utils.rb
#

require './dist_utils.rb'


def copy_files(vs_version)

	substrata_repos_dir = ".."
	glare_core_rev_path = "trunk"
	glare_core_repos_dir = getAndCheckEnvVar('GLARE_CORE_TRUNK_DIR') + "/../" + glare_core_rev_path

	begin
		output_dir = getCmakeBuildDir(vs_version, "Debug")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "Release")

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
	end
end


if OS.windows?
	copy_files(2022)
elsif OS.mac?
	substrata_repos_dir = ".."
	glare_core_rev_path = "glare-core"
	glare_core_repos_dir = getAndCheckEnvVar('GLARE_CORE_TRUNK_DIR') + "/../" + glare_core_rev_path

	begin
        build_dir = getCmakeBuildDir(0, "Debug")
		appdir = build_dir + "/gui_client.app"
		output_dir = build_dir + "/gui_client.app/Contents/MacOS/../Resources"

		copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, output_dir)
		# copyCyberspaceResources(output_dir)
		# copyCEFRedistMac(build_dir, appdir)
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
