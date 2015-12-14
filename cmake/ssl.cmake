# Use either LibreSSL or OpenSSL

if(INDIGO_USE_LIBRESSL)
	include(../cmake/libressl.cmake)
else()
	include(../cmake/openssl.cmake)
endif()