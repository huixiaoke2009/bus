MMLIB = mmlib
INCLUDE = include
BUS = bus
LOGSVR = logsvr

MAKE = make
CLEAN = clean
INSTALL = install

all:
	cd $(MMLIB) && $(MAKE)
	cd $(INCLUDE) && $(MAKE)
	cd $(BUS) && $(MAKE)
	cd $(LOGSVR) && $(MAKE)

clean:
	cd $(MMLIB) && $(MAKE) $(CLEAN)
	cd $(INCLUDE) && $(MAKE) $(CLEAN)
	cd $(BUS) && $(MAKE) $(CLEAN)
	cd $(LOGSVR) && $(MAKE) $(CLEAN)

