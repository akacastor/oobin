TARGET         = oobin
CSRC           = oobin.c main.c rscode-1.3/rs.c rscode-1.3/rs.c rscode-1.3/berlekamp.c rscode-1.3/galois.c

OPTIMIZE       = -O2

DEFS            = -D_SOFT_NAME_=\"$(TARGET)\" -D_SOFT_VER_=\"1.00\"


CC             = gcc
CFLAGS         = -Wall $(OPTIMIZE) $(DEFS)
#LDFLAGS        = -Wl,-u,vfprintf -lprintf_flt
OBJ            = $(CSRC:.c=.o)


all: $(TARGET)


$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)


%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@


clean:
	rm -rf *.o rscode-1.3/*.o $(TARGET)
