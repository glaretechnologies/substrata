#!/usr/bin/ruby

# This is a utility program to be able to use the config
# from config-lib.rb in non ruby scripts by reading the output
# as a string.

require './script_utils.rb'
require './config-lib.rb'


arg_parser = ArgumentParser.new(ARGV)

arg_parser.options.each do |opt|
	if opt[0] == "--qtversion"
		puts $qt_version
		exit 0
	elsif opt[0] == "--vsversion"
		puts $vs_version.to_s
		exit 0
	elsif opt[0] == "--llvmversion"
		puts $llvm_version
		exit 0
	elsif opt[0] == "--libresslversion"
		puts $libressl_version
		exit 0
	elsif opt[0] == "--opensslversion"
		puts $openssl_version
		exit 0
	elsif opt[0] == "--qtdir"
		puts $indigo_qt_dir
		exit 0
	else
		puts "Unrecognised argument: #{opt[0]}"
		exit 1
	end
end