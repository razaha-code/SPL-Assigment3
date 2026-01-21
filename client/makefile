CFLAGS := -c -Wall -Weffc++ -g -std=c++11 -Iinclude
LDFLAGS := -lboost_system -lpthread

all: StompWCIClient

StompWCIClient: bin/ConnectionHandler.o bin/StompClient.o bin/StompProtocol.o bin/event.o
	g++ -o bin/StompWCIClient bin/ConnectionHandler.o bin/StompClient.o bin/StompProtocol.o bin/event.o $(LDFLAGS)

bin/ConnectionHandler.o: src/ConnectionHandler.cpp | bin
	g++ $(CFLAGS) -o bin/ConnectionHandler.o src/ConnectionHandler.cpp

bin/StompClient.o: src/StompClient.cpp | bin
	g++ $(CFLAGS) -o bin/StompClient.o src/StompClient.cpp

bin/StompProtocol.o: src/StompProtocol.cpp | bin
	g++ $(CFLAGS) -o bin/StompProtocol.o src/StompProtocol.cpp

bin/event.o: src/event.cpp | bin
	g++ $(CFLAGS) -o bin/event.o src/event.cpp

bin:
	mkdir -p bin

.PHONY: clean
clean:
	rm -f bin/*
