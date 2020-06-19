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

#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <cinttypes>

int64_t csv_file_count(const char *file)
{
	int64_t c = 0;
	float tmp;
	FILE *fp;

	fp = fopen(file, "r");
	if (!fp) {
		fprintf(stderr, "Could not open csv file %s\n", file);
		return -EINVAL;
	}

	while (fscanf(fp, "%f%*[, ]", &tmp) > 0) {
		c++;
	}

	fclose(fp);

	return c;
}

int64_t csv_file_read(char *file, int **buf)
{
	int64_t count, i;
	int tmp;
	FILE *fp;

	count = csv_file_count(file);
	if (count < 0)
		return -1;

	*buf = new int[count];
	if (!*buf) {
		fprintf(stderr, "Could not allocate memory for data buffer\n");
		return -1;
	}

	fp = fopen(file, "r");
	if (!fp) {
		delete[] *buf;
		*buf = nullptr;

		fprintf(stderr, "Could not open csv file %s\n", file);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		tmp = fscanf(fp, "%d%*[, ]", *buf+i);
		if (tmp <= 0)
			break;
	}

	fclose(fp);

	return i;
}

int64_t csv_file_read_float(const char *file, float **buf)
{
	int64_t count, i;
	int tmp;
	FILE *fp;

	count = csv_file_count(file);
	if (count < 0)
		return -1;

	*buf = new float[count];
	if (!*buf) {
		fprintf(stderr, "Could not allocate memory for data buffer\n");
		return -1;
	}

	fp = fopen(file, "r");
	if (!fp) {
		delete[] *buf;
		*buf = nullptr;

		fprintf(stderr, "Could not open csv file %s\n", file);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		tmp = fscanf(fp, "%f%*[, ]", *buf+i);
		if (tmp <= 0)
			break;
	}

	fclose(fp);

	return i;
}

/*
 * Read n-tuples from file, store in "struct of arrays" format.
 */
int64_t csv_file_read_float_n(char *file, int n, float ***buf)
{
	int64_t count, i;
	int tmp;
	FILE *fp;

	count = csv_file_count(file);
	if (count < 0)
		return -1;

	if (count % n != 0) {
		fprintf(stderr, "Incomplete n-tuple found %" PRIu64 "\n", count);
		return -1;
	}

	*buf = new float*[n];
	if (!*buf) {
		fprintf(stderr, "Could not allocate memory for data buffer\n");
		return -1;
	}

	/* Make one large contiguous buffer for easier param passing */
	(*buf)[0] = new float[count];
	if (!(*buf)[0]) {
		delete[] *buf;
		*buf = nullptr;

		fprintf(stderr, "Could not allocate memory for data "
				"buffer\n");
		return -1;
	}

	for (i = 1; i < n; i++) {
		(*buf)[i] = (*buf)[0] + i * (count / n);
	}

	fp = fopen(file, "r");
	if (!fp) {
		delete[] *buf[0];
		delete[] *buf;
		*buf = nullptr;

		fprintf(stderr, "Could not open csv file %s\n", file);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		tmp = fscanf(fp, "%f%*[, ]", (*buf)[i % n]+(i / n));
		if (tmp <= 0)
			break;
	}

	fclose(fp);

	return i/n;
}
