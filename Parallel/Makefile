CC      ?= gcc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -pedantic -fopenmp
LDFLAGS ?=
LDLIBS  ?= -fopenmp
BITSIZE ?= #-DUSE_UINT32 # for 32 bits keys

TARGET  = sample_sort_omp
SRCS    = main.c parser.c utils.c local_sort.c sample_sort.c
OBJS    = $(SRCS:.c=.o)

# Default number of threads for execution targets
THREADS ?= 64

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
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000000 --nbuckets 16 --distribution uniform --sorting radix
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000000 --nbuckets 16 --distribution skewed --oversample 4 --sorting radix
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000000 --nbuckets 16 --distribution few-unique --sorting radix
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000000 --nbuckets 16 --distribution reverse --sorting radix
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000000 --nbuckets 16 --distribution almost-sorted --sorting radix

clean:
	rm -f $(OBJS) $(TARGET)
