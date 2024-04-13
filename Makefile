.PHONY: all shared shared-example clean 

all: example wav_player

os_generic.h:
	wget https://raw.githubusercontent.com/cntools/rawdraw/master/os_generic.h

LDFLAGS = -lasound -lpthread
ifeq "$(shell pkgconf --exists libpulse && echo 'found' )" "found"
PULSE ?= YES
else
PULSE ?= NO
endif
ifeq "$(PULSE)" "YES"
LDFLAGS += -lpulse -DPULSEAUDIO
endif

example : example.c os_generic.h
	$(CC) -o $@ $^ $(LDFLAGS) -lm

wav_player: shared
	make -C wave_player PULSE=$(PULSE)
	cp wave_player/wav_player ./

shared : os_generic.h
	$(CC) CNFA.c -shared -fpic -o libCNFA.so -DCNFA_IMPLEMENTATION -DBUILD_DLL \
		$(LDFLAGS)

shared-example : example.c shared
	$(CC) -L. -Wl,-rpath=. -o example example.c -DUSE_SHARED -lm -lCNFA

clean :
	-rm -rf *.o *~ example libCNFA.so wav_player
	-make -C wave_player clean

