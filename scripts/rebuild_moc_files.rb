#
# Rebuilds .moc files.  Needs to be done before CMake is run.
#
#

require 'fileutils'
require './script_utils.rb'
require './config-lib.rb'


$indigo_libs_dir = ENV['INDIGO_LIBS']
$indigo_qt_dir = "#{$indigo_libs_dir}/Qt"
if $indigo_libs_dir.nil?
	puts "INDIGO_LIBS env var not defined."
	exit(1)
end

$mocbin = ""

if OS.unix?
	$mocbin = "#{$indigo_qt_dir}/#{$qt_version}/bin/moc"
else
	$mocbin = "#{$indigo_qt_dir}/#{$qt_version}-vs#{$vs_version}-64/bin/moc.exe"
end

if(not File.exist?($mocbin))
	STDERR.puts "moc not found: #{$mocbin}"
	exit 1
end


def removeNLines(string, lines)
	#string = string.lines.to_a[lines .. -1].join
	# http://blog.grayproductions.net/articles/getting_code_ready_for_ruby_19
	if(string == "")
		return ""
	else
		return string.send(string.respond_to?(:lines) ? :lines : :to_s).to_a[lines .. -1].join
	end
end


def generateIfChanged(file)
	moc_out_filename = "moc_#{file}.cpp"
	
	if(File.exists?(moc_out_filename))
		# compare files. does it need to be rebuilt
		temp_file = "moc_temp.cpp"
		
		Kernel.system("#{$mocbin} #{file}.h > #{temp_file}")
		
		file = File.open(temp_file, "rb")
		temp_file_contents = removeNLines(file.read, 8)
		#puts(temp_file_contents)
		file.close
		
		file = File.open(moc_out_filename, "rb")
		moc_file_contents = removeNLines(file.read, 8)
		#puts(moc_file_contents)
		file.close
		
		if(temp_file_contents != moc_file_contents)
			FileUtils.rm(moc_out_filename)
			FileUtils.mv(temp_file, moc_out_filename)
			puts("file updated: #{moc_out_filename}")
		else
			FileUtils.rm(temp_file)
		end
	else
		Kernel.system("#{$mocbin} #{file}.h > #{moc_out_filename}")
		puts("file generated: #{moc_out_filename}")
	end
end


def deleteIfExists(file)
	moc_out_filename = "moc_#{file}.cpp"
	
	if(File.exists?(moc_out_filename))
		FileUtils.rm(moc_out_filename)
		puts("file deleted: #{moc_out_filename}")
	end
end



FileUtils.cd("../gui_client")

generateIfChanged("MainWindow")
generateIfChanged("GuiClientApplication")
generateIfChanged("GlWidget")

