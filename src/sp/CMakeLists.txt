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

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_executable(StorageArray
		$<TARGET_OBJECTS:simd_base>
		test/Test_StorageArray.cpp
	)
	target_link_libraries(StorageArray ${libs} ramulator)
	
	add_executable(SP_StrideSequencer
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_StrideSequencer.cpp
	)
	target_link_libraries(SP_StrideSequencer ${libs} ramulator)
	
	add_executable(SP_DQ
		$<TARGET_OBJECTS:simd_base>
		test/Test_DQ.cpp
	)
	target_link_libraries(SP_DQ ${libs} ramulator)
	
	add_executable(SP_Scratchpad
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_Scratchpad.cpp
	)
	target_link_libraries(SP_Scratchpad ${libs} ramulator)
	
	set_target_properties(StorageArray SP_StrideSequencer SP_DQ SP_Scratchpad
	    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${test_path}
	)
	
	add_test(sp_StorageArray ${test_path}/StorageArray)
	add_test(sp_StrideSequencer ${test_path}/SP_StrideSequencer)
	add_test(sp_DQ ${test_path}/SP_DQ)
	add_test(sp_Scratchpad ${test_path}/SP_Scratchpad)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")
