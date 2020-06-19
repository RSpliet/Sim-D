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

#ifndef MODEL_BUFFER_H
#define MODEL_BUFFER_H

#include <systemc>
#include <string>

using namespace sc_core;
using namespace sc_dt;
using namespace std;

namespace simd_model {

typedef enum {
	INPUT_NONE,
	DECIMAL_CSV,
	BINARY,
} buffer_input_type;

/** A buffer object.
 *
 * Sim-D works with mapped buffers. These Buffer objects capture their
 * parameters such that they can be passed around through SystemC ports
 */
class Buffer {
public:
	/** This buffer is valid. False for an unmapped buffer slot. */
	bool valid;

	/** Physical address of this buffer in DRAM. */
	sc_uint<32> addr;

	/** X,Y dimensions of buffer. */
	sc_uint<32> dims[2];

	/** Default constructor. */
	Buffer() : valid(false) {}

	/** Constructor.
	 * @param a Start address of buffer.
	 * @param dim_x X-dimension of buffer.
	 * @param dim_y Y-dimension of buffer.*/
	Buffer(sc_uint<32> a, sc_uint<32> dim_x = 0, sc_uint<32> dim_y = 0);

	/** Return the base address of this buffer. */
	sc_uint<32> getAddress(void) const;

	/** Return the x-dimension of this buffer. */
	sc_uint<32> get_dim_x(void) const;

	/** Return the y-dimension of this buffer. */
	sc_uint<32> get_dim_y(void) const;

	/** Print stream operation */
	inline friend ostream &
	operator<<(ostream &os, const Buffer &p)
	{
		os << "Buffer(" << p.addr << ", "
				<< p.dims[0] << "*" << p.dims[1] << ", stride "
				<< ")";

		return os;
	}

	/** SystemC mandatory trace op
	 * @param tf Reference to trace file.
	 * @param v Control stack entry to print.
	 * @param NAME Name of the SystemC object.
	 */
	inline friend void
	sc_trace(sc_trace_file *tf, const Buffer &v,
	    const std::string &NAME )
	{
	 	sc_trace(tf,v.valid, NAME + ".valid");
		sc_trace(tf,v.addr, NAME + ".addr");
	}

	/** Comparator.
	 * @param p Object to compare &this against.
	 * @return True iff the two objects target the same start address. */
	inline bool
	operator==(const Buffer &p) const
	{
		if (valid != p.valid)
			return false;

		return (!valid || addr == p.addr);
	}

	/** Incomparator.
	 * @param p Object to compare &this against.
	 * @return False iff the two objects target the same start address. */
	inline bool
	operator!=(const Buffer &p) const
	{
		return !(*this == p);
	}

	/** Assignment constructor.
	 * @param p Buffer object to take deep copy values from.
	 * @return Reference to new buffer &this. */
	inline Buffer &
	operator=(const Buffer &p)
	{
		valid = p.valid;
		addr = p.addr;
		dims[0] = p.dims[0];
		dims[1] = p.dims[1];

		return *this;
	}

	/** Assignment constructor from just an address.
	 *
	 * Dimensions will be set to 0, used for quick comparisons.
	 * @param p Address value.
	 * @return Reference to new buffer &this. */
	inline Buffer &
	operator=(const sc_uint<32> &p)
	{
		valid = true;
		addr = p;
		dims[0] = 0;
		dims[1] = 0;

		return *this;
	}
};

/** Buffer object from the program's point of view.
 *
 * The program parser stores additional information on a Buffer, e.g. a file
 * containing an input data set. Extend a buffer such that we can make this
 * happen without exposing irrelevant information to the compute pipeline.
 */
class ProgramBuffer : public Buffer {
private:
	/** Input file for data set that must be uploaded prior to simulation.
	 * Empty string if no such file exists (e.g. output buffer). */
	string data_input_file;

	/** Type of data input file. */
	buffer_input_type data_input_type;

public:
	/** Default constructor */
	ProgramBuffer();

	/** Constructor.
	 * @param a Start address.
	 * @param dim_x X-dimension.
	 * @param dim_y Y-dimension.
	 * @param dt Type of buffer input file (CSV of floats, binary).
	 * @param dif Input file path and name. */
	ProgramBuffer(sc_uint<32> a, unsigned int dim_x, unsigned int dim_y,
			buffer_input_type dt = INPUT_NONE, string dif = "");

	/** Return true iff this buffer has an associated input file with
	 * initialisation values.
	 * @return True iff this buffer has an associated input file. */
	bool hasDataInputFile(void) const;

	/** Return the type of the input data file (CSV of floats or binary).
	 * @return The type of the input data file. */
	buffer_input_type getDataInputType(void) const;

	/** Return the file name of the input data file.
	 * @return The file name of the input data file. */
	string getDataInputFile(void) const;

	/** Set the associated input file.
	 * @param dif Path and file name of input file.
	 * @param dt Type of the input data file (CSV of floats, binary).
	 * @return Content type (CSV of floats, binary). */
	void setDataInputFile(string dif, buffer_input_type dt);

	/** Return the total size of this buffer in bytes. */
	size_t size(void) const;

	/** Print stream operation */
	inline friend ostream &
	operator<<(ostream &os, const ProgramBuffer &p)
	{
		if (p.valid) {
			os << "Buffer(0x" << hex << p.addr << dec << ", "
				<< p.dims[0] << "*" << p.dims[1] << ", stride "
				<< ")";
			if (p.data_input_file != "")
				os << " <- " << p.data_input_file;
		} else {
			os << "Buffer(invalid)";
		}

		return os;
	}
};

}

#endif /* MODEL_BUFFER_H */
