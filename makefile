MMLIB = mmlib
INCLUDE = include
BUS = bus
LOGSVR = logsvr
AUTH = auth
USER = user
CONN = conn

MAKE = make
CLEAN = clean
INSTALL = install


all:
	cd $(MMLIB) && $(MAKE)
	cd $(INCLUDE) && $(MAKE)
	cd $(BUS) && $(MAKE)
	cd $(LOGSVR) && $(MAKE)
	cd $(AUTH) && $(MAKE)
	cd $(CONN) && $(MAKE)
	cd $(USER) && $(MAKE)
clean:
	cd $(MMLIB) && $(MAKE) $(CLEAN)
	cd $(INCLUDE) && $(MAKE) $(CLEAN)
	cd $(BUS) && $(MAKE) $(CLEAN)
	cd $(LOGSVR) && $(MAKE) $(CLEAN)
	cd $(AUTH) && $(MAKE) $(CLEAN)
	cd $(CONN) && $(MAKE) $(CLEAN)
	cd $(USER) && $(MAKE) $(CLEAN)
