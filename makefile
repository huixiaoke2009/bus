MMLIB = mmlib
INCLUDE = include
BUS = bus
APP = app
LOGSVR = logsvr

MAKE = make
CLEAN = clean
INSTALL = install

all:
	cd $(MMLIB) && $(MAKE)
	cd $(INCLUDE) && $(MAKE)
	cd $(BUS) && $(MAKE)
	cd $(APP) && $(MAKE)
	cd $(LOGSVR) && $(MAKE)

clean:
	cd $(MMLIB) && $(MAKE) $(CLEAN)
	cd $(INCLUDE) && $(MAKE) $(CLEAN)
	cd $(BUS) && $(MAKE) $(CLEAN)
	cd $(APP) && $(MAKE) $(CLEAN)
	cd $(LOGSVR) && $(MAKE) $(CLEAN)

