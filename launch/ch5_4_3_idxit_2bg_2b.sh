#!/bin/bash

for i in {0..32767}
do
	x="0 `(((${i}&16384)) && echo '1' || echo '0')` `(((${i}&8192)) && echo '1' || echo '0')` `(((${i}&4096)) && echo '1' || echo '0')` `(((${i}&2048)) && echo '1' || echo '0')` `(((${i}&1024)) && echo '1' || echo '0')` `(((${i}&512)) && echo '1' || echo '0')` `(((${i}&256)) && echo '1' || echo '0')` `(((${i}&128)) && echo '1' || echo '0')` `(((${i}&64)) && echo '1' || echo '0')` `(((${i}&32)) && echo '1' || echo '0')` `(((${i}&16)) && echo '1' || echo '0')` `(((${i}&8)) && echo '1' || echo '0')` `(((${i}&4)) && echo '1' || echo '0')` `(((${i}&2)) && echo '1' || echo '0')` `(((${i}&1)) && echo '1' || echo '0')`"
	echo -n "${x}, "
	echo `echo ${x} | src/mc/mcIdx -f - -b 0x3ffc | grep "Least" | cut -c 34-43 | xargs -E0 printf %u`
done
