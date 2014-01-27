all: miner

miner: miner.cpp
	g++ -pthread -fomit-frame-pointer -Wall -Werror -pedantic -Wno-deprecated-declarations -std=c++11 -O3 -g -o miner miner.cpp -lssl -lcrypto

clean:
	rm -rf miner wd* *.data* *.dSYM
