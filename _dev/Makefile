CC = gcc
CXX = g++

LIBS = -lSDL2 -lSDL2_net

OBJS =  \
	client.cpp\
	main.cpp\
	memread.cpp\
	packets.cpp\
	reciever.cpp\
	refdata.cpp\
	sender.cpp\
	server.cpp\
	session.cpp\
	zndNet.cpp\
	test/crc32.cpp
	
server: $(OBJS)
	$(CXX) -DSEPARATE_TEST_SERVER -std=c++11 -O0 -g -o zndserver $(OBJS) $(LIBS)

client: $(OBJS)
	$(CXX) -DSEPARATE_TEST_CLIENT -std=c++11 -O0 -g -o zndclient $(OBJS) $(LIBS)
