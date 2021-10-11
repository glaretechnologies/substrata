#
#  Copies various files that are need to run Substrata into the CYBERSPACE_OUTPUT directories.
#

require './dist_utils.rb'


def copy_files(vs_version, bitness)

	begin
		output_dir = getCmakeBuildDir(vs_version, "Debug")

		copyCyberspaceResources(output_dir)
	end

	begin
		output_dir = getCmakeBuildDir(vs_version, "RelWithDebInfo")

		copyCyberspaceResources(output_dir)
	end
end


if OS.windows?
	copy_files(2019, 64)
elsif OS.linux?
end
