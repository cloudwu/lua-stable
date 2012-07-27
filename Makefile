default : linux

all : test lua-stable testmt

linux : all
linux : CFLAGS = -fpic
linux : SO = so

win32 : all
win32 : CFLAGS = -march=i686 
win32 : LUA = -I/usr/local/include -L/usr/local/bin -llua52
win32 : SO = dll

test : stable.c test.c
	gcc -g -Wall $(CFLAGS) -o $@ $^

testmt : stable.c testmt.c
	gcc -g -Wall $(CFLAGS) -o $@ $^ -lpthread

lua-stable : stable.c lua-stable.c
	gcc -g -Wall $(CFLAGS) $(LUA) --shared -o stable.$(SO) $^



