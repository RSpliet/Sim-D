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

#ifndef MC_MODEL_STORAGE_LL_ELEM_H
#define MC_MODEL_STORAGE_LL_ELEM_H

#include <systemc>

using namespace sc_dt;
using namespace sc_core;
using namespace std;

namespace mc_model {

/** Linked list class for storage elements.
 * Prev and next pointers are always valid. A disconnected list or list with a
 * single element will have prev and next pointers pointing at itself.
 */
class storage_ll_elem
{
private:
	/** LL previous pointer. */
	storage_ll_elem *prev;
	/** LL next pointer. */
	storage_ll_elem *next;
	/** Row this element stores. */
	sc_uint<20> row;
	/** Data array storing the data for this row */
	uint32_t *data;

	/** Set the next pointer for this LL element
	 * @param elem Element to make the next-link point to */
	void set_next(storage_ll_elem *elem)
	{
		next = elem;
	}
	/** Set the previous pointer for this LL element
	 * @param elem Element to make the prev-link point to */
	void set_prev(storage_ll_elem *elem)
	{
		prev = elem;
	}
public:
	/** Constructor.
	 * @param r Row index this element stores.
	 * @param d Data array as backing store for this row. */
	storage_ll_elem(sc_uint<20> r, uint32_t *d)
	{
		row = r;
		data = d;
		prev = this;
		next = this;
	}

	~storage_ll_elem()
	{
		if (data)
			delete [] data;
	}

	/** Return the next element in the list.
	 * @return Next element. */
	storage_ll_elem *get_next()
	{
		return next;
	}

	/** Return the previous element in the list.
	 * @return Previous element. */
	storage_ll_elem *get_prev()
	{
		return prev;
	}

	/** Get the row this element stores.
	 * @return The row this element stores. */
	sc_uint<20> get_row()
	{
		return row;
	}

	/** Return the data word for this element at given offset.
	 * @param offset Offset within the data array in 32-bit words.
	 * @return The data stored at this offset. */
	uint32_t get_data(uint32_t offset)
	{
		return data[offset];
	}

	/** Set the data word at given offset.
	 * @param offset Offset within the data array in 32-bit words.
	 * @param val Value to store at given offset. */
	void set_data(uint32_t offset, uint32_t val)
	{
		data[offset] = val;
	}

	/** Insert the given element after this one.
	 * @param elem Element to insert in the linked list. */
	void insert_after(storage_ll_elem *elem)
	{
		elem->set_next(next);
		elem->set_prev(this);
		next->set_prev(elem);
		set_next(elem);
	}

	/** Insert the given element before this one in the LL
	 * @param elem Element to insert */
	void insert_before(storage_ll_elem *elem)
	{
		prev->insert_after(elem);
	}

	/** Remove this element from the list */
	void unlink()
	{
		next->set_prev(prev);
		prev->set_next(next);
		set_next(this);
		set_prev(this);
	}
};

}

#endif /* MC_MODEL_STORAGE_LL_ELEM_H */
