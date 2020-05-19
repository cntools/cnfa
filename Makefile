all : example

os_generic.h :
	wget https://raw.githubusercontent.com/cntools/rawdraw/master/os_generic.h

example : example.c os_generic.h
	gcc -o $@ $^ -lpulse -lasound -lpthread -lm

clean :
	rm -rf *.o *~ example

