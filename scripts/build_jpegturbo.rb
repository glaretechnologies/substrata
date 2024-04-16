#
# Builds LibJpeg Turbo on Windows, Mac or Linux.
#
#

require 'fileutils'
require 'net/http'
require './script_utils.rb'
require './config-lib.rb'
require './cmake.rb'


puts "-------------------------------------
LibJpeg Turbo build

"


$configurations = [ :release, :debug ]
$forcerebuild = false
$build_epoch = 1


def printUsage()
	puts "Usage: build_jpegturbo.rb [arguments]"
	puts ""
	puts "\t--vsversion, -v\t\tSpecifies the Visual Studio version. Default is #{$vs_version}."
	puts ""
	puts "\t--config, -c\t\tSpecifies the config to build. Release, Debug, or Both. Default is Both."
	puts ""
	puts "\t--forcerebuild, -f\tForce a rebuild."
	puts ""
	puts "\t--help, -h\t\tShows this help."
	puts ""
end


arg_parser = ArgumentParser.new(ARGV)
arg_parser.options.each do |opt|
	if opt[0] == "--vsversion" || opt[0] == "-v"
		$vs_version = opt[1].to_i
		if not [2012, 2013, 2015, 2017, 2019, 2022].include?($vs_version)
			puts "Unsupported VS version: #{opt[1]}. Skipping."
			exit 0
		end
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
	elsif opt[0] == "--forcerebuild" || opt[0] == "-f"
		$forcerebuild = true
	elsif opt[0] == "--help" || opt[0] == "-h"
		printUsage()
		exit 0
	else
		puts "Unrecognised argument: #{opt[0]}"
		exit 1
	end
end


def getLibJpegTurboSource()
	puts "Downloading LibJpeg Turbo release #{$libjpegturbo_version}..."
	
	downloadFileHTTPSIfNotOnDisk($libjpegturbo_source_file, "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/#{$libjpegturbo_version}.zip")
	
	extractArchiveIfNotExtraced($libjpegturbo_source_file, $libjpegturbo_source_name, true)
end


def getOutputDirName(configuration, dir_type, vs_version = -1)
	config_suffix = CMakeBuild.config_opts[configuration][1]
	
	if OS.windows?
		if vs_version == -1
			STDERR.puts "VS version not set."
			exit 1
		end
		
		return "libjpeg-turbo-#{$libjpegturbo_version}-vs#{vs_version}-#{dir_type}#{config_suffix}"
	else
		return "libjpeg-turbo-#{$libjpegturbo_version}-#{dir_type}#{config_suffix}"
	end
end


def getBuildDir(configuration, vs_version = -1)
	getOutputDirName(configuration, "build", vs_version)
end


def getInstallDir(configuration, vs_version = -1)
	getOutputDirName(configuration, "install", vs_version)
end


# Write a batch file with the right config
def buildLibJpegTurbo(configurations, vs_version)
	configurations.each do |configuration|
		cmake_build = CMakeBuild.new
		
		cmake_build.init("LibJpeg Turbo",
			"#{$indigo_libjpegturbo_dir}/#{$libjpegturbo_source_name}",
			"#{$indigo_libjpegturbo_dir}/#{getBuildDir(configuration, vs_version)}",
			"#{$indigo_libjpegturbo_dir}/#{getInstallDir(configuration, vs_version)}")

		cmake_args = "-DENABLE_STATIC=1"
		cmake_args += " -DENABLE_SHARED=0" if !OS.windows?
		cmake_args += " -DWITH_CRT_DLL=1" if OS.windows?
		
		cmake_build.configure(configuration, vs_version, cmake_args, false, OS.arm64?)
		cmake_build.build()
		cmake_build.install($build_epoch)
	end
end


$libjpegturbo_version = "3.0.0" # From 4th July 2023.
$libjpegturbo_source_name = "libjpeg-turbo-#{$libjpegturbo_version}"
$libjpegturbo_source_file = "libjpeg-turbo-#{$libjpegturbo_version}.zip"
$glare_core_dir = ENV['GLARE_CORE_LIBS']
if $glare_core_dir.nil?
	STDERR.puts "GLARE_CORE_LIBS env var not defined."
	exit(1)
end


$indigo_libjpegturbo_dir = "#{$glare_core_dir}/libjpeg-turbo"

FileUtils.mkdir($indigo_libjpegturbo_dir, :verbose=>true) if !Dir.exist?($indigo_libjpegturbo_dir)
puts "Chdir to \"#{$indigo_libjpegturbo_dir}\"."
Dir.chdir($indigo_libjpegturbo_dir)


# If force rebuild isn't set, skip the builds if the output exists.
if !$forcerebuild
	all_output_exists = true
	$configurations.each do |configuration|
		install_dir = getInstallDir(configuration, $vs_version)
		all_output_exists = false if !CMakeBuild.checkInstall(install_dir, $build_epoch)
	end
	
	if all_output_exists
		puts "LibJpeg Turbo: Builds are in place, use --forcerebuild to rebuild."
		exit(0)
	end
end


Timer.time {

# Download the source.
getLibJpegTurboSource()

buildLibJpegTurbo($configurations, $vs_version)

}

puts "Total build time: #{Timer.elapsedTime} s"
