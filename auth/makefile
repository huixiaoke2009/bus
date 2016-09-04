CXX = g++
RM = rm -f
CP = cp -f
CXXFLAGS += -Wall -D_GNU_SOURCE  -g -DLINUX -O0 -fPIC -Wno-invalid-offsetof
INCS = -I. -I../include -I../mmlib
LIBS = ../mmlib/mmlib.a /usr/lib64/mysql/libmysqlclient.a ../include/bus.a /usr/local/lib/libprotobuf.a -lpthread -ldl -lrt

TARGETS = auth
all:$(TARGETS)

auth:auth_main.o auth_proc.o
	$(CXX) $^ $(LIBS) -o $@ -lz

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(INCS) $< -o $@

clean:
	$(RM) $(TARGETS) *.o *.exe *.pb.h *.pb.cc
