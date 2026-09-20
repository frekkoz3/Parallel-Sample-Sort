# Hybrid MPI + OpenMP Compiler Setup
CC      = mpicc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -pedantic -fopenmp
LDFLAGS ?=
LDLIBS  ?= -fopenmp
BITSIZE ?= #-DUSE_UINT32 # Un-comment for 32-bit keys

TARGET  = sample_sort_mpi
SRCS    = main.c parser.c utils.c local_sort.c sample_sort.c
OBJS    = $(SRCS:.c=.o)

# Default parallel dimensions
RANKS   ?= 2
THREADS ?= 8

.PHONY: all clean run check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(BITSIZE) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(BITSIZE) -c $< -o $@

run: $(TARGET)
	OMP_NUM_THREADS=$(THREADS) mpirun -np $(RANKS) ./$(TARGET) \
		--n 100000000 \
		--distribution uniform \
		--merging_strat basic \
		--sorting merge

check: $(TARGET)
	@echo "Running MPI verification suite with RANKS=$(RANKS), OMP_NUM_THREADS=$(THREADS)..."
	OMP_NUM_THREADS=$(THREADS) mpirun -np $(RANKS) ./$(TARGET) --n 100000000 --distribution uniform --sorting radix
	OMP_NUM_THREADS=$(THREADS) mpirun -np $(RANKS) ./$(TARGET) --n 100000000 --distribution skewed --oversample 4 --sorting radix
	OMP_NUM_THREADS=$(THREADS) mpirun -np $(RANKS) ./$(TARGET) --n 100000000 --distribution few-unique --sorting radix
	OMP_NUM_THREADS=$(THREADS) mpirun -np $(RANKS) ./$(TARGET) --n 100000000 --distribution reverse --sorting radix
	OMP_NUM_THREADS=$(THREADS) mpirun -np $(RANKS) ./$(TARGET) --n 100000000 --distribution almost-sorted --sorting radix

clean:
	rm -f $(OBJS) $(TARGET)