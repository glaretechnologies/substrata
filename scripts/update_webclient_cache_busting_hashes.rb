# Update some cache-busting hashes in webclient.html


require 'fileutils'
require 'digest'


def printUsage()
	puts "Usage: update_webclient_cache_busting_hashes.rb substrata_src_dir"
end


if(ARGV.length < 1)
	printUsage()
	exit(1)
end

substrata_dir = ARGV[0]



puts "------------Using directories------------"
puts "substrata_dir: #{substrata_dir}"
puts "-----------------------------------------"


# Copy some resources to build output dir while we're at it (used for testing)
# These resources are served one by one to the webclient instead of being packaged and preloaded.
cyberspace_output = ENV['CYBERSPACE_OUTPUT']
if cyberspace_output.nil?
	puts "CYBERSPACE_OUTPUT env var must be defined"
	exit(1)
end


# Return hash string in hexadecimal
def computeHashForFile(path)
	sha256 = Digest::SHA256.file path
	return sha256.hexdigest[0, 16]
end

def checkAndReplaceString(content, src_str, dest_str)
	if content.index(src_str).nil?
		raise "Could not find string '" + src_str + "' in file."
	end

	puts "Updating '" + src_str + "' with '" + dest_str + "' in webclient.html..."
	return content.gsub(src_str, dest_str)
end

# Replaces some target strings ("GUI_CLIENT_DATA_HASH") etc. in a string with the actual hash value of the file, as computed from the file on disk.
def doReplacementsForGeneratedFilesInDir(webclient_html_contents, output_dir)
	gui_client_data_hash = computeHashForFile(output_dir + "/gui_client.data")
	webclient_html_contents = checkAndReplaceString(webclient_html_contents, "GUI_CLIENT_DATA_HASH", gui_client_data_hash)

	gui_client_wasm_hash = computeHashForFile(output_dir + "/gui_client.wasm")
	webclient_html_contents = checkAndReplaceString(webclient_html_contents, "GUI_CLIENT_WASM_HASH", gui_client_wasm_hash)

	gui_client_js_hash = computeHashForFile(output_dir + "/gui_client.js")
	webclient_html_contents = checkAndReplaceString(webclient_html_contents, "GUI_CLIENT_JS_HASH", gui_client_js_hash)

	return webclient_html_contents
end

#-------------------- Update webclient.html in output dir (not test_builds) with hashes of the various files, for cache-busting ----------------------
puts "Updating webclient.html with hashes..."
if(File.exist?(cyberspace_output + "/gui_client.data"))
	webclient_html_contents = File.read(substrata_dir + "/webclient/webclient.html")

	webclient_html_contents = doReplacementsForGeneratedFilesInDir(webclient_html_contents, cyberspace_output)

	# Write updated webclient.html contents back to disk in the output directory.
	puts "Writing to " + cyberspace_output + "/webclient.html..."
	File.write(cyberspace_output + "/webclient.html", webclient_html_contents)
else
	puts cyberspace_output + "/gui_client.data not found, skipping."
end

#---------------------- Update webclient.html in test builds output dir with hashes of the various files, for cache-busting ----------------------
puts "Updating test_builds/webclient.html with hashes..."
if(File.exist?(cyberspace_output + "/test_builds/gui_client.data"))

	webclient_html_contents = File.read(substrata_dir + "/webclient/webclient.html")

	webclient_html_contents = doReplacementsForGeneratedFilesInDir(webclient_html_contents, cyberspace_output + "/test_builds")

	# Write updated webclient.html contents back to disk in the output directory.
	puts "Writing to " + cyberspace_output + "/test_builds/webclient.html..."
	File.write(cyberspace_output + "/test_builds/webclient.html", webclient_html_contents)
else
	puts cyberspace_output + "/test_builds/gui_client.data not found, skipping."
end

puts "Done."
