CFLAGS = -Wall -O2
LDLIBS = -lrt

all: lock_comparison

clean:
	rm -f lock_comparison
