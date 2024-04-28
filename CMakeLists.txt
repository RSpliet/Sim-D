# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2020 Roy Spliet, University of Cambridge
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.
#
# Minimum is 3.1 for CMAKE_CXX_STANDARD integration
# Minimum is 3.7 for VERSION_GREATER_EQUAL test.
cmake_minimum_required(VERSION 3.7)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(Sim-D)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
find_package(SystemCLanguage CONFIG REQUIRED 2.3)
find_package(ramulator REQUIRED)
find_package(drampower REQUIRED)
find_package(XercesC REQUIRED)
add_definitions(-DRAMULATOR)

set(CMAKE_CXX_STANDARD 17)

find_package(Doxygen)

if(DOXYGEN_FOUND)
	set(DOXYGEN_INPUT Doxyfile)

	add_custom_target(docs 
		COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_INPUT} 
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif(DOXYGEN_FOUND)

# C99-style array designators are used quite extensively in Sim-D, using enum
# values as array indexes. This style of coding allows for easier refactoring,
# reducing the potential for errors. It's functional in GCC and CLANG despite
# violating the C++ standard, but CLANG/LLVM 10 started complaining about the
# practice. Silence the warning for now, and leave refactoring the code to drop
# array designator initialisation when it actually causes problems.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
	if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "10.0.0")
		add_compile_options("-Wno-c99-designator")
	endif(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "10.0.0")
endif(CMAKE_C_COMPILER_ID STREQUAL "Clang")

include_directories($<TARGET_PROPERTY:SystemC::systemc,INTERFACE_INCLUDE_DIRECTORIES>)
include_directories("./include")
include_directories("./src")

set(libs
	SystemC::systemc
	${XercesC_LIBRARIES}
	${CMAKE_THREAD_LIBS_INIT}
	drampowerxml
	drampower
	ramulator)
link_directories(${ramulator_DIR})
link_directories(${drampower_DIR})

enable_testing()
set(test_path ${PROJECT_SOURCE_DIR}/Testing/bin)

set(COMPUTE_THREADS 1024 CACHE STRING "Number of threads in a workgroup.")
add_compile_options(-DCOMPUTE_THREADS=${COMPUTE_THREADS})
set(COMPUTE_FPUS 128 CACHE STRING "Number of FPUs in a SimdCluster.")
add_compile_options(-DCOMPUTE_FPUS=${COMPUTE_FPUS})
set(COMPUTE_RCPUS 32 CACHE STRING "Number of RCP/RSQRT units in a SimdCluster.")
add_compile_options(-DCOMPUTE_RCPUS=${COMPUTE_RCPUS})
set(COMPUTE_IMEM_INSNS 2048 CACHE STRING "Size of instruction memory in #instructions.")
add_compile_options(-DCOMPUTE_IMEM_INSNS=${COMPUTE_IMEM_INSNS})

set(MC_DRAM_CHANS 1 CACHE STRING "Number of DRAM channels connected to memctrl")
add_compile_options(-DMC_DRAM_CHANS=${MC_DRAM_CHANS})
set(MC_BIND_BUFS 32 CACHE STRING "Number of bind buffer entries in memctrl")
add_compile_options(-DMC_BIND_BUFS=${MC_BIND_BUFS})

set(MC_DRAM_ORG "DDR4_8Gb_x16" CACHE STRING "DDR4 organisation string for Ramulator (see Ramulator/src/DDR4.cpp for valid options)")
add_compile_options(-DMC_DRAM_ORG="${MC_DRAM_ORG}")
set(MC_DRAM_SPEED "DDR4_3200AA" CACHE STRING "DDR4 speed configuration string for Ramulator (see Ramulator/src/DDR4.cpp for valid options)")
add_compile_options(-DMC_DRAM_SPEED="${MC_DRAM_SPEED}")

set(MC_DRAM_BANKS 8 CACHE STRING "DDR4 banks. Must correspond with Ramulator orga.")
add_compile_options(-DMC_DRAM_BANKS=${MC_DRAM_BANKS})
set(MC_DRAM_ROWS 65536 CACHE STRING "DDR4 rows. Must correspond with Ramulator orga.")
add_compile_options(-DMC_DRAM_ROWS=${MC_DRAM_ROWS})
set(MC_DRAM_COLS 1024 CACHE STRING "DDR4 columns. Must correspond with Ramulator orga.")
add_compile_options(-DMC_DRAM_COLS=${MC_DRAM_COLS})

set(SP_BUS_WIDTH 4 CACHE STRING "Number of 32-bit words transferred from scratchpad to register file per cycle.")
add_compile_options(-DSP_BUS_WIDTH=${SP_BUS_WIDTH})

add_library(simd_base OBJECT
	${PROJECT_SOURCE_DIR}/src/util/debug_output.cpp
	${PROJECT_SOURCE_DIR}/src/util/SimdTest.cpp
	${PROJECT_SOURCE_DIR}/src/util/sched_opts.cpp
)

add_library(simd_ddr4_lid OBJECT
	${PROJECT_SOURCE_DIR}/src/util/ddr4_lid.cpp
)

add_library(simd_mc_stats OBJECT
	${PROJECT_SOURCE_DIR}/src/mc/model/cmdarb_stats.cpp
)

add_library(simd_reg OBJECT
	${PROJECT_SOURCE_DIR}/src/util/parse.cpp
	${PROJECT_SOURCE_DIR}/src/model/Register.cpp
)

add_library(simd_mc_intf OBJECT
	${PROJECT_SOURCE_DIR}/src/util/csv.cpp
	${PROJECT_SOURCE_DIR}/src/model/Buffer.cpp
	${PROJECT_SOURCE_DIR}/src/model/stride_descriptor.cpp
)

add_library(simd_isa OBJECT
	${PROJECT_SOURCE_DIR}/src/isa/model/Operand.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/Instruction.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/BB.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/CFGEdge.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/Metadata.cpp
)

add_library(simd_isa_analysis OBJECT
	${PROJECT_SOURCE_DIR}/src/isa/analysis/CycleSim.cpp
	${PROJECT_SOURCE_DIR}/src/isa/analysis/DRAMSim.cpp
	${PROJECT_SOURCE_DIR}/src/isa/analysis/TimingDAG.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/DAG.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/ProgramPhaseList.cpp
)

add_library(simd_compile OBJECT
	${PROJECT_SOURCE_DIR}/src/isa/model/Loop.cpp
	${PROJECT_SOURCE_DIR}/src/isa/analysis/ControlFlow.cpp
	${PROJECT_SOURCE_DIR}/src/isa/model/Program.cpp
)

add_subdirectory(src/mc)
add_subdirectory(src/sp)
add_subdirectory(src/compute)
add_subdirectory(src/stridegen)
add_subdirectory(src/isa)
add_subdirectory(src/util)

add_executable(main
	$<TARGET_OBJECTS:simd_base>
	$<TARGET_OBJECTS:simd_reg>
	$<TARGET_OBJECTS:simd_isa>
	$<TARGET_OBJECTS:simd_compile>
	$<TARGET_OBJECTS:simd_ddr4_lid>
	$<TARGET_OBJECTS:simd_mc_intf>
	$<TARGET_OBJECTS:simd_mc_stats>
	src/main.cpp)
target_link_libraries(main ${libs})

add_executable(wcet
	$<TARGET_OBJECTS:simd_base>
	$<TARGET_OBJECTS:simd_reg>
	$<TARGET_OBJECTS:simd_isa>
	$<TARGET_OBJECTS:simd_isa_analysis>
	$<TARGET_OBJECTS:simd_compile>
	$<TARGET_OBJECTS:simd_ddr4_lid>
	$<TARGET_OBJECTS:simd_mc_intf>
	$<TARGET_OBJECTS:simd_mc_stats>
	src/wcet.cpp)
target_link_libraries(wcet ${libs})
