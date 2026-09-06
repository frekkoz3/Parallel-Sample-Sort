CC      ?= gcc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -pedantic -fopenmp
LDFLAGS ?=
LDLIBS  ?= -fopenmp

TARGET  = sample_sort_omp
SRCS    = main.c parser.c utils.c local_sort.c sample_sort.c
OBJS    = $(SRCS:.c=.o)

# Default number of threads for execution targets
THREADS ?= 16

.PHONY: all clean run check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 1000000 --nbuckets 8 --distribution uniform

check: $(TARGET)
	@echo "Running test suite with OMP_NUM_THREADS=$(THREADS)..."
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution uniform
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution skewed --oversample 4
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution few-unique
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution reverse
	OMP_NUM_THREADS=$(THREADS) ./$(TARGET) --n 100000 --nbuckets 8 --distribution almost-sorted

clean:
	rm -f $(OBJS) $(TARGET)