#!/bin/bash

let i=0

concat_repeat () {
	local a=0
	for a in $(seq $1)
	do
		str="${str}${2} "
	done
}

for a in {1..10}
do
	for ((b=a+1;b<11;b++))
	do
		for ((c=b+1;c<12;c++))
		do
			for ((d=c+1;d<13;d++))
			do
				for ((e=d+1;e<14;e++))
				do
					for ((f=e+1;f<15;f++))
					do
						for ((g=f+1;g<16;g++))
						do
							let i++;
							str=""
							concat_repeat ${a} "0"
							concat_repeat $(expr $b - $a) "17"
							concat_repeat $(expr $c - $b) "4113"
							concat_repeat $(expr $d - $c) "8209"
							concat_repeat $(expr $e - $d) "1"
							concat_repeat $(expr $f - $e) "33"
							concat_repeat $(expr $g - $f) "4129"
							concat_repeat $(expr 16 - $g) "8225"
							
							echo "${str}"`echo ${str} | src/mc/mcIdx -f - -b 0x3fbc | grep "Least" | cut -c 34-43 | xargs -E0 printf %u`
						done
					done
				done
			done
		done
	done
done

