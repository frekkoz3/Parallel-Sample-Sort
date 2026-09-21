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

sorting_comparison: $(TARGET)
	@echo "Running comparison of local sorting algorithm"
	@for number in 1 2 4 8 16 32 64; do \
		OMP_NUM_THREADS=$$number mpirun -np 1 ./$(TARGET) --n 50000000 --sorting radix --where_save ./results/sorting_comparison.csv; \
		OMP_NUM_THREADS=$$number mpirun -np 1 ./$(TARGET) --n 50000000 --sorting quick --where_save ./results/sorting_comparison.csv; \
	done

merging_comparison: $(TARGET)
	@echo "Running comparison of merging strategy"
	@for number in 4 8 16 32 64; do \
		OMP_NUM_THREADS=2 mpirun -np $$number ./$(TARGET) --n 50000000 --merging_strat basic --where_save ./results/merging_comparison.csv; \
		OMP_NUM_THREADS=2 mpirun -np $$number ./$(TARGET) --n 50000000 --merging_strat bin   --where_save ./results/merging_comparison.csv; \
		OMP_NUM_THREADS=2 mpirun -np $$number ./$(TARGET) --n 50000000 --merging_strat heap  --where_save ./results/merging_comparison.csv; \
	done

oversampling_tuning: $(TARGET)
	@echo "Searching for the optimal oversampling factor"

strong_scaling: $(TARGET)
	@echo "Running strong scaling experiment"
	@for number in 4 8 16 32 64; do \
		OMP_NUM_THREADS=4 mpirun -np $$number ./$(TARGET) --n 1000000000 --where_save ./results/strong_scaling.csv; \
	done

weak_scaling: $(TARGET)
	@echo "Running weak scaling experiment"
	@for number in 4 8 16 32; do \
		N=$$((20000000 * number)); \
		OMP_NUM_THREADS=4 mpirun -np $$number ./$(TARGET) --n $$N  --where_save ./results/weak_scaling.csv; \
	done

skewed_input: $(TARGET)
	@echo "Running skewed input experiment"
	@for number in 8 16 32; do \
		OMP_NUM_THREADS=4 mpirun -np $$number ./$(TARGET) --n 100000000 --distribution skewed  --where_save ./results/skewed_input.csv; \
	done

clean:
	rm -f $(OBJS) $(TARGET)
