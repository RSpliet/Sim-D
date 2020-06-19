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

#ifndef UTIL_RINGBUFFER_H
#define UTIL_RINGBUFFER_H

#include <exception>

using namespace std;

namespace simd_util {

/** A ringbuffer of fixed (but configurable) size.
 *
 * This ringbuffer's primary purpose is to help implement arbitrary length
 * pipelines. Storage required for this structure isn't necessarily
 * representative for registers required in a real pipeline, as each stage
 * contains the same data structure. However, we don't copy around the data
 * on each cycle, which should help performance a little bit. Because of this,
 * most methods (except swap) return references to items such that they can be
 * easily altered by the requester's control logic.
 */
template <class T>
class Ringbuffer
{
private:
	/** Number of entries (pipeline stages) */
	unsigned int entries;

	/** Array of ringbuffer elements. */
	T *buf;

	/** Pointer to write slot. */
	unsigned int head;

public:
	/** Constructor.
	 * @param e Number of entries. */
	Ringbuffer(unsigned int e = 1) : buf(nullptr)
	{
		resize(e);
	}

	/** Destructor. */
	~Ringbuffer()
	{
		if (buf) {
			delete[] buf;
		}
	}

	/** Return the number of entries (pipeline stages).
	 * @return The number of entries in this ringbuffer. */
	unsigned int
	getEntries(void)
	{
		return entries;
	}

	/** Resize the ringbuffer.
	 *
	 * Will reset the head pointer and all entries.
	 * @param e Number of entries. */
	void
	resize(unsigned int e)
	{
		if (e == 0)
			throw invalid_argument("Ringbuffer must contain at "
					"least one entry");

		if (buf) {
			delete[] buf;
			buf = nullptr;
		}

		buf = new T[e];
		entries = e;
		head = 0;
	}

	/** Remove the final stage entry from the pipeline, write back a new
	 * entry at stage 0.
	 *
	 * @param elem Element to swap in.
	 * @return Element at head of queue. */
	T
	swapHead(T &elem)
	{
		T ret_el;

		/** First write, then read. That may seem off, but for a
		 * pipeline of a single stage, we *want* to read back what we
		 * just wrote. */
		buf[head] = elem;
		head = (head + entries - 1) % entries;
		ret_el = buf[head];

		return ret_el;
	}

	/** Return a pointer to the requested pipeline stage data structure.
	 *
	 * @param stage Desired pipeline stage (0 .. #entries).
	 * @return The requested pipeline stage entry. */
	T &
	getStage(unsigned int stage)
	{
		if (stage >= entries)
			throw invalid_argument("Stage must be between 0 and "
				+ to_string(entries-1) + ", " + to_string(stage) + "provided.");

		stage = (head + stage) % entries;

		return buf[stage];
	}

	/** Obtain the entry for the last stage of the pipeline.
	 * @return The entry for the last stage of the pipeline. */
	T &
	top(void)
	{
		return getStage(entries - 1);
	}
};

}

#endif /* UTIL_RINGBUFFER_H */
