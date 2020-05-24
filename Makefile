.PHONY: all shared shared-example clean

all : example

os_generic.h :
	wget https://raw.githubusercontent.com/cntools/rawdraw/master/os_generic.h

example : example.c os_generic.h
	$(CC) -o $@ $^ -lpulse -lasound -lpthread -lm

shared : os_generic.h
	$(CC) CNFA.c -shared -fpic -o libCNFA.so -DCNFA_IMPLEMENTATION -DBUILD_DLL \
		-lasound -lpulse -lpthread

shared-example : example.c shared
	$(CC) -L. -Wl,-rpath=. -o example example.c -DUSE_SHARED -lm -lCNFA

clean :
	rm -rf *.o *~ example libCNFA.so

