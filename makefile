MMLIB = mmlib
INCLUDE = include
BUS = bus
AUTH = auth
LOGSVR = logsvr

MAKE = make
CLEAN = clean
INSTALL = install
CONN = conn

all:
	cd $(MMLIB) && $(MAKE)
	cd $(INCLUDE) && $(MAKE)
	cd $(BUS) && $(MAKE)
	cd $(AUTH) && $(MAKE)
	cd $(LOGSVR) && $(MAKE)
	cd $(CONN) && $(MAKE)
clean:
	cd $(MMLIB) && $(MAKE) $(CLEAN)
	cd $(INCLUDE) && $(MAKE) $(CLEAN)
	cd $(BUS) && $(MAKE) $(CLEAN)
	cd $(AUTH) && $(MAKE) $(CLEAN)
	cd $(LOGSVR) && $(MAKE) $(CLEAN)
	cd $(CONN) && $(MAKE) $(CLEAN)
