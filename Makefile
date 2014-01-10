PROG = chemcheck
OBJECTS = 

#CC = cc
CFLAGS += -g -Wall -include standard.h -DG_LOG_DOMAIN=\"$(P)\" `pkg-config --cflags glib-2.0`
LDFLAGS +=
LDLIBS += `pkg-config --libs glib-2.0 --libs libcsv-3.0`

all: $(PROG)

$(PROG): $(OBJECTS)

clean:
	rm -f *.o $(PROG)
	rm -rf $(PROG).dSYM
