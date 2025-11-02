CXX = g++
CXXFLAGS = -std=c++17 -O2 -Iinclude -I/usr/include/postgresql
LIBS = -lpq -pthread -lcurl

all: server loadgen

server:
	$(CXX) $(CXXFLAGS) -o kv_server src/server.cpp $(LIBS)

loadgen:
	$(CXX) $(CXXFLAGS) -o loadgen src/loadgen.cpp $(LIBS)

clean:
	rm -f kv_server loadgen
