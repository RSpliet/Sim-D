/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2020 Roy Spliet, University of Cambridge
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SRC_INCLUDE_UTIL_SIMDTEST_H_
#define SRC_INCLUDE_UTIL_SIMDTEST_H_

#include <systemc>

using namespace sc_core;
using namespace sc_dt;

namespace simd_test {

/**
 * Wrapper class for an sc_module implementing a unit test.
 *
 * This class exists to have a reliable and uniform mechanism to indicate that
 * a unit-test has finished to completion. This essentially solves a problem
 * where a unit test appears to have run without errors, while in practice it
 * is blocked indefinitely on a FIFO read or write operation.
 */
class SimdTest : public sc_module {
private:
	/** True iff the test has finished to completion. */
	bool test_finished;

protected:
	/** Default constructor. */
	SimdTest();

	/** Mark this test as finished. */
	void test_finish(void);

public:
	/** Return true iff the test has finished.
	 * @return true iff the test has finished. */
	bool has_finished(void) const;
};

}

#endif /* SRC_INCLUDE_UTIL_SIMDTEST_H_ */
