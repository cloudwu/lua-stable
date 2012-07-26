all : test testmt lua-stable

test : stable.c test.c
	gcc -g -Wall -o $@ $^

testmt : stable.c testmt.c
	gcc -g -Wall -o $@ $^ -lpthread

lua-stable : stable.c lua-stable.c
	gcc -g -Wall -fpic --shared -o stable.so $^



