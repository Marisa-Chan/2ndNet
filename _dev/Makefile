CC = gcc
CXX = g++

LIBS = -lSDL2 -lSDL2_net

OBJS =  \
	client.cpp\
	client_threads.cpp\
	main.cpp\
	reciever.cpp\
	sender.cpp\
	server.cpp\
	server_threads.cpp\
	session.cpp\
	single.cpp\
	single_threads.cpp\
	zndNet.cpp\
	test/crc32.cpp
	
server: $(OBJS)
	$(CXX) -DSEPARATE_TEST_SERVER -std=c++11 -O0 -g -o zndserver $(OBJS) $(LIBS)

client: $(OBJS)
	$(CXX) -DSEPARATE_TEST_CLIENT -std=c++11 -O0 -g -o zndclient $(OBJS) $(LIBS)

single: $(OBJS)
	$(CXX) -DSEPARATE_TEST_SINGLE -std=c++11 -O0 -g -o zndsingle $(OBJS) $(LIBS)
