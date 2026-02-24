#
# Downloads and builds Qt
#
#
#
# To debug build, can do something like this: (linux)
# cd /home/nick/libs/Qt/5.13.2_build
# /home/nick/libs/Qt/qt-everywhere-src-5.13.2/configure  -prefix /home/nick/libs/Qt/5.13.2 -opensource -confirm-license -nomake tests -nomake examples -skip qt3d -skip qtactiveqt -skip qtandroidextras -skip qtcanvas3d -skip qtcharts -skip qtconnectivity -skip qtdatavis3d -skip qtgraphicaleffects -skip qtlocation -skip qtpurchasing -skip qtquickcontrols -skip qtquickcontrols2 -skip qtscript -skip qtscxml -skip qtsensors -skip qtserialbus -skip qtserialport -skip qtsvg -skip qtvirtualkeyboard -skip qtwayland -skip qtwebchannel -skip qtwebengine -skip qtwebsockets -skip qtwebview -skip qtxmlpatterns -skip qtremoteobjects -skip qtwebglplugin -skip qtnetworkauth -skip qtspeech -skip qtwinextras -skip qtmacextras -qt-xcb -qt-libpng
#
# or on windows:
# Open visual studio native tools x64 command prompt
# cd c:/programming/Qt/6.2.4-vs2022-64_build
# "c:/programming/Qt\qt-everywhere-src-6.2.4\configure.bat" -prefix "c:/programming/Qt/6.2.4-vs2022-64" -opensource -confirm-license -force-debug-info -nomake tests -nomake examples -opengl desktop -no-dbus  -skip qt3d -skip qtactiveqt -skip qtandroidextras -skip qtcanvas3d -skip qtcharts -skip qtconnectivity -skip qtdatavis3d -skip qtgraphicaleffects -skip qtlocation -skip qtpurchasing -skip qtquickcontrols -skip qtquickcontrols2 -skip qtscript -skip qtscxml -skip qtsensors -skip qtserialbus -skip qtserialport -skip qtsvg -skip qtvirtualkeyboard -skip qtwayland -skip qtwebchannel -skip qtwebengine -skip qtwebsockets -skip qtwebview -skip qtxmlpatterns -skip qtremoteobjects -skip qtwebglplugin -skip qtnetworkauth -skip qtspeech -skip qtmacextras -skip qtx11extras -debug-and-release -mp -platform win32-msvc
#
# Make sure to delete CMakeCache.txt before reconfiguring stuff!
#
#
# On Mac there now seems to be a problem where Qt doesn't install the header files properly into the 'include' dir.  We can fix that ourselves:
# cd /home/nick/programming/libs/Qt/5.15.10
# mkdir include/QtCore
# cp lib/QtCore.framework/Headers/* include/QtCore/
# mkdir include/QtWidgets/
# cp lib/QtWidgets.framework/Headers/* include/QtWidgets/
# mkdir include/QtGui
# cp lib/QtGui.framework/Headers/* include/QtGui/
# mkdir include/QtOpenGL
# cp lib/QtOpenGL.framework/Headers/* include/QtOpenGL/
# mkdir include/QtMultimedia
# cp lib/QtMultimedia.framework/Headers/* include/QtMultimedia/
# mkdir include/QtNetwork
# cp lib/QtNetwork.framework/Headers/* include/QtNetwork/
# mkdir include/QtGamepad
# cp lib/QtGamepad.framework/Headers/*.h include/QtGamepad/
#

require 'fileutils'
require 'digest'
require 'securerandom'
require 'net/http'
require './script_utils.rb'
require './config-lib.rb'
require './cmake.rb'


puts "-------------------------------------
Qt build

"


$config = :debug_and_release
$forcerebuild = false
$build_epoch = 2


def printUsage()
	puts "Usage: build_qt.rb [arguments]"
	puts ""
	puts "\t--release, -R\t\tSpecifies the Qt release to build. Default is #{$qt_version}."
	puts ""
	puts "\t--config, -c\t\tSpecifies the config to build. Valid options are: Release, Debug. Default is: Both."
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
			puts "Using default version: #{$qt_version}"
		else
			$qt_version = opt[1]
		end
	elsif opt[0] == "--help" || opt[0] == "-h"
		printUsage()
		exit 0
	elsif opt[0] == "--config" || opt[0] == "-c"
		if opt[1].downcase == "release"
			$config = :release
		elsif opt[1].downcase == "debug"
			$config = :debug
		elsif opt[1].downcase == "both"
			$config == :debug_and_release
		else
			puts "Invalid config: #{opt[0]}"
			exit 1
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


def getQtSource()
	puts "Downloading Qt release #{$qt_version}..."
	
	qt_source_file = "#{$qt_source_name}#{OS.windows? ? ".zip" : ".tar.xz"}"
	
	downloadFileHTTPSIfNotOnDisk(qt_source_file, "https://download.qt.io/archive/qt/#{$qt_version_major}.#{$qt_version_minor}/#{$qt_version}/single/" + qt_source_file)

	# Check checksum/hash of download
	sha256 = Digest::SHA256.file qt_source_file
	digest = sha256.hexdigest
	puts "SHA 256 digest: #{digest}"

	target_digest = OS.windows? ? $zip_sha256_digest : $tar_sha256_digest

	if(digest != target_digest)
		puts "ERROR: SHA 256 digest for download is wrong."
		exit(1)
	end
	puts "Digest is correct."
	
	puts "getQtSource() done."
end


def skipModuleString()
	skip_modules = [
			"qt3d",
			"qtactiveqt",
			"qtandroidextras",
			#"qtbase",
			"qtcanvas3d",
			"qtcharts",
			"qtconnectivity",
			"qtdatavis3d",
			#"qtdeclarative",
			#"qtdoc",
			#"qtgamepad",
			"qtgraphicaleffects",
			#"qtimageformats",
			"qtlocation",
			#"qtmultimedia",
			"qtpurchasing",
			"qtquickcontrols",
			"qtquickcontrols2",
			"qtscript",
			"qtscxml",
			"qtsensors",
			"qtserialbus",
			"qtserialport",
			"qtsvg",
			#"qttools",
			#"qttranslations",
			"qtvirtualkeyboard",
			"qtwayland",
			"qtwebchannel",
			"qtwebengine",
			"qtwebsockets",
			"qtwebview",
			"qtxmlpatterns"
		]

	if Gem::Version.new($qt_version) >= Gem::Version.new('5.11.0')
		skip_modules.push("qtremoteobjects").push("qtwebglplugin")
	end

	if Gem::Version.new($qt_version) >= Gem::Version.new('5.8.0')
		skip_modules.push("qtnetworkauth").push("qtspeech") # if OS.windows?
	end
		
	skip_modules.push("qtmacextras").push("qtx11extras") if OS.windows?
	skip_modules.push("qtwinextras").push("qtx11extras") if OS.mac?
	skip_modules.push("qtwinextras").push("qtmacextras") if OS.linux?
		
	skip_modules_string = ""
	
	skip_modules.each do |module_string|
		skip_modules_string = skip_modules_string + " -skip " + module_string
	end
	
	return skip_modules_string
end


def getInstallDir(vs_version = -1)
	if OS.windows?
		if vs_version == -1
			STDERR.puts "VS version not set."
			exit 1
		end
		
		return "#{$qt_version}-vs#{vs_version}-64"
	else
		return "#{$qt_version}"
	end
end


# Write a batch file with the right config
def buildWindows(config, vs_version)
	puts "Preparing vs#{vs_version} build."
	
	vs_versions = { 2012 => "11.0", 2013 => "12.0", 2015 => "14.0", 2017 => "15.0", 2019 => "16.0", 2022 => "17.0" }
	if vs_versions[vs_version].nil?
		puts "Error, invalid vs_version: #{vs_version}"
		exit(1)
	end
	
	# work out the min and max mkspek we can use. using older mkspec with newer vs versions is usually fine.
	min_vs_verison_supported = 2015 # just assume 2015 as the min now. TODO: Actually look it up in the docs.
	max_vs_verison_supported = nil
	if Gem::Version.new($qt_version) >= Gem::Version.new('5.15.0')
		max_vs_verison_supported = 2019
	elsif Gem::Version.new($qt_version) >= Gem::Version.new('5.9.0')
		max_vs_verison_supported = 2017
	else
		max_vs_verison_supported = 2015
	end
	
	if(vs_version < min_vs_verison_supported)
		puts "vs#{vs_version} not supported by Qt #{$qt_version}. Skipping."
		exit(0)
	end
	
	if $qt_version_major.to_i >= 6
		mkspec = "win32-msvc" #For qt 6.0+
	else
		mkspec = "win32-msvc#{[vs_version, max_vs_verison_supported].min}"
	end
	
	# We want the source dir we extract to to be unique for the VS version and bitness.
	# This is because the src dir can only be configured for one type of VS version and bitness, and we want to keep the source code
	# and PDBs around as well, which will be in this dir.
	# NOTE: As of Qt 5.7.1, it seems pdb files are installed to the prefix dir, so this is only needed for the source,
	# which means one source dir may be enough?

	install_dir_name = getInstallDir(vs_version)
	build_dir_name = install_dir_name + "_build"
	
	if Dir.exist?(install_dir_name)
		FileUtils.rm_r(install_dir_name)
		puts "Deleted old install dir."
	end
	
	FileUtils.mkdir(install_dir_name)
	
	if Dir.exist?(build_dir_name)
		FileUtils.rm_r(build_dir_name)
		puts "Deleted old build dir."
	end
	
	FileUtils.mkdir(build_dir_name)
	
	extractArchiveIfNotExtraced("#{$qt_source_name}.zip", $qt_source_name, true)
	
	if $qt_version_major.to_i == 5 && $qt_version_minor.to_i >= 15 && $qt_version_rev.to_i >= 3
        
        puts "Removing 'opensource' from source dir name..."
        puts "old $qt_source_name: #{$qt_source_name}"
        
        if File.exist?($qt_source_name + "/" + $qt_source_name_without_opensource)
            FileUtils.mv($qt_source_name + "/" + $qt_source_name_without_opensource, $qt_source_name_without_opensource, :verbose=>true);
        end
        $qt_source_name = $qt_source_name_without_opensource
        
        puts "new $qt_source_name: #{$qt_source_name}"
    end
	
	config_string = ""
	if config == :release
		config_string = "-release"
	elsif config == :debug
		config_string = "-debug"
	elsif config == :debug_and_release
		config_string = "-debug-and-release"
	#elsif config == :release-debug # This is the default when nothing is set.
	#	config_string = "-debug-and-release"
	end

	puts "config_string: '#{config_string}'"

	# TODO: make the path selection a bit nicer...
	
	skip_modules_string = skipModuleString()
		
	# You can set the VS_INSTALL_DRIVE env var to something like "D" if you have VS installed not on your C-drive.
	vs_install_drive = !ENV["VS_INSTALL_DRIVE"].nil? ? ENV["VS_INSTALL_DRIVE"] : "C"
	
	if vs_version >= 2022
		vs_dir = "#{vs_install_drive}:\\Program Files\\Microsoft Visual Studio\\#{vs_version}\\Community\\VC\\Auxiliary\\Build"
	elsif vs_version >= 2017
		vs_dir = "#{vs_install_drive}:\\Program Files (x86)\\Microsoft Visual Studio\\#{vs_version}\\Community\\VC\\Auxiliary\\Build"
	else
		vs_dir = "#{vs_install_drive}:\\Program Files (x86)\\Microsoft Visual Studio #{vs_versions[vs_version]}\\VC"
	end
	if !File.exist?(vs_dir)
		puts "Visual Studio dir not found: #{vs_dir}"
		exit(1)
	end
	
	# "The Microsoft Program Maintenance Utility (NMAKE.EXE) is a command-line tool included with Visual Studio. It builds projects based on commands that are contained in a description file, usually called a makefile.
	# NMAKE must run in a Developer Command Prompt window." - https://docs.microsoft.com/en-us/cpp/build/reference/nmake-reference?view=msvc-170

	if $qt_version_major.to_i >= 6  # Qt 6 changed to using cmake
		build_commands = <<EOF

cmake --build . --parallel || EXIT /B 1

cmake --install . || EXIT /B 1
EOF
	else
		build_commands = <<EOF

nmake || EXIT /B 1

nmake install || EXIT /B 1
EOF
	end



	s = <<EOF

CALL "#{vs_dir}\\vcvarsall.bat" amd64

CD "#{$indigo_qt_dir}\\#{build_dir_name}"

REM should we do -ltcg?
CALL "#{$indigo_qt_dir}\\#{$qt_source_name}\\configure.bat" -prefix "#{$indigo_qt_dir}/#{install_dir_name}" -opensource -confirm-license -force-debug-info -nomake tests -nomake examples -opengl desktop -no-dbus #{skip_modules_string} #{config_string} -mp -platform #{mkspec}
EOF
	
	s += build_commands
	
	batch_file_name = "build_qt_#{SecureRandom.hex}.bat"
	f = File.open(batch_file_name, "w")
	f << s
	f.close
	
	puts "Done."
	
	puts "Building Qt..."
	exec_command(batch_file_name)
	
	File.write("#{install_dir_name}/" + CMakeBuild.successFilename(), $build_epoch.to_s, mode: "w")
	
	# Delete temp stuff.
	FileUtils.rm(batch_file_name)
	
	# Delete the build dir when we are done.
	# Source is in source, pdb files are copied to install dir.
	if Dir.exist?(build_dir_name)
		FileUtils.rm_r(build_dir_name)
		puts "Deleted build dir."
	end
end


def patchFile(path)
	puts "Patching '#{path}'..."

	contents = File.open(path).read()

	#puts "contents: #{contents}"

	new_content = contents.gsub("QT_BEGIN_NAMESPACE", "#include <limits>\nQT_BEGIN_NAMESPACE")

	if new_content == contents
		puts "Patching failed, failed to find code to be replaced."
		exit(1)
	end
	
	File.open(path, 'w') { |file| file.write(new_content) }
end


# Adds "include <limits>" to a bunch of files, which is missing.  See https://bugreports.qt.io/browse/QTBUG-90395
def patchSource(qt_source_dir)
	
	puts "Patching source with <limits>.."
	
	puts "qt_source_dir: #{qt_source_dir}"

	patchFile(qt_source_dir + "/qtdeclarative/src/qmldebug/qqmlprofilerevent_p.h")
	patchFile(qt_source_dir + "/qtbase/src/corelib/global/qendian.h")
	#TEMP NOT in qt 5.15.3 patchFile(qt_source_dir + "/qtbase/src/corelib/tools/qbytearraymatcher.h")
	
	puts "Done patching source."
end



def buildUnix()
	install_dir_name = getInstallDir()
	build_dir_name = install_dir_name + "_build"

	if File.exist?(install_dir_name)
		FileUtils.rm_r(install_dir_name)
		puts "Deleted old install."
	end
	
	FileUtils.mkdir(install_dir_name)
	
	if Dir.exist?(build_dir_name)
		FileUtils.rm_r(build_dir_name)
		puts "Deleted old build dir."
	end
	
	FileUtils.mkdir(build_dir_name)
	
	extractArchiveIfNotExtraced("#{$qt_source_name}.tar.xz", $qt_source_name, true)
    
    if $qt_version_major.to_i == 5 && $qt_version_minor.to_i >= 15 && $qt_version_rev.to_i >= 3
        
        puts "Removing 'opensource' from source dir name..."
        puts "old $qt_source_name: #{$qt_source_name}"
        
        if File.exist?($qt_source_name + "/" + $qt_source_name_without_opensource)
            FileUtils.mv($qt_source_name + "/" + $qt_source_name_without_opensource, $qt_source_name_without_opensource, :verbose=>true);
        end
        $qt_source_name = $qt_source_name_without_opensource
        
        puts "new $qt_source_name: #{$qt_source_name}"
    end
	
	skip_modules_string = skipModuleString()
	
	# Note: -sdk is probably needed on OSX, otherwise verifying signature fails.
	# Also some xcode bug requires us to pass -isysroot to linker, for the same reason.
	if OS.mac? && $qt_version_major.to_i >= 6
		osx_args = OS.mac? ? "-debug-and-release -no-dbus -cmake-generator \"Ninja Multi-Config\"" : ""
	else
		osx_args = OS.mac? ? "-debug-and-release -no-dbus" : ""
	end

	# qt build fails with "Checking for valid makespec... ERROR: Cannot compile a minimal program. The toolchain or QMakeSpec is broken"
	#osx_args = OS.mac? ? "-debug-and-release -no-dbus -sdk macosx11.5 QMAKE_MACOSX_DEPLOYMENT_TARGET=10.13 QMAKE_LFLAGS=\"-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.15.sdk\"" : ""
	
	# fails with: Could not launch “gui_client”: The app is incompatible with the current version of macOS. Please check the app's deployment target.
	#osx_args = OS.mac? ? "-debug-and-release -no-dbus -sdk macosx11.3 QMAKE_MACOSX_DEPLOYMENT_TARGET=10.13 QMAKE_LFLAGS=\"-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX11.3.sdk\"" : ""
	#osx_args = OS.mac? ? "-debug-and-release -no-dbus -sdk macosx10.15 QMAKE_MACOSX_DEPLOYMENT_TARGET=10.13" : ""
	
	#osx_args = OS.mac? ? "-debug-and-release -no-dbus" : ""

	if $qt_version_major.to_i >= 6
		linux_args = OS.linux? ? "-xcb -qt-libpng" : "" # add -force-debug-info for debug info in release builds on linux.
	elsif $qt_version_major.to_i >= 5 && $qt_version_minor.to_i >= 15
		linux_args = OS.linux? ? "-bundled-xcb-xinput -qt-libpng" : "" # -qt-xcb option was removed in 5.15, see https://codereview.qt-project.org/c/qt/qtdoc/+/300877/3/doc/src/platforms/linux.qdoc
	else
		linux_args = OS.linux? ? "-qt-xcb -qt-libpng" : "" # add -force-debug-info for debug info in release builds on linux.
	end
	
	if $qt_version_major.to_i <= 6
		patchSource($qt_source_name) # Fix missing #include <limit> issue, seems to be fixed in 6.2.4.
	end

	Dir.chdir(build_dir_name) do
		exec_command("\"#{$indigo_qt_dir}/#{$qt_source_name}/configure\" -prefix \"#{$indigo_qt_dir}/#{install_dir_name}\" -opensource " +
			"-confirm-license -nomake tests -nomake examples " +
			"#{skip_modules_string} #{osx_args} #{linux_args}")

		if $qt_version_major.to_i >= 6 # At some point Qt changed to using cmake/ninja to do the actual building.
			exec_command("cmake --build . --parallel")
			exec_command("ninja install")
		else
			exec_command("make -j#{getNumLogicalCores()}")
			exec_command("make -j1 install")
		end

		File.write("../#{install_dir_name}/" + CMakeBuild.successFilename(), $build_epoch.to_s, mode: "w")
	end
	
	# Delete the build dir when we are done.
	# Source is in source, everything we need is copied to install dir.
	if Dir.exist?(build_dir_name)
		FileUtils.rm_r(build_dir_name)
		puts "Deleted build dir."
	end
end

version_split = $qt_version.split('.')
$qt_version_major = version_split[0]
$qt_version_minor = version_split[1]
$qt_version_rev = version_split[2]
$qt_source_name = "qt-everywhere-src-#{$qt_version}"
$qt_source_name_without_opensource = "qt-everywhere-src-#{$qt_version}"

if $qt_version_major.to_i == 5 && $qt_version_minor.to_i >= 15 && $qt_version_rev.to_i >= 3
    $qt_source_name = "qt-everywhere-opensource-src-#{$qt_version}"
end

if $qt_version == "6.2.4"
    $zip_sha256_digest = "af722a77d2acf07f48291fa8e720f8004e1e0738914ad89ab98142ec55b80fd3"
    $tar_sha256_digest = "cfe41905b6bde3712c65b102ea3d46fc80a44c9d1487669f14e4a6ee82ebb8fd"
elsif $qt_version == "6.2.2"
	$zip_sha256_digest = "03abc5194eab6e778098105316fb977958d9d9b5b154c9e264690481dcef7df2" # From https://download.qt.io/official_releases/qt/6.2/6.2.2/single/qt-everywhere-src-6.2.2.zip.mirrorlist
	$tar_sha256_digest = "907994f78d42b30bdea95e290e91930c2d9b593f3f8dd994f44157e387feee0f" # From https://download.qt.io/official_releases/qt/6.2/6.2.2/single/qt-everywhere-src-6.2.2.tar.xz.mirrorlist
elsif $qt_version == "5.15.16"
    $zip_sha256_digest = "b0c834a58ab216c5709f40bcd451ea6459ff43cd8cea98cc3ed2f0003ba36388" # From https://download.qt.io/official_releases/qt/5.15/5.15.16/single/qt-everywhere-opensource-src-5.15.16.zip.mirrorlist
    $tar_sha256_digest = "efa99827027782974356aceff8a52bd3d2a8a93a54dd0db4cca41b5e35f1041c" # From https://download.qt.io/official_releases/qt/5.15/5.15.16/single/qt-everywhere-opensource-src-5.15.16.tar.xz.mirrorlist
elsif $qt_version == "5.15.10"
    $zip_sha256_digest = "d44c5831c626b323379f11ad82c4cc6ac5a354b2b354dd0ab4b3d21047eb7061" # Computed with Digest::SHA256, checked md5 hash vs one on website.
    $tar_sha256_digest = "b545cb83c60934adc9a6bbd27e2af79e5013de77d46f5b9f5bb2a3c762bf55ca" # Computed with Digest::SHA256, checked md5 hash vs one on website.
elsif $qt_version == "5.15.4"
    $zip_sha256_digest = "76c0cac5c3db5bc5812121401abada6ff0842411a07d00803a76c5d8e9c4c3fd" # Computed with Digest::SHA256, checked md5 hash vs one on website.
    $tar_sha256_digest = "615ff68d7af8eef3167de1fd15eac1b150e1fd69d1e2f4239e54447e7797253b" # Computed with Digest::SHA256, checked md5 hash vs one on website.
elsif $qt_version == "5.15.3"
    $zip_sha256_digest = "bc590018b9e6e0eee21a5c479a6a975660ff87a18a45f76b1984c079d413b82b" # From https://download.qt.io/official_releases/qt/5.15/5.15.3/single/qt-everywhere-opensource-src-5.15.3.zip.mirrorlist
    $tar_sha256_digest = "b7412734698a87f4a0ae20751bab32b1b07fdc351476ad8e35328dbe10efdedb" # From https://download.qt.io/official_releases/qt/5.15/5.15.3/single/qt-everywhere-opensource-src-5.15.3.tar.xz.mirrorlist
elsif $qt_version == "5.15.2"
	$zip_sha256_digest = "6c5d37aa96f937eb59fd4e9ce5ec97f45fbf2b5de138b086bdeff782ec661733" # From https://download.qt.io/official_releases/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.zip.mirrorlist
	$tar_sha256_digest = "3a530d1b243b5dec00bc54937455471aaa3e56849d2593edb8ded07228202240" # From https://download.qt.io/official_releases/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.tar.xz.mirrorlist
elsif $qt_version == "5.15.0"
	$zip_sha256_digest = "8bf073f6ab3147f2fe72b753c1ebea83007deb2606e22a2804ebcf6467749469" # From https://download.qt.io/official_releases/qt/5.15/5.15.0/single/qt-everywhere-src-5.15.0.zip.mirrorlist
	$tar_sha256_digest = "22b63d7a7a45183865cc4141124f12b673e7a17b1fe2b91e433f6547c5d548c3" # From https://download.qt.io/official_releases/qt/5.15/5.15.0/single/qt-everywhere-src-5.15.0.tar.xz.mirrorlist
elsif $qt_version == "5.14.2"
	$zip_sha256_digest = "847f39c5b9db3eeee890a2aee3065ae81032287ab9d5812015ff9b37d19b64d6" # From https://download.qt.io/official_releases/qt/5.14/5.14.2/single/qt-everywhere-src-5.14.2.zip.mirrorlist
	$tar_sha256_digest = "c6fcd53c744df89e7d3223c02838a33309bd1c291fcb6f9341505fe99f7f19fa" # From https://download.qt.io/official_releases/qt/5.14/5.14.2/single/qt-everywhere-src-5.14.2.tar.xz.mirrorlist
elsif $qt_version == "5.13.2"
	$zip_sha256_digest = "8399208eb8d11771d2983f83e376e42036f2ff1f562049354fcaa9af8a7eeba9" # From https://download.qt.io/official_releases/qt/5.13/5.13.2/single/qt-everywhere-src-5.13.2.zip.mirrorlist
	$tar_sha256_digest = "55e8273536be41f4f63064a79e552a22133848bb419400b6fa8e9fc0dc05de08" # From https://download.qt.io/official_releases/qt/5.13/5.13.2/single/qt-everywhere-src-5.13.2.tar.xz.mirrorlist
elsif $qt_version == "5.13.0"
	$zip_sha256_digest = "68122be08b27c2b8371d532b7639e02633a91b8d28dc88dbcc9af76dab2cc9d9" # From https://download.qt.io/official_releases/qt/5.13/5.13.0/single/qt-everywhere-src-5.13.0.zip.mirrorlist
	$tar_sha256_digest = "2cba31e410e169bd5cdae159f839640e672532a4687ea0f265f686421e0e86d6" # From https://download.qt.io/official_releases/qt/5.13/5.13.0/single/qt-everywhere-src-5.13.0.tar.xz.mirrorlist
elsif $qt_version == "5.12.4"
	$zip_sha256_digest = "caab56aa411ef471d56bd6de54088bb38f0369de26dc874dd2703801d30d4ef9"
	$tar_sha256_digest = "85da5e0ee498759990180d5b8192efaa6060a313c5018b772f57d446bdd425e1"
elsif $qt_version == "5.11.3"
	$zip_sha256_digest = "b1a92f22ca595e5506eef2fc35c90a7d4b8a45c76d51c142cd60fec044fb08a1" # From https://download.qt.io/new_archive/qt/5.11/5.11.3/single/qt-everywhere-src-5.11.3.zip.mirrorlist
	$tar_sha256_digest = "859417642713cee2493ee3646a7fee782c9f1db39e41d7bb1322bba0c5f0ff4d" # From https://download.qt.io/new_archive/qt/5.11/5.11.3/single/qt-everywhere-src-5.11.3.tar.xz.mirrorlist
elsif $qt_version == "5.11.1"
	$zip_sha256_digest = "7e439f4a1786abc5a6040047f7e6e0e963c5a3516c1169141e99febb187d3832" # From https://download.qt.io/official_releases/qt/5.11/5.11.1/single/qt-everywhere-src-5.11.1.zip.mirrorlist
	$tar_sha256_digest = "39602cb08f9c96867910c375d783eed00fc4a244bffaa93b801225d17950fb2b" # From https://download.qt.io/official_releases/qt/5.11/5.11.1/single/qt-everywhere-src-5.11.1.tar.xz.mirrorlist
elsif $qt_version == "5.8.0"
	$zip_sha256_digest = "c57cf81c1394230c5a188b7601bb4c072314cb350d5d3d6b5b820426c60570e5" # From http://download.qt.io/official_releases/qt/5.8/5.8.0/single/qt-everywhere-opensource-src-5.8.0.zip.mirrorlist
	$tar_sha256_digest = "9dc5932307ae452855863f6405be1f7273d91173dcbe4257561676a599bd58d3" # From http://download.qt.io/official_releases/qt/5.8/5.8.0/single/qt-everywhere-opensource-src-5.8.0.tar.gz.mirrorlist
elsif $qt_version == "5.7.1"
	$zip_sha256_digest = "4e50c645ff614d831712f5ef19a4087b4c00824920c79e96fee17d9373b42cf3" # From https://download.qt.io/official_releases/qt/5.7/5.7.1/single/qt-everywhere-opensource-src-5.7.1.zip.mirrorlist
	$tar_sha256_digest = "c86684203be61ae7b33a6cf33c23ec377f246d697bd9fb737d16f0ad798f89b7" # From https://download.qt.io/official_releases/qt/5.7/5.7.1/single/qt-everywhere-opensource-src-5.7.1.tar.gz.mirrorlist
end
$glare_core_libs_dir = ENV['GLARE_CORE_LIBS']
if $glare_core_libs_dir.nil?
	puts "GLARE_CORE_LIBS env var not defined."
	exit(1)
end


$indigo_qt_dir = "#{$glare_core_libs_dir}/Qt"

FileUtils.mkdir($indigo_qt_dir, :verbose=>true) if !Dir.exist?($indigo_qt_dir)
puts "Chdir to \"#{$indigo_qt_dir}\"."
Dir.chdir($indigo_qt_dir)


# If force rebuild isn't set, skip the builds if the output exists.
if !$forcerebuild
	all_output_exists = true
	install_dir = getInstallDir($vs_version)
	all_output_exists = false if !CMakeBuild.checkInstall(install_dir, $build_epoch)
	
	if all_output_exists
		puts "Qt: Builds are in place, use --forcerebuild to rebuild."
		exit(0)
	end
end


Timer.time {

# Download the source.
getQtSource()

if OS.windows?
	buildWindows($config, $vs_version)
else
	buildUnix()
end

}

puts "Total build time: #{Timer.elapsedTime} s"

