#!/usr/bin/ruby

# This is a utility program to be able to use the config
# from config-lib.rb in non ruby scripts by reading the output
# as a string.
# The version of Qt we use needs to be accessed by both CMake and ruby build_dist script etc..

require './script_utils.rb'
require './config-lib.rb'


arg_parser = ArgumentParser.new(ARGV)

arg_parser.options.each do |opt|
	if opt[0] == "--qtversion"
		puts $qt_version
		exit 0
	elsif opt[0] == "--qtdir"
		puts $indigo_qt_dir
		exit 0
	elsif opt[0] == "--version"
		puts get_substrata_version()
		exit 0
	else
		puts "Unrecognised argument: #{opt[0]}"
		exit 1
	end
end