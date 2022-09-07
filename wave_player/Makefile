.PHONY: clean 

C_SRCS = wav_player.c 
OUT := wav_player

CFLAGS   = -O2 -g
LDFLAGS  = -lpulse -lasound -lpthread -lm
CC ?= gcc -std=c99

OBJS := $(C_SRCS:.c=.o)

$(OUT): $(OBJS) 
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(OUT)

clean:
	rm $(OBJS) $(OUT) 

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@
