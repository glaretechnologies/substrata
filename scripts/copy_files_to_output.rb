#
#  Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#

require './dist_utils.rb'


def copy_files(vs_version)

	begin
		output_dir = getCmakeBuildDir(vs_version, "Debug")

		copyCyberspaceResources(output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		copyCyberspaceResources(output_dir)
		copyCEFRedistWindows(output_dir)
		copyBugSplatRedist(output_dir)
	end
end


if OS.windows?
	copy_files(2022)
elsif OS.mac?
	begin
		output_dir = getCmakeBuildDir(0, "Debug") + "/gui_client.app/Contents/MacOS/../Resources"

		copyCyberspaceResources(output_dir)
	end

	begin
		#output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		#copyCyberspaceResources(output_dir)
	end
else
	output_dir = getCmakeBuildDir(0, "Debug")
	puts "output_dir: #{output_dir}"
	copyCyberspaceResources(output_dir)
end
