#
# Builds with CMake.
#
# Copyright Glare Technologies Limited 2023 -
#


require 'fileutils'
require 'pathname'


class CMakeBuild

	def init(build_name, source_dir, build_dir, install_dir)
		@build_name = build_name
		
		@cmake_version = Gem::Version.new(`cmake --version`[/\d+\.\d+\.\d+/x])
		
		# make sure the cmake version is sane...
		if @cmake_version < Gem::Version.new('3.9')
			STDERR.puts "#{@build_name} CMake init: Error, your installed cmake '#{@cmake_version}' is too old. You should use at least #{Gem::Version.new('3.9')}."
			exit(1)
		end
		
		@source_dir = source_dir
		if !Pathname.new(@source_dir).absolute?
			STDERR.puts "#{@build_name} CMake init: Error, path to source must be absolute."
			exit(1)
		end
		
		@build_dir = build_dir
		if !Pathname.new(@build_dir).absolute?
			STDERR.puts "#{@build_name} CMake init: Error, base build path must be absolute."
			exit(1)
		end
		
		@install_dir = install_dir
		if !Pathname.new(@install_dir).absolute?
			STDERR.puts "#{@build_name} CMake init: Error, base build path must be absolute."
			exit(1)
		end
	end
	
	
	def configure(configuration, vs_version = -1, cmake_args = "", allow_reconfig = false, target_arm64 = false)
		if @@config_opts[configuration] == nil
			STDERR.puts "#{@build_name} CMake configure: Error, invalid configration: #{configuration.to_s}"
			exit(1)
		end
		
		@configuration = configuration
		config_name = getConfigName()
	
		if OS.windows? && @@vs_versions[vs_version].nil?
			STDERR.puts "#{@build_name} CMake configure: Error, invalid vs_version: #{vs_version}"
			exit(1)
		end
		@vs_version = vs_version
		
		# If init2 was used, generate the build dir.
		#if @base_dir != nil && @build_version != nil
		#	@build_dir = "#{@base_dir}/#{getBuildDir(configuration, vs_version)}"
		#	@install_dir = "#{@base_dir}/#{getInstallDir(configuration, vs_version)}"
		#end
		
		# allow_reconfig controls if the cmake directory will be deleted or if it can be reconfigured.
		if allow_reconfig == false
			if File.exist?(@build_dir)
				FileUtils.rm_r(@build_dir)
				puts "#{@build_name} CMake configure: Deleted old build directory."
			end
			
			FileUtils.mkdir(@build_dir)
		end
		
		if File.exist?(@install_dir)
			FileUtils.rm_r(@install_dir)
			puts "#{@build_name} CMake configure: Deleted old install directory."
		end
		
		unix_args = ""
		osx_args = ""
		win_args = ""

		if OS.unix?
			unix_args = " -DCMAKE_BUILD_TYPE=#{config_name}"
		end

		if OS.mac?
			if target_arm64
				osx_args = " -DCMAKE_OSX_ARCHITECTURES:STRING=\"arm64\""
			else
				osx_args = " -DCMAKE_OSX_ARCHITECTURES:STRING=\"x86_64\""
			end
			osx_args += " -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=\"10.8\"" # the sdk still needs to work on 10.8 during 4.4, even if the main indigo release targets 10.13 (because of Qt)
		end

		if OS.windows?
			win_args = " -G \"#{getVSGenerator()}\" -T \"#{getVSToolset()}\""
		end

		Dir.chdir(@build_dir) do
			print_and_exec_command("cmake \"#{@source_dir}\" -DCMAKE_INSTALL_PREFIX:STRING=\"#{@install_dir}\"#{unix_args}#{osx_args}#{win_args} #{cmake_args}")
		end
	end
	
	
	def build(targets = [], clean = false)
		if @configuration == nil
			STDERR.puts "#{@build_name} CMake build: Error, not configured"
			exit(1)
		end
		
		if targets.include?("install")
			STDERR.puts "#{@build_name} CMake build: Cannot include taget 'install' in build(). Use CMakeBuild::install()."
			exit(1)
		end
	
		config_name = getConfigName()
		
		puts "#{@build_name} CMake build: building #{config_name} config"
		
		# Basically, do not use 'cmake --build', it sucks.
		if OS.windows?
			buildVisualStudio(targets, clean)
		else
			buildMakefile(targets, clean)
		end
	
		#make_args = ""
		#
		#if clean == true
		#	make_args += " --clean-first"
		#end
		#
		#if @cmake_version >= Gem::Version.new('3.12') # --parallel supported?
		#	make_args += " -j #{getNumLogicalCores()} --parallel #{getNumLogicalCores()}"
		#elsif OS.unix?
		#	make_args += " -- -j #{getNumLogicalCores()}" # pass -j arg to make directly
		#end
		#
		#make_args += " -- /m"
		#
		#Dir.chdir(@build_dir) do
		#	exec_command("cmake --build . -v --target #{target} --config #{config_name} #{make_args}")
		#end
		#
		#FileUtils.touch("#{@install_dir}/glare-build.success")
	end
	

	def self.successFilename()
		return "glare-build.success"
	end


	def self.checkInstall(install_dir, epoch = 0)
		success_file_path = "#{install_dir}/#{successFilename()}"

		# File doesn't exist: build was unsuccessful.
		if !File.exist?(success_file_path)
			return false
		end

		success_file_data = File.read(success_file_path).split

		# Epoch is 0 and file is empty: valid install.
		if epoch == 0 && success_file_data.empty?
			return true
		end

		# Epoch in file is >= epoch: valid install.
		if success_file_data[0].to_i >= epoch.to_i
			return true
		end

		return false
	end


	# User can increase epoch to force a rebuild.
	def install(epoch = 0)
		if OS.windows?
			installVisualStudio()
		else
			installMakefile()
		end

		File.write("#{@install_dir}/" + CMakeBuild.successFilename(), epoch.to_s, mode: "w")
	end
	
	
	#
	# 'private' stuff from here.
	#
	def installVisualStudio()
		msbuildpath = getMSBuildPath()
		install_proj_path = getInstallProjectPath()
		
		config_name = getConfigName()
		platform = "x64"
		
		exec_command("\"#{msbuildpath}\" \"#{install_proj_path}\" /target:build /p:Configuration=#{config_name} /p:Platform=#{platform}")
	end
	
	
	def installMakefile()
		Dir.chdir(@build_dir) do
			exec_command("make install")
		end
	end
	
	
	def buildMakefile(targets, clean)
		Dir.chdir(@build_dir) do
			exec_command("make clean") if clean == true
			
			make_targets = ""
			if targets.length > 0
				puts "#{@build_name} CMake build: Building targets: "
				puts targets.map { |p| "\t" + p }.join("\n")
				
				make_targets = targets.join(" ")
			end
			
			exec_command("make -j #{getNumLogicalCores()} #{make_targets}")
		end
	end
	
	
	def buildVisualStudio(targets, clean)
		msbuildpath = getMSBuildPath()
		solution_path = getSolutionPath()
		
		config_name = getConfigName()
		platform = "x64"
		
		if targets.length > 0
			puts "#{@build_name} CMake build: Building targets: "
			puts targets.map { |p| "\t" + p }.join("\n")
		end
		
		if clean == true
			target_clean_string = "clean"
			if targets.length > 0
				target_clean_string = targets.map { |p| p + ":clean" }.join(";")
			end
			
			print_and_exec_command("\"#{msbuildpath}\" \"#{solution_path}\" /target:#{target_clean_string} /p:Configuration=#{config_name} /p:Platform=#{platform}")
		end
		
		target_build_string = "build"
		if targets.length > 0
			target_build_string = targets.join(";")
		end
		
		print_and_exec_command("\"#{msbuildpath}\" \"#{solution_path}\" /target:#{target_build_string} /p:Configuration=#{config_name} /p:Platform=#{platform} /m")
	end
	
	
	#def getOutputDirName(configuration, dir_type, vs_version = -1)
	#	if OS.windows?
	#		if vs_version == -1
	#			STDERR.puts "#{@build_name} CMake: VS version not set."
	#			exit 1
	#		end
	#		
	#		return "#{@build_name.downcase}-#{@build_version}-#{dir_type}-vs#{vs_version}#{@@config_opts[configuration][1]}"
	#	else
	#		return "#{@build_name.downcase}-#{@build_version}-#{dir_type}#{@@config_opts[configuration][1]}"
	#	end
	#end
	#
	#
	#def getBuildDir(configuration, vs_version = -1)
	#	getOutputDirName(configuration, "build", vs_version)
	#end
	#
	#
	#def getInstallDir(configuration, vs_version = -1)
	#	getOutputDirName(configuration, "install", vs_version)
	#end
	
	
	def getConfigName()
		if @configuration == nil
			STDERR.puts "#{@build_name} CMake: Error: Failed to get config name. Not configured."
			exit(1)
		end
		
		return @@config_opts[@configuration][0]
	end
	
	
	@@config_opts = {
		:debug => ["Debug", "-debug"],
		:sdkdebug => ["SDKDebug", "-sdkdebug"],
		:relwithdebinfo => ["RelWithDebInfo", "-relwithdebinfo"],
		:release => ["Release", ""]
	}
	
	def self.config_opts
		# Return the value of this variable
		@@config_opts
	end
	
	
	#
	# VS specific stuff from here.
	#
	def getInstallProjectPath()
		if @build_dir == nil
			STDERR.puts "#{@build_name} CMake: Error: Failed to get install project path. Not initialised."
			exit(1)
		end
	
		install_project_path = "#{@build_dir}/INSTALL.vcxproj"
		if !File.exist?(install_project_path)
			STDERR.puts "#{@build_name} CMake: Error: Install project file does not exist."
			exit(1)
		end
		
		return install_project_path
	end
	
	
	def getSolutionPath()
		if @build_dir == nil
			STDERR.puts "#{@build_name} CMake: Error: Failed to get solution path. Not initialised."
			exit(1)
		end
	
		sln_paths = Dir["#{@build_dir}/*.sln".gsub("\\", "/")] # file globbing only works with forwards slashes

		if sln_paths.length != 1 || sln_paths.first == ""
			STDERR.puts "#{@build_name} CMake: Error: Solution file not found."
			exit(1)
		end
		return sln_paths.first
	end
	
	
	def getVSGenerator()
		if @vs_version == nil
			STDERR.puts "#{@build_name} CMake: Error: Failed to get VS generator. Not configured."
			exit(1)
		end
		
		return @@vs_versions[@vs_version][0]
	end
	
	
	def getVSToolset()
		if @vs_version == nil
			STDERR.puts "#{@build_name} CMake: Error: Failed to get VS toolset. Not configured."
			exit(1)
		end
		
		return @@vs_versions[@vs_version][1]
	end
	
	
	def getMSBuildPath()
		# Get the lastest available MSBuild
		[2022,2019,2017,2015,2012].each do |vs|
			msbuild_path = @@vs_versions[vs][2]
			return msbuild_path if File.exist?(msbuild_path)
		end
		
		STDERR.puts "#{@build_name} CMake: Error: No supported version of MSBuild found on system."
		exit(1)
		
		#if @vs_version == nil
		#	STDERR.puts "#{@build_name} CMake: Error: Failed to get MSBuild path. Not configured."
		#	exit(1)
		#end
		#
		#return @@vs_versions[@vs_version][2]
	end
	
	
	@@vs_versions = {
		2012 => ["Visual Studio 11 Win64", "v110", "#{ENV['WINDIR']}\\Microsoft.NET\\Framework\\v4.0.30319\\msbuild.exe"],
		2013 => ["Visual Studio 12 Win64", "v120", 'C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe'],
		2015 => ["Visual Studio 14 Win64", "v140", 'C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe'],
		2017 => ["Visual Studio 15 Win64", "v141", 'C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe'],
		2019 => ["Visual Studio 16", "v142", 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe'],
		2022 => ["Visual Studio 17", "v143", 'C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\MSBuild.exe']
	}
end

