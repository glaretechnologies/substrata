#
# Various utility methods for scripting.
#
#

require 'rubygems'
require 'open3'
require 'net/http'


def exec_command(cmd)
	res = Kernel.system(cmd)
	if !res
		STDERR.puts "Command failed: " + cmd
		exit(1)
	end
end


def exec_command_no_exit(cmd)
	res = Kernel.system(cmd)
	if !res
		STDERR.puts "Command failed: " + cmd
		return 1
	end

	return 0
end


def print_and_exec_command(cmd)
	puts "------Executing command------"
	puts cmd
	puts "-----------------------------"

	exec_command(cmd)
end


def svn_export(from_path, to_path)
	exec_command("svn export \"#{from_path}\" \"#{to_path}\" --force")
end



def get_cyberspace_version
	versionfile = IO.readlines("../shared/Version.h").join

	versionmatch = versionfile.match(/cyberspace_version\s+=\s+\"(.*)\"/)

	if versionmatch.nil? || versionmatch[1].nil?
		puts "Failed to extract version number from Version.h"
		exit(1)
	end

	version = versionmatch[1]
	version
end


def ssh_make_dir_remote(host, username, remote_path, port=22, winAllowSCP=true)
	puts "Making dir: #{host}:#{remote_path}"

	#Check if openssh ssh is available
	if (OS.windows? && winAllowSCP && sshAgentRunning() || OS.unix?) && programAvailable("ssh")
		puts "Using SSH"

		exec_command("ssh -P #{port} #{username}@#{host} \"mkdir -p #{remote_path}\"")

	elsif programAvailable("plink") # Use pscp
		puts "Using PLINK"

		exec_command("plink -no-antispoof -P #{port} #{username}@#{host} \"mkdir -p #{remote_path}\"")
	else
		raise "Cannot create remote directory. SSH and PLINK not available."
	end
end


def convert_to_msys_path(path)
	if OS.windows?
		if path[1] == ":"[0] # We replace : in absolute path since SCP doesn't like it
			path = "/" + path.gsub(":", "")
		end

		path = path.gsub("\\", "/")

		return path
	else
		return path
	end
end


def programAvailable(name)
	ssh_out, s = Open3.capture2e(name)

	#puts ssh_out

	if ssh_out.empty?
		false
	else
		true
	end
end


def listProgramsAvaliable()
	puts "ssh available" if programAvailable("ssh")
	puts "scp available" if programAvailable("scp")
	puts "pscp available" if programAvailable("pscp")
	puts ""
end


def sshAgentRunning()
	ssh_agent_pid = ENV['SSH_AGENT_PID']

	if ssh_agent_pid == nil
		return false
	else
		puts "SSH agent seems to be running (SSH_AGENT_PID env var is set)"
		return true if OS.windows? #do not continue to get the pid on windows.
	end

	begin
		Process.getpgid( ssh_agent_pid )
		puts "SSH agent is running"
		return true
	rescue Errno::ESRCH
		return false
	end

end


def scp_upload(host, username, local_path, remote_path, port=22, winAllowSCP=true)
	local_filename = File.basename(local_path)
	puts "Uploading: '#{local_filename}' to '#{remote_path}' on host '#{host}'"
	
	start = Time.now

	#Check if openssh scp is available
	if (OS.windows? && winAllowSCP && sshAgentRunning() || OS.unix?) && programAvailable("scp")
		puts "Using SCP"
		mysy_local_path = convert_to_msys_path(local_path)

		exec_command("scp -P #{port} \"#{mysy_local_path}\" #{username}@#{host}:#{remote_path}")

	elsif programAvailable("pscp") # Use pscp
		puts "Using PSCP"
		raise ArgumentError, 'Upload error: Local file path includes wildcards. Wildcards are not supported by pscp.' if local_path.include?('*')

		exec_command("pscp -P #{port} \"#{local_path}\" #{username}@#{host}:#{remote_path}")
	else
		raise "Cannot upload. SCP and PSCP not available."
	end

	elapsed = Time.now - start
	file_size = File.size?(local_path)

	if file_size != nil
		puts "Uploaded: " + sprintf("%.2f", file_size / (1024.0 * 1024.0)) + " MB | " + sprintf("%.2f", file_size.to_f / (elapsed * 1024.0)) + " KB/s | " + Time.at(elapsed).gmtime.strftime('%R:%S')
		puts ""
	end
end


def scp_download(host, username, remote_path, local_path, port=22, winAllowSCP=true)
	remote_filename = File.basename(remote_path)
	local_filename = File.basename(local_path)
	puts "Downloading: #{remote_filename}"

	start = Time.now

	#Check if openssh scp is available
	if (OS.windows? && winAllowSCP && sshAgentRunning() || OS.unix?) && programAvailable("scp")
		puts "Using SCP"
		msys_local_path = convert_to_msys_path(local_path)

		exec_command("scp -P #{port} #{username}@#{host}:#{remote_path} \"#{msys_local_path}\"")
	elsif programAvailable("pscp") # Use pscp
		puts "Using PSCP"
		exec_command("pscp -P #{port} #{username}@#{host}:#{remote_path} \"#{local_path}\"")
	else
		raise "Cannot upload. SCP and PSCP not available."
	end

	elapsed = Time.now - start
	file_size = File.size?(local_path)

	if file_size != nil
		puts "Downloaded: " + sprintf("%.2f", file_size / (1024.0 * 1024.0)) + " MB | " + sprintf("%.2f", file_size.to_f / (elapsed * 1024.0)) + " KB/s | " + Time.at(elapsed).gmtime.strftime('%R:%S')
		puts ""
	end
end


# Handles HTTP redirects.
def doDownloadFileHTTPS(disk_path, uri_string, num_redirects)
	uri = URI(uri_string)

	puts "Downloading '#{uri_string}' to '#{disk_path}'..."

	throw "Too many redirects." if num_redirects > 10

	path_and_query = uri.path + (uri.query ? ("?" + uri.query) : "")

	Net::HTTP.start(uri.host, :use_ssl => uri.scheme == 'https') do |http|
		req = Net::HTTP::Get.new path_and_query

		http.request(req) do |response|
			case response
				when Net::HTTPSuccess then
					open(disk_path, 'wb') do |file|
						response.read_body do |chunk|
							file.write chunk
						end
					end
				when Net::HTTPRedirection then
					location = response['Location']
					puts ""
					puts "redirected to #{location}"
					puts ""
					doDownloadFileHTTPS(disk_path, location, num_redirects + 1)
				else
					puts "ERROR: " + response.value
					exit(1)
				end
		end
	end
end


def downloadFileHTTPS(disk_path, uri_string)
	doDownloadFileHTTPS(disk_path, uri_string, 0)
end

def downloadFileHTTPSIfNotOnDisk(disk_path, uri_string)

	if File.exist?(disk_path)
		puts "Already present on disk at '#{disk_path}', skipping download of '#{uri_string}'."
		return
	end

	downloadFileHTTPS(disk_path, uri_string)
end


# Returns true if tar.exe is present on the system.  Should be built into Windows 10 build 17063+ (see https://blogs.windows.com/windows-insider/2017/12/19/announcing-windows-10-insider-preview-build-17063-pc/)
def haveTar()
	return Kernel.system("tar --version") # "system returns true if the command gives zero exit status, false for non zero exit status"
end

# Extract the archive to the current working directory.
def extractArchive(archive, silent = false)
	if OS.windows?
		if haveTar() && !archive.include?(".tar.xz") # Windows tar doesn't support xz however.
			puts "Extracting #{archive} with tar..."
			exec_command("tar -x -f \"#{archive}\"")
		else
			sevenz_path = "7z.exe"
			silent_flag = silent ? " > nul" : ""

			if archive.include?(".tar.gz") || archive.include?(".tar.xz")
				puts "Extracting #{archive} (silently)..."
				# Extract with 7zip. -y option won't show any prompts and assumes yes in all prompts. Redirect to nul because there is no silent option.
				exec_command("\"#{sevenz_path}\" x #{archive} -y#{silent_flag}")
				puts "Done."

				tar_archive = File.basename(archive[0..-4]) # remove ".gz"

				puts "Extracting #{tar_archive} (silently)..."
				exec_command("\"#{sevenz_path}\" x #{tar_archive} -y#{silent_flag}")
				puts "Done."

				if File.exist?(tar_archive)
					FileUtils.rm_r(tar_archive)
				end
			elsif archive.include?(".zip")
				puts "Extracting #{archive} (silently)..."
				exec_command("\"#{sevenz_path}\" x #{archive} -y#{silent_flag}")
				puts "Done."
			end
		end
	else
		if archive.include?(".tar.gz") || archive.include?(".tar.xz")
			silent_flag = silent ? "" : "v"
			puts "Extracting #{archive}..."
			exec_command("tar -x#{silent_flag}f #{archive}")
			puts "Done."
		elsif archive.include?(".zip")
			puts "Extracting #{archive}..."
			exec_command("unzip #{archive}")
			puts "Done."
		end
	end
end


def extractArchiveIfNotExtraced(archive, target_dir, silent = false)
	if File.exist?("#{target_dir}/glare-extract.success")
		puts "Already extracted '#{archive}', skipping..."
		return
	end
	
	temp_extract_dir = "temp_#{target_dir}"
	
	FileUtils.rm_r(target_dir) if Dir.exist?(target_dir)
	FileUtils.rm_r(temp_extract_dir) if Dir.exist?(temp_extract_dir)
	
	FileUtils.mkdir(temp_extract_dir)
	
	Dir.chdir(temp_extract_dir) do
		extractArchive("../" + archive, silent)
	end
	
	if Dir.exist?(temp_extract_dir + "/" + target_dir)
		FileUtils.touch("#{temp_extract_dir}/#{target_dir}/glare-extract.success")
		FileUtils.mv(temp_extract_dir + "/" + target_dir, ".")
		FileUtils.rm_r(temp_extract_dir)
	else
		STDERR.puts "WARNING: #{archive} didn't extract to expected target dir #{target_dir}, move into target dir and pray."
		FileUtils.touch("#{temp_extract_dir}/glare-extract.success")
		FileUtils.mv(temp_extract_dir, target_dir)
	end
end


module OS
	def OS.windows?
		(/cygwin|mswin|mingw|bccwin|wince|emx/ =~ RUBY_PLATFORM) != nil
	end

	def OS.mac?
		(/darwin/ =~ RUBY_PLATFORM) != nil
	end

	def OS.arm64?
		(/arm64/ =~ RUBY_PLATFORM) != nil
	end

	def OS.unix?
		!OS.windows?
	end

	def OS.linux?
		OS.unix? and not OS.mac?
	end

	def OS.linux64?
		OS.linux? and (/64/ =~ RUBY_PLATFORM) != nil
	end
end


def getNumLogicalCores()
	if OS.mac?
		$num_threads = `sysctl -n hw.logicalcpu`.strip
	elsif OS.linux?
		$num_threads = `grep -c ^processor /proc/cpuinfo`.strip
	elsif OS.windows?
		$num_threads = `echo %NUMBER_OF_PROCESSORS%`.strip
	else
		STDERR.puts "Unknown OS, can't get number of logical cpu cores."
		exit 1
	end
end


# A simple argument parser.
# It handles:
#	* long args "--xxxx" also with values "--xxxxx yyyyyy"
#	* short args "-x" also with values "-x yyyyyy"
#	* lists of short args "-xxxxx" without values
#
# It does not handle unnamed arguments!
class ArgumentParser
	attr_accessor :options, :input

	# An array of string argumenst. ARGV, for example.
	def initialize(args_in)
		@input = args_in
		@options = []
		parse()
	end

	private # All private from here

	def starts_with?(string, characters)
		string.match(/^#{characters}/) ? true : false
	end


	def nextVal(idx)
		next_arg = @input[idx+1]
		if next_arg != nil
			if !starts_with?(next_arg, "-")
				return next_arg
			end
		end

		return nil
	end


	def addToOptions(arg, value)
		if arg != nil
			option = [arg, value]

			@options.push(option)
		end
	end


	def parse()
		i = 0
		while i < @input.length
			current_arg = @input[i]
			#puts current_arg

			if starts_with?(current_arg, "--") # if its a long arg
				arg = current_arg

				value = nextVal(i)

				if value != nil
					i = i + 1
				end

				addToOptions(arg, value)
			elsif starts_with?(current_arg, "-") && current_arg.length == 2 # A short arg
				arg = current_arg

				value = nextVal(i)
				if value != nil
					i = i + 1
				end

				addToOptions(arg, value)
			elsif starts_with?(current_arg, "-") && current_arg.length > 2 # A list of short args. Note: lists of short args can't have values.
				args = current_arg[1..-1]

				args.split("").each do |j|
					addToOptions("-#{j}", nil)
				end

			elsif !starts_with?(current_arg, "-") # Unnamed arg. Doesn't support unnamed. Raise.
				raise ArgumentError, "Found unnamed argument: '#{current_arg}'"
			else
				raise ArgumentError, "What the hell are you doing? ... #{current_arg}"
				puts "Error!"
			end

			i = i + 1
		end
	end
end


def copyAllFilesInDir(dir_from, dir_to)
	if !Dir.exist?(dir_from)
		STDERR.puts "Error: Source directory doesn't exist: #{dir_from}"
		exit(1)
	end
	
	if !Dir.exist?(dir_to)
		STDERR.puts "Error: Target directory doesn't exist: #{dir_to}"
		exit(1)
	end

	Dir.foreach(dir_from) do |f|
		FileUtils.cp(dir_from + "/" + f, dir_to, verbose: true) if f != ".." && f != "."
	end
end


def copyAllFilesInDirDelete(dir_from, dir_to)
	if !Dir.exist?(dir_from)
		STDERR.puts "Error: Source directory doesn't exist: #{dir_from}"
		exit(1)
	end
	
	if !Dir.exist?(dir_to)
		STDERR.puts "Error: Target directory doesn't exist: #{dir_to}"
		exit(1)
	end

	Dir.foreach(dir_from) do |f|
		if f != ".." && f != "."
			FileUtils.rm(dir_to + "/" + f, verbose: true, force: true)
			FileUtils.cp(dir_from + "/" + f, dir_to, verbose: true)
		end
	end
end


def getAndCheckEnvVar(name)
	env_var = ENV[name]

	if env_var.nil?
		STDERR.puts "#{name} env var not defined."
		exit(1)
	end

	puts "#{name}: #{env_var}"
	return env_var
end


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

