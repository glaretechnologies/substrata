#
# Builds OpenSSL on Windows, Mac or Linux.
#
#

require 'fileutils'
require 'net/http'
require './script_utils.rb'
require './config-lib.rb'


class Timer
	def self.time(&block)
		start_time = Time.now
		result = block.call
		end_time = Time.now
		@time_taken = end_time - start_time
		result
	end

	def self.elapsedTime
		return @time_taken
	end

end


def getOpenSSLSource()
	puts "Downloading LibreSSL release #{$libressl_version}..."

	if File.exists?($libressl_source_file)
		puts "LibreSSL release already downloaded."
		return
	end

	Net::HTTP.start("ftp.openbsd.org") do |http|
		resp = http.get("/pub/OpenBSD/LibreSSL/" + $libressl_source_file)
		open($libressl_source_file, "wb") do |file|
			file.write(resp.body)
		end
	end
	
	puts "Done."
end


def extractArchive(archive)
	if OS.windows?
		sevenz_path = "C:\\Program Files\\7-Zip\\7z.exe"

		if archive.include?(".tar.gz")
			puts "Extracting #{archive} (silently)..."
			# Extract with 7zip. -y option won't show any prompts and assumes yes in all prompts. Redirect to nul because there is no silent option.
			Kernel.system("\"#{sevenz_path}\" x #{archive} -y > nul")
			puts "Done."
			
			tar_archive = archive[0..-4] # remove ".gz"
			
			puts "Extracting #{tar_archive} (silently)..."
			Kernel.system("\"#{sevenz_path}\" x #{tar_archive} -y > nul")
			puts "Done."
			
			if File.exists?(tar_archive)
				FileUtils.rm_r(tar_archive)
			end
		elsif archive.include?(".zip")
		
		end
	else
		if archive.include?(".tar.gz")
			puts "Extracting #{archive}..."
			Kernel.system("tar -zxf #{archive}")
                        puts "Done."
		end
	end
end


def getLineIndexThatContains(array, startwith, start_line)	
	array.each_index do |x| 
		if x > start_line
			if array[x].include?(startwith)
				return x
			end
		end
	end
	
	return nil
end


# Write a batch file with the right config
def buildWindows(bitness, config, vs_version)
	puts "
	
	
Preparing vs#{vs_version} #{bitness}-bit build."
	puts "Targeting Windows XP: #{$target_xp}"

	if File.exists?($libressl_source_name)
		FileUtils.rm_r($libressl_source_name)
		puts "Deleted possible junk."
	end
	
	extractArchive($libressl_source_file)

	# Generator and toolset names.
	vs_versions = {
		2008 => ["Visual Studio 9 2008", "v90"],
		2010 => ["Visual Studio 10", "v100"],
		2012 => ["Visual Studio 11", $target_xp ? "v110_xp" : "v110"],
		2013 => ["Visual Studio 12", $target_xp ? "v120_xp" : "v120"],
		2015 => ["Visual Studio 14", $target_xp ? "v140_xp" : "v140"],
		2017 => ["Visual Studio 15", $target_xp ? "v141_xp" : "v141"]
	}
	
	if vs_versions[vs_version].nil?
		puts "Error, invalid vs_version: #{vs_version}"
		exit(1)
	end
	
	src_dir = "#{$indigo_libressl_dir}/#{$libressl_source_name}"
	
	config_opts = {
		:debug => ["Debug", "-debug"],
		:release => ["Release", ""]
	}
	
	configurations = []
	configurations << :debug if config == "Both" || config == "Debug"
	configurations << :release if config == "Both" || config == "Release"
	
	configurations.each do |configuration|
		build_dir = "#{$indigo_libressl_dir}/libressl-#{$libressl_version}-#{bitness == 64 ? "x64" : "x86"}-vs#{vs_version}-build#{config_opts[configuration][1]}"
		
		if File.exists?(build_dir)
			FileUtils.rm_r(build_dir)
			puts "Deleted old build."
		end
		
		FileUtils.mkdir(build_dir)
		
		Dir.chdir(build_dir) do
			prefix = "#{$indigo_libressl_dir}/libressl-#{$libressl_version}-#{bitness == 64 ? "x64" : "x86"}-vs#{vs_version}-install#{config_opts[configuration][1]}"
			
			if File.exists?(prefix)
				FileUtils.rm_r(prefix)
				puts "Deleted old install."
			end
		
			Kernel.system("cmake \"#{src_dir}\" -G \"#{vs_versions[vs_version][0]}#{bitness == 64 ? " Win64" : ""}\" -T \"#{vs_versions[vs_version][1]}\" -DCMAKE_INSTALL_PREFIX:STRING=\"#{prefix}\"")
			
			puts "Doing #{config_opts[configuration][0]} build."
			Kernel.system("cmake --build . --config #{config_opts[configuration][0]}")
			
			Kernel.system("cmake -DCMAKE_INSTALL_CONFIG_NAME:STRING=#{config_opts[configuration][0]} -P ./cmake_install.cmake")
		end
	end
end


def buildOpenSslUnix()
	if File.exists?($libressl_source_name)
		FileUtils.rm_r($libressl_source_name)
		puts "Deleted possible junk."
	end

	extractArchive($libressl_source_file)
	
	prefix = "#{$indigo_libressl_dir}/libressl-#{$libressl_version}-install"
	
	if File.exists?(prefix)
		FileUtils.rm_r(prefix)
		puts "Deleted old build."
	end
	
	Dir.chdir($libressl_source_name) do
		Kernel.system("./configure --prefix=#{prefix}")
		Kernel.system("make -j8")
		Kernel.system("make install")
	end
end

# Both of these come from config-lib.rb.
#$libressl_version = "2.3.1"
#$vs_version = 2013
$config = "Both" # Build both debug and release configs by default
$target_xp = false # use platform toolset that supports windows xp


def printUsage()
	puts "Usage: build_openssl.rb [arguments]"
	puts ""
	puts "Options marked with * are the default."
	puts ""
	puts "\t--release, -R\t\tSpecifies the LibreSSL release to get. Default is #{$libressl_version}."
	puts ""
	puts "\t--vsversion, -v\t\tSpecifies the Visual Studio version. Default is #{$vs_version}."
	puts ""
	puts "\t--config, -c\t\tSpecifies the config to build. Release, Debug, or Both. Default is #{$config}."
	puts ""
	puts "\t--targetxp\t\tTarget Windows XP, if available. Default is: #{$target_xp}."
	puts ""
	puts "\t--help, -h\t\tShows this help."
	puts ""
end

# Don't parse args as may be required from build.rb
puts "Using default version: #{$libressl_version}"
puts "Using default config: #{$config}"
#arg_parser = ArgumentParser.new(ARGV)
#arg_parser.options.each do |opt|
#	if opt[0] == "--release" || opt[0] == "-R"
#		if opt[1] == nil
#			puts "Using default version: #{$libressl_version}"
#		else
#			$libressl_version = opt[1]
#		end
#	elsif opt[0] == "--vsversion" || opt[0] == "-v"
#		if opt[1] == "2013"
#			$vs_version = 2013
#		elsif opt[1] == "2015"
#			$vs_version = 2015
#		elsif opt[1] == "2017"
#			$vs_version = 2017
#		else
#			puts "Invalid vsversion: #{opt[1]}"
#			exit 1
#		end
#	elsif opt[0] == "--config" || opt[0] == "-c"
#		if opt[1] == nil
#			puts "Using default config: #{$config}"
#		else
#			$config = opt[1]
#		end
#	elsif opt[0] == "--targetxp"
#		if opt[1].downcase == "true" || opt[1].downcase == "1"
#			$target_xp = true
#		elsif opt[1].downcase == "false" || opt[1].downcase == "0"
#			$target_xp = false
#		else
#			puts "Invalid boolean for targetxp: #{opt[0]}"
#			exit 1
#		end
#	elsif opt[0] == "--help" || opt[0] == "-h"
#		printUsage()
#		exit 0
#	else
#		puts "Unrecognised argument: #{opt[0]}"
#		exit 1
#	end
#end


if($config != "Release" and $config != "Debug" and $config != "Both")
	STDERR.puts "Unknown config #{$config}."
	exit(1)
end


$libressl_source_name = "libressl-#{$libressl_version}"
$libressl_source_file = "#{$libressl_source_name}.tar.gz"
$indigo_libs_dir = ENV['INDIGO_LIBS']
if $indigo_libs_dir.nil?
	STDERR.puts "INDIGO_LIBS env var not defined."
	exit(1)
end


$indigo_libressl_dir = "#{$indigo_libs_dir}/LibreSSL"

if !File.exists?($indigo_libressl_dir)
	puts "making dir #{$indigo_libressl_dir}"
	FileUtils.mkdir($indigo_libressl_dir)
end

puts "Chdir to \"#{$indigo_libressl_dir}\"."
Dir.chdir($indigo_libressl_dir)

Timer.time {

# Download the source.
getOpenSSLSource()

if OS.windows?
	# Build all configs
	#buildWindows(32, $config, $vs_version)
	buildWindows(64, $config, $vs_version)
else
	buildOpenSslUnix()
end

}

puts "Total build time: #{Timer.elapsedTime} s"
