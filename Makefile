CC = gcc
CFLAGS = -Wall
LDLIBS = -ljson-c -luuid
objects = main.o common.o db.o protocol.o

etestd : $(objects)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(objects) : common.h
common.o : common.h
db.o : db.h
protocol.o : protocol.h

.PHONY : clean
clean :
	$(RM) etestd $(objects)
