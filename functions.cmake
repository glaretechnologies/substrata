
# Function for checking program is on path
function(programAvailable program)
	find_program(PROGRAM_FOUND_${program} "${program}")
	
	if("PROGRAM_FOUND_${program}-NOTFOUND" STREQUAL "${PROGRAM_FOUND_${program}}")
		MESSAGE(FATAL_ERROR "ERROR: ${program} not found.")
	endif()
endfunction()

# Function for getting chaotica config.
function(getConfigOption config_name result)
	# Execute config.rb.
	execute_process(COMMAND "ruby" "${CMAKE_SOURCE_DIR}/scripts/config.rb" "${config_name}"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts"
		OUTPUT_VARIABLE TEMP)
	
	# Remove newlines.
	string(REPLACE "\n" "" TEMP ${TEMP})
	
	# Set result.
	set (${result} ${TEMP} PARENT_SCOPE)
endfunction()


function(convertToForwardSlashes str result)
	string(REPLACE "\\" "/" var str)
	set (${result} ${var} PARENT_SCOPE)
endfunction()

# Function for checking an environment variable exists
function(checkAndGetEnvVar envvar result)
	if(DEFINED ENV{${envvar}})
		# To prevent issues with \ characters in macros, replace possible \ INDIGO_LIBS env vars and store it in INDIGO_LIBS_ENV.
		string(REPLACE "\\" "/" var $ENV{${envvar}})
		set (${result} ${var} PARENT_SCOPE)
	else()
		MESSAGE(FATAL_ERROR "ERROR: Environment variable ${envvar} not defined.")
	endif()
endfunction()