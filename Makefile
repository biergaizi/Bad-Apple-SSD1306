all:
	gcc -o 12864 -l rt 12864.c bcm2835.c -O2 -march=native

