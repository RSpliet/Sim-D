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

#ifndef UTIL_CSV_H
#define UTIL_CSV_H

#include <stdint.h>

int64_t csv_file_count(const char *file);
int64_t csv_file_read(char *file, int **buf);
int64_t csv_file_read_float(const char *file, float **buf);
int64_t csv_file_read_float_n(char *file, int n, float ***buf);

#endif /* UTIL_CSV_H */
