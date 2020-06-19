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

#ifndef MC_CONTROL_STORAGE_H
#define MC_CONTROL_STORAGE_H

#include <cstdint>

#include "mc/model/storage_ll_elem.h"
#include "util/constmath.h"

using namespace std;
using namespace mc_model;

namespace mc_control {

/**
 * Storage back-end for our simulation environment.
 * Ramulator does not seem to provide any storage solution alongside its
 * timing model, so we have to implement our own. We're dealing with a multi-GB
 * address space, which is more than often required during simulation.
 * As a trade-off between speed and storage, we implement a hash-table and
 * allocate the memory on demand at page granularity.
 */
template <unsigned int BUS_WIDTH, unsigned int DRAM_BANKS,
	unsigned int DRAM_COLS, unsigned int DRAM_ROWS>
/* <16,16,1024,32768> */
class Storage
{
private:
	/** Hashmap for the storage linked lists. */
	storage_ll_elem *hashmap[DRAM_BANKS * 128];

	/** Allocate memory for a row of words.
	 * @return A 32-bit integer array holding data for a full row. */
	uint32_t *alloc_row()
	{
		unsigned int size;

		/* BUS_WIDTH is expressed in n 32-bit words per burst,
		 * corresponds with n/2 bytes per cycle, or n/8 words. */
		size = DRAM_COLS * (BUS_WIDTH / 8);

		return new uint32_t[size];
	}

	/** Add a new row to the desired hashmap ll.
	 * @param hashmap_entry The index in the hashmap to store this elem
	 * @param elem Linked list element to insert */
	void insert(unsigned int hashmap_entry, storage_ll_elem *elem)
	{
		if (hashmap[hashmap_entry] == nullptr)
			hashmap[hashmap_entry] = elem;
		else
			hashmap[hashmap_entry]->insert_before(elem);
	}

	/** For given (bank, row), find the right ll element, allocate and
	 * insert if it doesn't exist already.
	 * @param bank Bank
	 * @param row Row
	 * @return The linked list element referred to by the address */
	storage_ll_elem *ensure_row(sc_uint<const_log2(DRAM_BANKS)> bank,
			sc_uint<20> row)
	{
		unsigned int hashmap_entry;
		storage_ll_elem *head, *ptr;
		uint32_t *data;

		hashmap_entry = ((row & 0x7f) << const_log2(DRAM_BANKS)) | bank;

		head = hashmap[hashmap_entry];
		if (head != nullptr) {
			ptr = head;
			do {
				if (ptr->get_row() == row)
					return ptr;

				ptr = ptr->get_next();
			} while (ptr != head);
		}

		/* Not found */
		data = alloc_row();

		/* Each bank gets its own set of rows, so no need to encode
		 * the bank in the storage_ll_elem */
		ptr = new storage_ll_elem(row, data);
		insert(hashmap_entry, ptr);

		return ptr;
	}

public:
	/** Constructor */
	Storage()
	{
		unsigned int i;

		for (i = 0; i < DRAM_BANKS * 128; i++)
			hashmap[i] = nullptr;
	}

	~Storage()
	{
		unsigned int i;
		storage_ll_elem *elem, *next;

		for (i = 0; i < DRAM_BANKS * 128; i++) {
			if (hashmap[i]) {
				next = hashmap[i];
				do {
					elem = next;
					next = elem->get_next();
					elem->unlink();
					delete elem;
				} while (elem != next);
			}

		}
	}

	/** Return the word stored at a given address.
	 * @param bank Bank.
	 * @param row Row.
	 * @param col Column.
	 * @param dq_word 32-bit subset index on the data line.
	 * @return Value stored at given address. */
	uint32_t get_word(sc_uint<const_log2(DRAM_BANKS)> bank,
			sc_uint<20> row, sc_uint<20> col, sc_uint<10> dq_word)
	{
		uint32_t offset;
		storage_ll_elem *elem;

		elem = ensure_row(bank, row);
		/* Translate col to offset... */
		offset = (col * (BUS_WIDTH / 8)) | dq_word;

		return elem->get_data(offset);
	}

	/** Store a word at a given address.
	 * @param bank Bank.
	 * @param row Row.
	 * @param col Column.
	 * @param dq_word 32-bit subset index on the data lines.
	 * @param val Value to write to memory. */
	void set_word(sc_uint<const_log2(DRAM_BANKS)> bank,
			sc_uint<20> row, sc_uint<20> col, sc_uint<10> dq_word,
			uint32_t val)
	{
		uint32_t offset;
		storage_ll_elem *elem;

		elem = ensure_row(bank, row);
		/* Translate col to offset... */
		offset = (col * (BUS_WIDTH / 8)) | dq_word;

		elem->set_data(offset, val);
	}
};

}

#endif /* MC_CONTROL_STORAGE_H */
