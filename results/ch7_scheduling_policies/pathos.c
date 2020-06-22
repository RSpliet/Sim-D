#include <stdio.h>

int
main(int argc, char **argv)
{
	unsigned int i;
	
	for (i = 0; i <= 700; i++) {
		if (i < 25) {
			printf("%u,0,2\n", i);
			printf("%u,1,5\n", i);
		} else {
			if ((i / 25) % 2)
				printf("%u,0,1\n", i);
			else
				printf("%u,0,4\n", i);
			
			if (i < 56)
				printf("%u,1,2\n", i);
			else if (i < 686)
				printf("%u,1,3\n", i);
			else
				printf("%u,1,0\n", i);
		}
		
		printf("%u,2,0\n\n", i);
	}
	
	return 0;
}
