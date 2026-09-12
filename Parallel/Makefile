CC      ?= gcc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -pedantic -fopenmp
LDFLAGS ?=
LDLIBS  ?= -fopenmp
BITSIZE ?= #-DUSE_UINT32 # for 32 bits keys

TARGET  = sample_sort_omp
SRCS    = main.c parser.c utils.c local_sort.c sample_sort.c
OBJS    = $(SRCS:.c=.o)

# Default number of threads for execution targets
THREADS ?= 16

.PHONY: all clean run check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(BITSIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(BITSIZE) -c $< -o $@

run: $(TARGET)
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000000 --nbuckets 16 --distribution uniform --merging_strat basic --sorting radix

check: $(TARGET)
	@echo "Running test suite with OMP_NUM_THREADS=$(THREADS)..."
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution uniform
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution skewed --oversample 4
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution few-unique
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution reverse
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution almost-sorted

clean:
	rm -f $(OBJS) $(TARGET)