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

add_executable(mc
	$<TARGET_OBJECTS:simd_base>
	$<TARGET_OBJECTS:simd_reg>
	$<TARGET_OBJECTS:simd_mc_intf>
	$<TARGET_OBJECTS:simd_mc_stats>
	mc.cpp
)
target_link_libraries(mc ${libs} ramulator)

add_executable(mcIdx
	$<TARGET_OBJECTS:simd_base>
	$<TARGET_OBJECTS:simd_reg>
	$<TARGET_OBJECTS:simd_mc_intf>
	$<TARGET_OBJECTS:simd_mc_stats>
	mcIdx.cpp
)
target_link_libraries(mcIdx ${libs} ramulator)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_executable(StrideSequencer
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_mc_intf>
		$<TARGET_OBJECTS:simd_reg>
		test/Test_StrideSequencer.cpp
	)
	target_link_libraries(StrideSequencer ${libs})
	
	add_executable(IdxIterator
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_mc_intf>
		$<TARGET_OBJECTS:simd_reg>
		test/Test_IdxIterator.cpp
	)
	target_link_libraries(IdxIterator ${libs})
	
	add_executable(CmdGen_DDR4
		$<TARGET_OBJECTS:simd_base>
		test/Test_CmdGen_DDR4.cpp
	)
	target_link_libraries(CmdGen_DDR4 ${libs})
	
	add_executable(CmdArb_DDR4
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_mc_stats>
		test/Test_CmdArb_DDR4.cpp
	)
	target_link_libraries(CmdArb_DDR4 ${libs} ramulator)
	
	add_executable(mc_DQ
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		test/Test_DQ.cpp
	)
	target_link_libraries(mc_DQ ${libs} ramulator)
	
	set_target_properties(StrideSequencer CmdGen_DDR4 CmdArb_DDR4
					  mc_DQ IdxIterator
	    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${test_path}
	)
	
	add_test(mc_StrideSequencer ${test_path}/StrideSequencer)
	add_test(mc_IdxIterator ${test_path}/IdxIterator)
	add_test(mc_CmdGen_DDR4 ${test_path}/CmdGen_DDR4)
	add_test(mc_CmdArb_DDR4 ${test_path}/CmdArb_DDR4)
	add_test(mc_DQ ${test_path}/mc_DQ)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")
