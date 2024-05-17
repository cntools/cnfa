.PHONY: all shared shared-example clean 

all: example wav_player

os_generic.h:
	wget https://raw.githubusercontent.com/cntools/rawdraw/master/os_generic.h


ifeq ($(OS),Windows_NT)
    HOST_OS = Windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        HOST_OS = Linux
    else ifeq ($(UNAME_S),Darwin)
        HOST_OS = Darwin
    endif
endif

ifeq ($(HOST_OS),Windows)
	LDFLAGS = -lwinmm -lpthread
else ifeq ($(HOST_OS),Linux)
	LDFLAGS = -lasound -lpthread
else ifeq ($(UNAME_S),Darwin)
	LDFLAGS = -lpthread
endif

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


tools/single_file_creator : tools/single_file_creator.c
	gcc -o $@ $^

CNFA_sf.h : tools/single_file_creator CNFA.h
	echo "//This file was automatically generated by Makefile at https://github.com/cnlohr/cnfa" > $@
	echo "//This single-file feature is still in early alpha testing." >> $@f
	echo "//Generated from files git hash $(shell git rev-parse HEAD) on $(shell date) (This is not the git hash of this file)" >> $@
	./tools/single_file_creator CNFA.h CNFA.c CNFA_android.c CNFA_alsa.c CNFA_null.c CNFA_pulse.c CNFA_sun.c CNFA_wasapi.c CNFA_winmm.c >> $@

clean :
	-rm -rf *.o *~ example libCNFA.so wav_player
	-make -C wave_player clean

