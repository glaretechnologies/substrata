#
# Builds LLVM on Windows, Mac or Linux.
#
#

require 'fileutils'
require 'net/http'
require './script_utils.rb'
require './config-lib.rb'
require './cmake.rb'


puts "-------------------------------------
LLVM build

"


# Set up defaults
$configurations = [ :release, :debug ]
# todo: use CMakeBuild.config_opts
$config_opts = {
	:debug => ["Debug", "_debug"],
	:release => ["Release", ""]
}
$forcerebuild = false
$build_epoch = 0


def printUsage()
	puts "Usage: build_llvm.rb [arguments]"
	puts ""
	puts "Options marked with * are the default."
	puts ""
	puts "\t--release, -R\t\tSpecifies the LLVM release to get. Default is #{$llvm_version}."
	puts ""
	puts "\t--config, -c\t\tSpecifies the config to build. Release, Debug, or Both. Default is Both."
	puts ""
	puts "\t--vsversion, -v\t\tSpecifies the vs version to use. Valid options are: 2012, 2013, 2015, 2017, 2019, 2022. Default is: #{$vs_version}."
	puts ""
	puts "\t--forcerebuild, -f\tForce a rebuild."
	puts ""
	puts "\t--help, -h\t\tShows this help."
	puts ""
end


arg_parser = ArgumentParser.new(ARGV)

arg_parser.options.each do |opt|
	if opt[0] == "--release" || opt[0] == "-R"
		if opt[1] == nil
			puts "Using default version: #{$llvm_version}"
		else
			$llvm_version = opt[1]
		end
	elsif opt[0] == "--help" || opt[0] == "-h"
		printUsage()
		exit 0
	elsif opt[0] == "--config" || opt[0] == "-c"
		if opt[1] == nil
			puts "Using default config."
		else
			config = opt[1].downcase
			if(config != "release" and config != "debug" and config != "both")
				STDERR.puts "Unknown config #{opt[1]}."
				exit(1)
			end
		
			$configurations = []
			$configurations << :debug if config == "both" || config == "debug"
			$configurations << :release if config == "both" || config == "release"
		end
	elsif opt[0] == "--vsversion" || opt[0] == "-v"
		$vs_version = opt[1].to_i
		if not [2012, 2013, 2015, 2017, 2019, 2022].include?($vs_version)
			puts "Unsupported VS version: #{opt[1]}. Skipping."
			exit 0
		end
	elsif opt[0] == "--forcerebuild" || opt[0] == "-f"
		$forcerebuild = true
	else
		puts "Unrecognised argument: #{opt[0]}"
		exit 1
	end
end


# returns the path it was extracted to
def getLLVMSourceDownloadAndExtract()
	puts "Downloading LLVM release #{$llvm_version}..."

	extension = (Gem::Version.new($llvm_version) >= Gem::Version.new('3.6')) ? "xz" : "gz"

	src_file = "llvm-#{$llvm_version}.src.tar.#{extension}"
	src_folder = (Gem::Version.new($llvm_version) <= Gem::Version.new('3.4')) ? "llvm-#{$llvm_version}" : "llvm-#{$llvm_version}.src"
	
	if(Gem::Version.new($llvm_version) >= Gem::Version.new('11.0.0')) # At some version the downloads moved to github, not sure when.
		downloadFileHTTPSIfNotOnDisk(src_file, "https://github.com/llvm/llvm-project/releases/download/llvmorg-#{$llvm_version}/llvm-#{$llvm_version}.src.tar.xz")
	else
		downloadFileHTTPSIfNotOnDisk(src_file, "https://releases.llvm.org/#{$llvm_version}/#{src_file}")
	end
		
	
	puts "src_file: #{src_file}"
	puts "src_folder: #{src_folder}"
	extractArchiveIfNotExtraced(src_file, src_folder)
	
	return src_folder
end


def getOutputDirName(configuration, dir_type, vs_version = -1)
	version_string = $llvm_version.gsub(".", "_")
	config_suffix = $config_opts[configuration][1] # todo: use CMakeBuild.config_opts

	if OS.windows?
		if vs_version == -1
			STDERR.puts "VS version not set."
			exit 1
		end
		
		return "llvm_#{version_string}_#{dir_type}_vs#{vs_version}_64#{config_suffix}"
	elsif OS.linux?
		return "llvm_#{version_string}_dylib_#{dir_type}#{config_suffix}"   # Make dylib build
	else
		return "llvm_#{version_string}_#{dir_type}#{config_suffix}"
	end
end


def getBuildDir(configuration, vs_version = -1)
	return getOutputDirName(configuration, "build", vs_version)
end


def getInstallDir(configuration, vs_version = -1)
	return getOutputDirName(configuration, "install", vs_version)
end


def buildLLVM(llvm_src_dir, vs_version = -1)
	$configurations.each do |configuration|
		cmake_build = CMakeBuild.new
		
		cmake_build.init("LLVM",
			"#{$llvm_dir}/#{llvm_src_dir}",
			"#{$llvm_dir}/#{getBuildDir(configuration, vs_version)}",
			"#{$llvm_dir}/#{getInstallDir(configuration, vs_version)}")
			
		cmake_args = ""
		
		if OS.mac?
			# While we target 10.8, we need to tell oidn to use libc++.
			cmake_args += " -DCMAKE_CXX_FLAGS:STRING=\"-std=c++11 -stdlib=libc++\"" +
				" -DCMAKE_EXE_LINKER_FLAGS:STRING=\"-stdlib=libc++\"" +
				" -DCMAKE_SHARED_LINKER_FLAGS:STRING=\"-stdlib=libc++\"" +
				" -DCMAKE_MODULE_LINKER_FLAGS:STRING=\"-stdlib=libc++\""
		end
		
		if OS.windows?
			cmake_args += " -DCMAKE_CXX_FLAGS:STRING=\"-D_SECURE_SCL=0\"" if configuration == :release
			cmake_args += " -DLLVM_INCLUDE_TOOLS=OFF"
			
			if Gem::Version.new($llvm_version) >= Gem::Version.new('8.0.0')
				cmake_args += " -DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=ON "
			end
		end
		
		# Unix specific arguments (OSX and linux)
		if !OS.windows?
			# Note: When building with GCC, inheriting from an LLVM class requires LLVM to be built with RTTI. (as long as indigo uses RTTI, which it does)
			# And WinterMemoryManager in VirtualMachine.cpp in Winter trunk inherits from an LLVM class.
			cmake_args += " -DLLVM_REQUIRES_RTTI=TRUE"
		end
		
		if OS.linux?
			cmake_args += " -DLLVM_BUILD_LLVM_DYLIB=TRUE"
		end
		
		cmake_args += " -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_OPTIMIZED_TABLEGEN=ON -DLLVM_TARGETS_TO_BUILD=\"X86\""
		
		cmake_build.configure(configuration, vs_version, cmake_args)
		cmake_build.build()
		cmake_build.install($build_epoch)
	end
end


$glare_core_libs_dir = ENV['GLARE_CORE_LIBS']
if $glare_core_libs_dir.nil?
	puts "GLARE_CORE_LIBS env var not defined."
	exit(1)
end

# this is cmake, we cant have backslashes.
$glare_core_libs_dir = $glare_core_libs_dir.gsub("\\", "/")


$llvm_dir = "#{$glare_core_libs_dir}/llvm"

FileUtils.mkdir($llvm_dir, :verbose=>true) if !Dir.exists?($llvm_dir)
puts "Chdir to \"#{$llvm_dir}\"."
Dir.chdir($llvm_dir)


# If force rebuild isn't set, skip the builds if the output exists.
if !$forcerebuild
	all_output_exists = true
	$configurations.each do |configuration|
		install_dir = getInstallDir(configuration, $vs_version)
		puts "Checking dir for presence of build: '" + install_dir + "'..."
		build_exists_at_dir = CMakeBuild.checkInstall(install_dir, $build_epoch)
		puts "build exists: " + build_exists_at_dir.to_s
		all_output_exists = false if !build_exists_at_dir
	end
	
	if all_output_exists
		puts "LLVM: Builds are in place, use --forcerebuild to rebuild."
		exit(0)
	end
end


Timer.time {

# Download the source.
llvm_src_dir = getLLVMSourceDownloadAndExtract()

buildLLVM(llvm_src_dir, $vs_version)

}

puts "Total build time: #{Timer.elapsedTime} s"
