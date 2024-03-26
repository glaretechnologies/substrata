#
# Various utility methods for packing cyberspace distributions.
#
#

require 'fileutils'
require './script_utils.rb'
require './config-lib.rb'


def getCmakeBuildDir(vs_version, config)
	cyberspace_output = getAndCheckEnvVar('CYBERSPACE_OUTPUT')

	if OS.windows?
		return cyberspace_output + "/vs#{vs_version}/cyberspace_x64/#{config}"
	else
		if config == $config_name_release
			return cyberspace_output
		else
			return cyberspace_output + "/test_builds"
		end
	end
end


def copyQtRedistWindows(vs_version, target_dir, copy_debug = false)
	if (!OS.windows?)
		return
	end
	
	# Get Qt path.
	glare_core_libs_dir = getAndCheckEnvVar('GLARE_CORE_LIBS')
	qt_dir = "#{glare_core_libs_dir}/Qt/#{$qt_version}-vs#{vs_version}-64"
	lib_path = "#{qt_dir}/bin"
	plugins_path = "#{qt_dir}/plugins"
	
	# Qt dlls.
	dll_files = ["Qt5Core", "Qt5Gui",
		"Qt5OpenGL", "Qt5Widgets", "Qt5Multimedia", "Qt5Network", "Qt5Gamepad"]
		
	dll_files.each do |dll_file|
		FileUtils.cp("#{lib_path}/#{dll_file}.dll", target_dir, :verbose => true) if !copy_debug
		FileUtils.cp("#{lib_path}/#{dll_file}d.dll", target_dir, :verbose => true) if copy_debug
	end
	
	# Imageformats
	imageformats_dir = "#{plugins_path}/imageformats"
	imageformats_target_dir = "#{target_dir}/imageformats"
	
	FileUtils.mkdir_p(imageformats_target_dir, :verbose => true)
	
	image_formats = ["qjpeg"]
		
	image_formats.each do |format|
		FileUtils.cp("#{imageformats_dir}/#{format}.dll", imageformats_target_dir, :verbose => true) if !copy_debug
		FileUtils.cp("#{imageformats_dir}/#{format}d.dll", imageformats_target_dir, :verbose => true) if copy_debug
	end
	
	# Seems to work without copying runtime DLLs into these dirs.
	# copyVCRedist(vs_version, imageformats_target_dir, false)
	
	# copyVCRedist(vs_version, sqldrivers_target_dir, false)
	
	# Platfroms
	platforms_dir = "#{plugins_path}/platforms"
	platforms_dir_target_dir = "#{target_dir}/platforms"
	
	FileUtils.mkdir_p(platforms_dir_target_dir, :verbose => true)
	
	FileUtils.cp("#{platforms_dir}/qwindows.dll", platforms_dir_target_dir, :verbose => true) if !copy_debug
	FileUtils.cp("#{platforms_dir}/qwindowsd.dll", platforms_dir_target_dir, :verbose => true) if copy_debug
	
	# Styles
	styles_dir = "#{plugins_path}/styles"
	styles_dir_target_dir = "#{target_dir}/styles"
	
	FileUtils.mkdir_p(styles_dir_target_dir, :verbose => true)
	
	FileUtils.cp("#{styles_dir}/qwindowsvistastyle.dll", styles_dir_target_dir, :verbose => true) if !copy_debug
	FileUtils.cp("#{styles_dir}/qwindowsvistastyled.dll", styles_dir_target_dir, :verbose => true) if copy_debug

	# copyVCRedist(vs_version, platforms_dir_target_dir, false)
end


def copyCEFRedistWindows(target_dir)
	if (!OS.windows?)
		return
	end
	
	# Get CEF binary distibution path.
	cef_bin_distrib_dir = getAndCheckEnvVar('CEF_BINARY_DISTRIB_DIR')
	
	# See e.g. C:\cef\chromium\src\cef\binary_distrib\cef_binary_101.0.0-Unknown.0+gUnknown+chromium-101.0.4951.26_windows64\README.txt for needed files.
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libcef.dll", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/chrome_elf.dll", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/snapshot_blob.bin", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/v8_context_snapshot.bin", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/d3dcompiler_47.dll", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libEGL.dll", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libGLESv2.dll", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/vk_swiftshader.dll", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/vk_swiftshader_icd.json", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/vulkan-1.dll", target_dir, :verbose => true)
	FileUtils.cp_r(cef_bin_distrib_dir + "/Release/swiftshader", target_dir, :verbose => true)
	
	FileUtils.cp_r(cef_bin_distrib_dir + "/Resources/locales", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_100_percent.pak", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_200_percent.pak", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/icudtl.dat", target_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/resources.pak", target_dir, :verbose => true)
end

def copyCEFRedistLinux(target_dir, strip_symbols)

	# The rpath is set to look in lib, so put the CEF files there.
	target_lib_dir = target_dir + "/lib"

	FileUtils.mkdir_p(target_lib_dir, :verbose => true) if !File.exists?(target_lib_dir) # Make target_lib_dir if it doesn't exist already.
	
	# Get CEF binary distibution path.
	cef_bin_distrib_dir = getAndCheckEnvVar('CEF_BINARY_DISTRIB_DIR')
	
	# See e.g. C:\cef\chromium\src\cef\binary_distrib\cef_binary_101.0.0-Unknown.0+gUnknown+chromium-101.0.4951.26_windows64\README.txt for needed files.
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libcef.so",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/chrome-sandbox",			target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/snapshot_blob.bin",		target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/v8_context_snapshot.bin",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libEGL.so",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libGLESv2.so",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libvk_swiftshader.so",		target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/vk_swiftshader_icd.json",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Release/libvulkan.so.1",			target_lib_dir, :verbose => true)
	FileUtils.cp_r(cef_bin_distrib_dir + "/Release/swiftshader",			target_lib_dir, :verbose => true)
	
	FileUtils.cp_r(cef_bin_distrib_dir + "/Resources/locales",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_100_percent.pak",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/chrome_200_percent.pak",	target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/icudtl.dat",				target_lib_dir, :verbose => true)
	FileUtils.cp(cef_bin_distrib_dir + "/Resources/resources.pak",			target_lib_dir, :verbose => true)
	
	# We strip symbols for the Substrata distribution, because libcef.so is gigantic without stripping (e.g. 1.3 GB), and much smaller with stripping (e.g. 194 MB)
	if strip_symbols
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libcef.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libEGL.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libGLESv2.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libvk_swiftshader.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/libvulkan.so.1\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/swiftshader/libEGL.so\"")
		print_and_exec_command("strip --strip-unneeded \"#{target_lib_dir}/swiftshader/libGLESv2.so\"")
	end
end

def copyCEFRedistMac(cyb_output_dir, appdir)
    
    cef_binary_distrib_dir = getAndCheckEnvVar('CEF_BINARY_DISTRIB_DIR')

    FileUtils.mkdir(appdir + "/Contents/Frameworks", {:verbose=>true}) if !File.exists?(appdir + "/Contents/Frameworks") # Make frameworks dir if not existing.

    FileUtils.cp_r(cef_binary_distrib_dir + "/Release/Chromium Embedded Framework.framework", appdir + "/Contents/Frameworks", {:verbose=>true})

    # Copy helper apps.  Assuming they are built to the same directory as gui_client.app.
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper.app",            appdir + "/Contents/Frameworks", {:verbose=>true})
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper (Plugin).app",   appdir + "/Contents/Frameworks", {:verbose=>true})
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper (Renderer).app", appdir + "/Contents/Frameworks", {:verbose=>true})
    FileUtils.cp_r(cyb_output_dir + "/gui_client Helper (GPU).app",      appdir + "/Contents/Frameworks", {:verbose=>true})
end


# Copy BugSplat support files (See https://docs.bugsplat.com/introduction/getting-started/integrations/desktop/cplusplus)
def copyBugSplatRedist(dist_dir)

	bugsplat_dir = getAndCheckEnvVar('GLARE_CORE_LIBS') + "/BugSplat"
	FileUtils.copy(bugsplat_dir + "/BugSplat/x64/Release/BsSndRpt64.exe",   "#{dist_dir}/", :verbose => true)
	FileUtils.copy(bugsplat_dir + "/BugSplat/x64/Release/BugSplat64.dll",   "#{dist_dir}/", :verbose => true)
	FileUtils.copy(bugsplat_dir + "/BugSplat/x64/Release/BugSplatRc64.dll", "#{dist_dir}/", :verbose => true)
end


def copyVCRedist(vs_version, target_dir)
	if(vs_version == 2022)
		redist_path = "C:/Program Files/Microsoft Visual Studio/#{vs_version}/Community/VC/Redist/MSVC/14.32.31326/x64"
		
		copyAllFilesInDirDelete("#{redist_path}/Microsoft.VC143.CRT", target_dir)
	elsif(vs_version == 2019)
		redist_path = "C:/Program Files (x86)/Microsoft Visual Studio/#{vs_version}/Community/VC/Redist/MSVC/14.29.30133/x64"
		
		copyAllFilesInDirDelete("#{redist_path}/Microsoft.VC142.CRT", target_dir)
	else
		STDERR.puts "Unhandled vs version in copyVCRedist: #{vs_version}"
		exit 1
	end
end


def copyCyberspaceResources(substrata_repos_dir, glare_core_repos_dir, dist_dir, vs_version = $vs_version, config = $config_name_release, copy_build_output = true)
	
	FileUtils.mkdir_p("#{dist_dir}/data", :verbose => true) # Make 'data' dir, so that setting it as a target will make data/shaders be created etc..

	FileUtils.cp_r(substrata_repos_dir + "/resources", dist_dir + "/data", :verbose => true)
	
	FileUtils.cp_r(substrata_repos_dir + "/shaders", dist_dir + "/data", :verbose => true) # Copy OpenGL shaders from the Substrata repo.
	
	# Copy misc. files.
	#FileUtils.cp("../lang/ISL_stdlib.txt", dist_dir, :verbose => true)
	
	# Copy OpenGL shaders.
	FileUtils.cp_r("#{glare_core_repos_dir}/opengl/shaders", dist_dir + "/data", :verbose => true)
	
	# Copy OpenGL data.
	FileUtils.cp_r("#{glare_core_repos_dir}/opengl/gl_data", dist_dir + "/data", :verbose => true)

	# Copy licence.txt
	FileUtils.cp_r(substrata_repos_dir + "/docs/licence.txt", dist_dir + "/", :verbose => true)

	# Make sure files are group/other readable, they weren't for some reason.  The Dir.glob gets all files (recursively) in dir.
	FileUtils.chmod("u=wr,go=rr", Dir.glob("#{dist_dir}/data/shaders/*.*"))
	FileUtils.chmod("u=wr,go=rr", Dir.glob("#{dist_dir}/data/gl_data/*.*"))
end
