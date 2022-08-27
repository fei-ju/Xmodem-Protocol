PORT=51234

CFLAGS= -DPORT=\$(PORT) -g -Wall -Werror -fsanitize=address 
DEPENDENCIES = crc16.h xmodemserver.h helper.h

all : xmodemserver

xmodemserver : xmodemserver.o crc16.o helper.o
	gcc ${CFLAGS} -o $@ $^ -lm

%.o : %.c ${DEPENDENCIES}
	gcc ${CFLAGS} -c $<
clean :
	rm -f *.o xmodemserver