CXX = g++
RM = rm -f
CP = cp -f
CXXFLAGS += -Wall -D_GNU_SOURCE  -g -DLINUX -O0 -fPIC -Wno-invalid-offsetof
INCS = -I. -I../include -I../mmlib
LIBS = ../mmlib/mmlib.a ../include/bus.a /usr/local/lib/libprotobuf.a

TARGETS = bus
all:$(TARGETS)

$(TARGETS):bus_main.o bus.o
	$(CXX) $^ $(LIBS) -o $@ -lz

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(INCS) $< -o $@

clean:
	$(RM) $(TARGETS) *.o *.exe *.pb.h *.pb.cc
