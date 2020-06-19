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

#include <fstream>
#include <iostream>
#include <string>
#include <cmath>
#include <cstdio>

using namespace std;

const static string param_str[7] = {
	"Error",
	"referenceNormal.x",
	"referenceNormal.y",
	"referenceNormal.z",
	"out.x",
	"out.y",
	"out.z",
};

int
read_int(ifstream &fs)
{
	int result;

	fs.read((char *)&result, 4);

	return result;
}

float
read_float(ifstream &fs)
{
	float result;

	fs.read((char *)&result, 4);

	return result;
}

int
main(int argc, char **argv)
{
	unsigned int errors;
	unsigned int i, n, j;
	int result_cmp, result_gold;
	float flcmp, flgold;
	float delta = 0.05;

	if (argc < 4) {
		cerr << "Must provide two file paths and a number of elements."
				<< endl;

		return 1;
	}
	n = atoi(argv[3]);

	ifstream fcmp(argv[1]);
	ifstream fgold(argv[2]);

	errors = 0;

	for (i = 0; i < n && errors < 10; i++) {
		result_cmp = read_int(fcmp);
		result_gold = read_int(fgold);

		if (result_cmp != result_gold) {
			cerr << i << ": Result mismatch, " << result_cmp << " != " <<
					result_gold << endl;
			errors++;
		}

		if (result_cmp < 1 || result_gold < 1) {
			fcmp.ignore(28);
			fgold.ignore(28);
		} else {
			for (j = 0; j < 7; j++) {
				flcmp = read_float(fcmp);
				flgold = read_float(fgold);

				if (fabs(flcmp - flgold) > delta) {
					cerr << i << ": " << param_str[j] << " mismatch, ";
					printf("%.6f != %.6f", flcmp, flgold);
					cerr << endl;
					errors++;
				}
			}
		}
	}

	if (errors >= 10)
		cerr << "Too many errors, exiting" << endl;

	fcmp.close();
	fgold.close();

	return errors;
}
