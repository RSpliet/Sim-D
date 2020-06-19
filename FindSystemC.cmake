# - Try to find SystemC
# Once done this will define
#  SYSTEMC_FOUND		- System has SystemC
#  SYSTEMC_INCLUDE_DIRS - The SystemC include directories
#  SYSTEMC_LIBRARIES	- The libraries needed to use SystemC
#  SYSTEMC_VERSION		- SystemC version string

include(FindPackageHandleStandardArgs)

set(_SYSTEMC_HINTS
	$ENV{SYSTEMC_PREFIX}/include
	$ENV{SYSTEMC_PREFIX}/lib
	$ENV{SYSTEMC_PREFIX}/lib64
	$ENV{SYSTEMC_PREFIX}/lib-linux64
)

set(_SYSTEMC_PATHS
	/usr/include
	/usr/include/systemc
	/usr/local/systemc/include
	/usr/local/systemc/lib-linux64
	/usr/local/systemc/lib
	/usr/local/systemc/lib64
	/usr/local/systemc-2.3.1/include
	/usr/local/systemc-2.3.1/lib-linux64
	/usr/local/systemc-2.3.3/include
	/usr/local/systemc-2.3.3/lib-linux64
	/usr/local/systemc-2.3.3/lib64
	/usr/local/systemc-2.3.3/lib
	/opt/systemc
	/opt/systemc-2.3.1
	/opt/systemc-2.3.3
	/usr/lib
	/usr/lib64
	/usr/lib-linux64
	/usr/local/lib
	/usr/local/lib64
	/usr/local/lib-linux64
)

find_path(SYSTEMC_INCLUDE_DIRS NAMES systemc.h HINTS ${_SYSTEMC_HINTS}
	PATHS ${_SYSTEMC_PATHS}
)

find_path(SYSTEMC_LIBRARY_DIR NAMES libsystemc.a HINTS ${_SYSTEMC_HINTS}
	PATHS ${_SYSTEMC_PATHS}
)

set(SYSTEMC_LIBRARIES ${SYSTEMC_LIBRARY_DIR}/libsystemc.a)
set(_SYSTEMC_VER_H ${SYSTEMC_INCLUDE_DIRS}/sysc/kernel/sc_ver.h)

exec_program("grep '#define SC_VERSION_MAJOR' ${_SYSTEMC_VER_H} | \
				tr -s ' ' | cut -d ' ' -f 3 " OUTPUT_VARIABLE _SYSTEMC_MAJOR)
exec_program("grep '#define SC_VERSION_MINOR' ${_SYSTEMC_VER_H} | \
				tr -s ' ' | cut -d ' ' -f 3 " OUTPUT_VARIABLE _SYSTEMC_MINOR)
exec_program("grep '#define SC_VERSION_PATCH' ${_SYSTEMC_VER_H} | \
				tr -s ' ' | cut -d ' ' -f 3 " OUTPUT_VARIABLE _SYSTEMC_PATCH)

set(SYSTEMC_VERSION "${_SYSTEMC_MAJOR}.${_SYSTEMC_MINOR}.${_SYSTEMC_PATCH}")

find_package_handle_standard_args(SystemC FOUND_VAR SYSTEMC_FOUND
				REQUIRED_VARS SYSTEMC_LIBRARIES SYSTEMC_INCLUDE_DIRS 
				VERSION_VAR SYSTEMC_VERSION)


