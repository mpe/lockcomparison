CFLAGS = -Wall -O2 -fno-stack-protector
LDLIBS = -lrt

PROGS := test_spin_sync_sync test_spin_isync_lwsync test_spin_lwsync_lwsync \
	 test_spin_sync_lwsync test_spin_sync_sync test_spin_isync_sync \
	 test_spin_lwsync_sync

test_spin_sync_lwsync: CFLAGS += -D DO_SYNC_IO
test_spin_isync_lwsync: CFLAGS += -D DO_SYNC_IO
test_spin_lwsync_lwsync: CFLAGS += -D DO_SYNC_IO

all: $(PROGS)

test_%: lock_comparison.c
	$(CC) $(CFLAGS) -D TEST_NAME=$@ -o $@ $<

$(PROGS): lock_comparison.c

clean:
	rm -f  $(PROGS)
