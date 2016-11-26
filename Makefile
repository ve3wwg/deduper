# Makefile for deduper
######################################################################

include Makefile.incl

LDFLAGS		= -L/usr/local/lib
#		  -Wl,-R$(PREFIX)/lib

.PHONY:	all clean clobber install

TARGETS = deduper

all:	Makefile.deps $(TARGETS)

install: all

OBJS	= system.o

deduper: $(OBJS)

test_system: system.x1o
	$(CXX) -o a.out system.x1o $(LDFLAGS)

system.x1o: system.hpp

clean:	
	rm -f *.o *.x1o a.out

clobber: clean
	@rm -f .errs.t
	rm -f $(TARGETS)

######################################################################
#  Dependencies
######################################################################

-include Makefile.deps

Makefile.deps: Makefile
	$(CXX) $(CXXFLAGS) -MM 	$(OBJ:.o=.cpp) >Makefile.deps

# End Makefile
