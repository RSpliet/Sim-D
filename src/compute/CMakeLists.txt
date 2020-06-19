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

add_executable(compute
	$<TARGET_OBJECTS:simd_base>
	$<TARGET_OBJECTS:simd_reg>
	$<TARGET_OBJECTS:simd_isa>
	$<TARGET_OBJECTS:simd_ddr4_lid>
	$<TARGET_OBJECTS:simd_mc_intf>
	compute.cpp
)
target_link_libraries(compute ${libs})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_executable(CtrlStack
		$<TARGET_OBJECTS:simd_base>
		test/Test_CtrlStack.cpp
	)
	target_link_libraries(CtrlStack ${libs})
	
	add_executable(RegFile_1S_3R1W
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_RegFile_1S_3R1W.cpp
	)
	target_link_libraries(RegFile_1S_3R1W ${libs})
	
	add_executable(Scoreboard
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_isa>
		test/Test_Scoreboard.cpp
	)
	target_link_libraries(Scoreboard ${libs})
	
	add_executable(IMem
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_isa>
		test/Test_IMem.cpp
	)
	target_link_libraries(IMem ${libs})
	
	add_executable(IFetch
		$<TARGET_OBJECTS:simd_base>
		test/Test_IFetch.cpp
	)
	target_link_libraries(IFetch ${libs})
	
	add_executable(IDecode_1S
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_isa>
		test/Test_IDecode_1S.cpp
	)
	target_link_libraries(IDecode_1S ${libs})
	
	add_executable(IExecute
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_isa>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_IExecute.cpp
	)
	target_link_libraries(IExecute ${libs})
	
	add_executable(WorkScheduler
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_isa>
		$<TARGET_OBJECTS:simd_ddr4_lid>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_WorkScheduler.cpp
	)
	target_link_libraries(WorkScheduler ${libs})
	
	add_executable(SimdCluster
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_isa>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_SimdCluster.cpp
	)
	target_link_libraries(SimdCluster ${libs})
	
	add_executable(BufferToPhysXlat
		$<TARGET_OBJECTS:simd_base>
		$<TARGET_OBJECTS:simd_reg>
		$<TARGET_OBJECTS:simd_mc_intf>
		test/Test_BufferToPhysXlat.cpp
	)
	target_link_libraries(BufferToPhysXlat ${libs})
	
	set_target_properties(CtrlStack RegFile_1S_3R1W Scoreboard IMem
			IFetch IDecode_1S IExecute WorkScheduler SimdCluster
			BufferToPhysXlat
	    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${test_path}
	)
	
	add_test(compute_CtrlStack ${test_path}/CtrlStack)
	add_test(compute_RegFile_1S_3R1W ${test_path}/RegFile_1S_3R1W)
	add_test(compute_Scoreboard ${test_path}/Scoreboard)
	add_test(compute_IMem ${test_path}/IMem)
	add_test(compute_IFetch ${test_path}/IFetch)
	add_test(compute_IDecode_1S ${test_path}/IDecode_1S)
	add_test(compute_IExecute ${test_path}/IExecute)
	add_test(compute_WorkScheduler ${test_path}/WorkScheduler)
	add_test(compute_SimdCluster ${test_path}/SimdCluster)
	add_test(compute_BufferToPhysXlat ${test_path}/BufferToPhysXlat)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")
