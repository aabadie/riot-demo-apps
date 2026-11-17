# TinyFL on native nodes connected by the ZEP dispatcher of RIOT
RIOTBASE ?= ../../../RIOT
DISPATCHER = $(RIOTBASE)/dist/tools/zep_dispatch/bin/zep_dispatch

test: $(DISPATCHER)
	$(MAKE) -C ../.. BOARD=native64 RIOTBASE=$(abspath $(RIOTBASE)) all
	python3 run.py --dispatcher $(DISPATCHER) --nodes 4 --seconds 30 --port 17760
	python3 run.py --dispatcher $(DISPATCHER) --nodes 10 --seconds 60 --topology line --port 17761
	python3 run.py --dispatcher $(DISPATCHER) --nodes 4 --seconds 85 --restart 15 --port 17762

$(DISPATCHER):
	$(MAKE) -C $(RIOTBASE)/dist/tools/zep_dispatch

.PHONY: test
