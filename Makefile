CFLAGS = -Wall -O2
LDLIBS = -lpthread

all: lock_comparison

clean:
	rm -f lock_comparison
