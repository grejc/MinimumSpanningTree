CC = gcc
MPICC = mpicc

SRC_SEQ = minspatree_seq.c
SRC_PAR = minspatree_par.c
SRC_COMMON = graph.c

TARGET_SEQ = minspatree_seq.out
TARGET_PAR = minspatree_par.out

SEQ_FLAGS = -Wall -O3 -I.
MPI_INCLUDE_TYPE := $(shell [ "$$(whoami)" = "rgm42426" ] && echo "FALLBACK" || echo "DEFAULT")
PAR_FLAGS = -DGREJC_SETUP_MPI_$(MPI_INCLUDE_TYPE) -Wall -O3 -I.
LDFLAGS =

OBJ_SEQ = minspatree_seq.o graph_seq.o
OBJ_PAR = minspatree_par.o graph_par.o

.PHONY: default
default: all

.PHONY: all
all: compile-seq compile-par
	@echo "====== A L L   D O N E ======"

.PHONY: compile-seq
compile-seq: $(TARGET_SEQ)
	@echo "  → Compiled sequential version!"

.PHONY: compile-par
compile-par: $(TARGET_PAR)
	@echo "  → Compiled parallel version!"


$(TARGET_SEQ): $(OBJ_SEQ)
	$(CC) $(SEQ_FLAGS) $(LDFLAGS) -o $@ $^

$(TARGET_PAR): $(OBJ_PAR)
	$(MPICC) $(PAR_FLAGS) $(LDFLAGS) -o $@ $^

minspatree_seq.o: minspatree_seq.c utils.h graph.h
	$(CC) $(SEQ_FLAGS) -c -o $@ $<

minspatree_par.o: minspatree_par.c utils.h graph.h
	$(MPICC) $(PAR_FLAGS) -c -o $@ $<

graph_seq.o: graph.c graph.h
	$(CC) $(SEQ_FLAGS) -c -o $@ $<

graph_par.o: graph.c graph.h
	$(MPICC) $(PAR_FLAGS) -c -o $@ $<

.PHONY: clean
clean: clean-seq clean-par
	@echo "   all files have been removed..."

.PHONY:clean-seq
clean-seq:
	rm -f $(TARGET_SEQ) minspatree_seq.o graph_seq.o *.o

.PHONY: clean-par
clean-par:
	rm -f $(TARGET_PAR) minspatree_par.o graph_par.o *.o

.PHONY: seq
seq: compile-seq

.PHONY: par
par: compile-par
