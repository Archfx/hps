CC=gcc
CFLAGS=-O2 -Wall -Iincludes

SRC_DIR=src
INC_DIR=includes

OBJS=$(SRC_DIR)/main.o \
     $(SRC_DIR)/hw_config.o \
     $(SRC_DIR)/workload.o \
     $(SRC_DIR)/scheduler.o \
     $(SRC_DIR)/simulator.o

all: tfhe_sim

tfhe_sim: $(OBJS)
	$(CC) $(CFLAGS) -o tfhe_sim $(OBJS)

main.o: $(SRC_DIR)/main.c $(INC_DIR)/types.h $(INC_DIR)/hw_config.h \
         $(INC_DIR)/workload.h $(INC_DIR)/scheduler.h $(INC_DIR)/simulator.h

$(SRC_DIR)/hw_config.o: $(SRC_DIR)/hw_config.c $(INC_DIR)/hw_config.h $(INC_DIR)/types.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/hw_config.c -o $(SRC_DIR)/hw_config.o

$(SRC_DIR)/workload.o: $(SRC_DIR)/workload.c $(INC_DIR)/workload.h $(INC_DIR)/types.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/workload.c -o $(SRC_DIR)/workload.o

$(SRC_DIR)/scheduler.o: $(SRC_DIR)/scheduler.c $(INC_DIR)/scheduler.h $(INC_DIR)/types.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/scheduler.c -o $(SRC_DIR)/scheduler.o

$(SRC_DIR)/simulator.o: $(SRC_DIR)/simulator.c $(INC_DIR)/simulator.h \
                         $(INC_DIR)/scheduler.h $(INC_DIR)/types.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/simulator.c -o $(SRC_DIR)/simulator.o

clean:
	rm -f tfhe_sim *.o $(SRC_DIR)/*.o
