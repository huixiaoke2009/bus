CXX = g++
RM = rm -f
CP = cp -f
CXXFLAGS += -Wall -D_GNU_SOURCE  -g -DLINUX -O0 -fPIC 
INCS = -I. -I../include -I../mmlib 
LIBS = ../mmlib/mmlib.a /usr/local/lib/libprotobuf.a -lpthread

TARGETS = conn

all:$(TARGETS)

conn:conn_main.o conn.o
	$(CXX) $^ $(LIBS) -o $@


%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(INCS) $< -o $@

clean:
	$(RM) $(TARGETS) *.o *.exe

install:
	$(CP) $(TARGETS) /data/xserver/bin/$(TARGETS)
	
total:
	make clean&&make&&make install
