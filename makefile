CXX = g++
CXXFLAGS = -std=c++17 -O2 -I/usr/include/postgresql
LIBS = -lpq -lhiredis -lcurl -pthread

all: server loadgen

server:
	$(CXX) $(CXXFLAGS) server.cpp -o server $(LIBS)

loadgen:
	$(CXX) $(CXXFLAGS) loadgen.cpp -o loadgen $(LIBS)

clean:
	rm -f server loadgen
