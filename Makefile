CC = gcc
CFLAGS = -Wall
LDLIBS = -ljson-c -luuid -lnettle
objects = main.o common.o db.o protocol.o

all : etestd

etestd : $(objects)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

etestd-static : $(objects)
	$(CC) $(LDFLAGS) -static $^ $(LDLIBS) -o $@	

debug : CFLAGS += -g
debug : etestd

$(objects) : common.h
common.o : common.h
db.o : db.h
protocol.o : protocol.h

.PHONY : clean debug
clean :
	$(RM) etestd etestd-static $(objects)
