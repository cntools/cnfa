.PHONY: clean 

C_SRCS = wav_player.c 
OUT := wav_player

CFLAGS   = -O2 -g -DUSE_SHARED
LDFLAGS  = -L../. -Wl,-rpath=. -lCNFA -lm
CC ?= gcc -std=c99

OBJS := $(C_SRCS:.c=.o)

$(OUT): $(OBJS) 
	$(CC) $(CFLAGS) $(OBJS) -o $(OUT) $(LDFLAGS) 

clean:
	rm $(OBJS) $(OUT) 

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@
