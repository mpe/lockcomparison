CFLAGS = -Wall -O2 -fno-stack-protector
LDLIBS = -lrt

PROGS := test_spin_sync_sync test_spin_isync_lwsync test_spin_lwsync_lwsync \
	 test_spin_sync_lwsync test_spin_sync_sync test_spin_isync_sync \
	 test_spin_lwsync_sync

test_spin_sync_lwsync: CFLAGS += -D DO_SYNC_IO
test_spin_isync_lwsync: CFLAGS += -D DO_SYNC_IO
test_spin_lwsync_lwsync: CFLAGS += -D DO_SYNC_IO

LSTS := $(subst test_,,$(addsuffix .lst,$(PROGS)))

all: $(PROGS) $(LSTS)

test: all
	run-parts --regex="test_spin_*" .

test_%: lock_comparison.c
	$(CC) $(CFLAGS) -D TEST_NAME=$@ -o $@ $<

%.lst: test_%
	objdump -d $< > $@

$(PROGS): lock_comparison.c

clean:
	rm -f  $(PROGS) $(LSTS)

.PHONY: all clean
