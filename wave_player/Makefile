.PHONY: clean 

C_SRCS = wav_player.c 
OUT := wav_player

CFLAGS   = -O2 -g
LDFLAGS  = -lasound -lpthread -lm
CC ?= gcc -std=c99
ifeq "$(shell pkgconf --exists pulse && echo 'found' )" "found"
PULSE ?= "YES"
else
PULSE ?= "NO"
endif
ifeq "$(PULSE)" "YES"
LDFLAGS += -lpulse
endif

OBJS := $(C_SRCS:.c=.o)

$(OUT): $(OBJS) 
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(OUT)

clean:
	rm $(OBJS) $(OUT) 

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@
