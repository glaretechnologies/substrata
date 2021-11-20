#
# Builds LibreSSL on Windows, Mac or Linux.
#
#

require 'fileutils'
require 'net/http'
require './script_utils.rb'
require './config-lib.rb'
require './cmake.rb'


puts "-------------------------------------
LibreSSL build

"


$libressl_version = "3.3.5"
# $vs_version = 2019 # comes from config-lib.rb.
$configurations = [ :release, :debug ]
$forcerebuild = false
$build_epoch = 0


def printUsage()
	puts "Usage: build_libressl.rb [arguments]"
	puts ""
	puts "\t--release, -R\t\tSpecifies the LibreSSL release to get. Default is #{$libressl_version}."
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
	if opt[0] == "--release" || opt[0] == "-R"
		if opt[1] == nil
			puts "Using default version: #{$libressl_version}"
		else
			$libressl_version = opt[1]
		end
	elsif opt[0] == "--vsversion" || opt[0] == "-v"
		$vs_version = opt[1].to_i
		if not [2013, 2015, 2017, 2019].include?($vs_version)
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
		puts "Warning: Unrecognised argument: #{opt[0]}" # We can have args here from build.rb which we don't handle.
		#exit 1
	end
end


def getLibreSSLSource()
	puts "Downloading LibreSSL release #{$libressl_version}..."
	
	downloadFileHTTPSIfNotOnDisk($libressl_source_file, "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/#{$libressl_source_file}")
	
	extractArchiveIfNotExtraced($libressl_source_file, $libressl_source_name, true)
	
	patchSource() if OS.windows?
end


# Patch source to fix an issue with closing the socket while it's doing a blocking call.
# See https://github.com/libressl-portable/portable/issues/266
# Basically we want to avoid calling read() on a socket.  Instead just return the WSA error code.
def patchSource()
	puts "Patching source.."
	src_dir = $libressl_source_name
	
	if File.exist?("#{src_dir}/glare-patch.success")
		puts "Already patched, skipping."
		return
	end

	#puts "src_dir: #{src_dir}"

	#path = src_dir + "/crypto/compat/posix_win.c"
	#puts "Patching '#{path}'..."

	#contents = File.open(path).read()

	#puts "contents: #{contents}"

	#new_content = contents.gsub("(err == WSAENOTSOCK || err == WSAEBADF", "/*GLARE NEWCODE*/0 && (err == WSAENOTSOCK || err == WSAEBADF")

	#if new_content == contents
	#	puts "Patching failed, failed to find code to be replaced."
	#	exit(1)
	#end
	
	FileUtils.cp($cyberspace_trunk_dir + "/libressl_patches/posix_win.c", src_dir + "/crypto/compat/posix_win.c")
	FileUtils.cp($cyberspace_trunk_dir + "/libressl_patches/tls_config.c", src_dir + "/tls/tls_config.c")

	#File.open(path, 'w') { |file| file.write(new_content) }
	
	FileUtils.touch("#{src_dir}/glare-patch.success")

	#puts "new_content: #{new_content}"
	puts "Done patching source."
end


def getOutputDirName(configuration, dir_type, vs_version = -1)
	config_suffix = CMakeBuild.config_opts[configuration][1]
	
	if OS.windows?
		if vs_version == -1
			STDERR.puts "VS version not set."
			exit 1
		end
		
		return "libressl-#{$libressl_version}-x64-vs#{vs_version}-#{dir_type}#{config_suffix}"
	else
		return "libressl-#{$libressl_version}-#{dir_type}#{config_suffix}"
	end
end


def getBuildDir(configuration, vs_version = -1)
	getOutputDirName(configuration, "build", vs_version)
end


def getInstallDir(configuration, vs_version = -1)
	getOutputDirName(configuration, "install", vs_version)
end


# Write a batch file with the right config
def buildLibreSSL(configurations, vs_version)
	configurations.each do |configuration|
		cmake_build = CMakeBuild.new
		
		cmake_build.init("LibreSSL",
			"#{$indigo_libressl_dir}/#{$libressl_source_name}",
			"#{$indigo_libressl_dir}/#{getBuildDir(configuration, vs_version)}",
			"#{$indigo_libressl_dir}/#{getInstallDir(configuration, vs_version)}")
		
		cmake_build.configure(configuration, vs_version)
		cmake_build.build()
		cmake_build.install($build_epoch)
	end
end



$libressl_source_name = "libressl-#{$libressl_version}"
$libressl_source_file = "#{$libressl_source_name}.tar.gz"
$indigo_libs_dir = ENV['INDIGO_LIBS']
if $indigo_libs_dir.nil?
	STDERR.puts "INDIGO_LIBS env var not defined."
	exit(1)
end


$indigo_libressl_dir = "#{$indigo_libs_dir}/LibreSSL"

$cyberspace_trunk_dir = Dir.getwd + "/.."

FileUtils.mkdir($indigo_libressl_dir, :verbose=>true) if !Dir.exists?($indigo_libressl_dir)
puts "Chdir to \"#{$indigo_libressl_dir}\"."
Dir.chdir($indigo_libressl_dir)


# If force rebuild isn't set, skip the builds if the output exists.
if !$forcerebuild
	all_output_exists = true
	$configurations.each do |configuration|
		install_dir = getInstallDir(configuration, $vs_version)
		all_output_exists = false if !CMakeBuild.checkInstall(install_dir, $build_epoch)
	end
	
	if all_output_exists
		puts "LibreSSL: Builds are in place, use --forcerebuild to rebuild."
		exit(0)
	end
end


#Timer.time {

# Download the source.
getLibreSSLSource()

buildLibreSSL($configurations, $vs_version)

#}

#puts "Total build time: #{Timer.elapsedTime} s"
