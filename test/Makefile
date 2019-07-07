CC = gcc
CXX = g++

LIBS = -lSDL2 -lSDL2_net -l2ndnet -L../

OBJS =  \
	main.cpp\
	crc32.cpp
	
server: $(OBJS)
	$(CXX) -DSEPARATE_TEST_SERVER -std=c++11 -I../lib/ -O0 -g -o zndserver $(OBJS) $(LIBS)

client: $(OBJS)
	$(CXX) -DSEPARATE_TEST_CLIENT -std=c++11 -I../lib/ -O0 -g -o zndclient $(OBJS) $(LIBS)

single: $(OBJS)
	$(CXX) -DSEPARATE_TEST_SINGLE -std=c++11 -I../lib/ -O0 -g -o zndsingle $(OBJS) $(LIBS)
