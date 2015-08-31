CFLAGS = -Wall -O2
LDLIBS = -lrt

PROGS := test_spin_sync_sync test_spin_isync_lwsync test_spin_lwsync_lwsync \
	 test_spin_sync_lwsync test_spin_sync_sync test_spin_isync_sync

all: $(PROGS)

test_%: lock_comparison.c
	$(CC) $(CFLAGS) -D TEST_NAME=$@ -o $@ $<

$(PROGS): lock_comparison.c

clean:
	rm -f  $(PROGS)
