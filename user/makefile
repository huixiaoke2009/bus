CXX = g++
RM = rm -f
CP = cp -f
CXXFLAGS += -Wall -D_GNU_SOURCE  -g -DLINUX -O0 -fPIC -Wno-invalid-offsetof
INCS = -I. -I../include -I../mmlib
LIBS = ../mmlib/mmlib.a ../include/bus.a /usr/local/lib/libprotobuf.a -lpthread -ldl -lrt
LIBS2 = ../mmlib/mmlib.a ../include/bus.a /usr/local/lib/libprotobuf.a /usr/lib64/mysql/libmysqlclient.a -lpthread -ldl -lrt
TARGETS = user writer loader
all:$(TARGETS)

user:user_main.o user_proc.o user_shm_api.o 
	$(CXX) $^ $(LIBS) -o $@ -lz

writer:writer_user.o user_shm_api.o
	$(CXX) $^ $(LIBS2) -o $@ -lz

loader:loader_user.o user_shm_api.o
	$(CXX) $^ $(LIBS2) -o $@ -lz

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(INCS) $< -o $@

clean:
	$(RM) $(TARGETS) *.o *.exe *.pb.h *.pb.cc
