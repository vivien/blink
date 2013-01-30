CFLAGS += -Wall

blink: blink.c
	$(CC) $(CFLAGS) -o blink blink.c

clean:
	rm -f blink
