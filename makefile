#include ../../incl.mk

# CFLAGS += -g -Wall -O2
LIB += -ldl -lssl -lcrypto -lpthread
# INC += -std=c++0x

OBJ = \
	tlib_cfg.o \
	virtual_client.o \
	tcp_client.o \
	http_client.o \
	https_client.o \
	user_func.o \
	presscall.o

TARGET = presscall
#############################################################
$(TARGET): $(OBJ)
	g++ -o $@ $^ $(LIB)

%.o: %.cpp %.h
	g++ $(CFLAGS) $(INC) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

