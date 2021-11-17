CFLAGS += -Wall -O2
# DEBUGFLAG = -g
LIB += -lstdc++ -ldl -lcrypto -lpthread -lssl
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
	gcc $(CFLAGS) -o $@ $^ $(LIB)

%.o: %.cpp %.h
	gcc $(CFLAGS) $(DEBUGFLAG) $(INC) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

