#include <cassert>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <stdio.h>
#include <sstream>
#include <string.h>
#include <vector>
#include "Timer.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std;

static const uint64_t ThreadCount = 12;



struct Sha1Digest {
	unsigned char v[SHA_DIGEST_LENGTH];

	/// Constructor
	Sha1Digest() {}
	/// Constructor
	Sha1Digest(const char* c,int64_t len) { fromString(c,len); }
	/// Sha1 from a string
	void fromString(const char* c,int32_t len) {
	    SHA1((const unsigned char *)c, len, reinterpret_cast<unsigned char*>(&v));
	}
	int hexValue(char c) {
		if (c >= '0' && c <= '9') return c-'0';
		if (c >= 'a' && c <= 'z') return (c-'a')+10;
		if (c >= 'A' && c <= 'Z') return (c-'A')+10;
		std::cout << "WHAT? " << (int)c << std::endl;
		throw;
	}
	/// Sha1 read from hex string
	void fromHexString(const char* c,int32_t len) {
		assert(len%2==0);
		unsigned char* temp=reinterpret_cast<unsigned char*>(&v);
		memset(temp,0,SHA_DIGEST_LENGTH);
		for (int i=0; i<std::min<int>(SHA_DIGEST_LENGTH,len/2); ++i) {
			temp[i] = (hexValue(c[i*2]) << 4) + hexValue(c[i*2+1]);
		}
	}
	/// To hex string
	std::string toHexString() const {
	    char buf[SHA_DIGEST_LENGTH*2];
		const unsigned char* temp=reinterpret_cast<const unsigned char*>(&v);
	    for (int i=0; i < SHA_DIGEST_LENGTH; i++) {
	        sprintf((char*)&(buf[i*2]), "%02x", temp[i]);
	    }
	    return std::string(buf,SHA_DIGEST_LENGTH*2);
	}
	/// Comparison
	bool operator<(const Sha1Digest& other) const { 
		return std::lexicographical_compare(v,v+SHA_DIGEST_LENGTH,other.v,other.v+SHA_DIGEST_LENGTH);
	}
};


struct UpdatableSha1Digest : public Sha1Digest {
	SHA_CTX fixedContext;
	SHA_CTX fullContext;

	UpdatableSha1Digest() {
		if(!SHA1_Init(&fixedContext)) throw;
	}

	void setFixed(const char* input,int length) {
		if(!SHA1_Update(&fixedContext, (unsigned char*)input, length)) throw;
	}

	void setVariable(const char* input,int length) {
		fullContext=fixedContext;
		if(!SHA1_Update(&fullContext, (unsigned char*)input, length)) throw;
		if(!SHA1_Final(v, &fullContext)) throw;
	}
};


int mySystem(const std::string& dir,std::string cmd) {
	cmd = std::string("cd ") + dir + " ; " + cmd;
	return (system(cmd.c_str()));
}


static std::string run(const std::string& dir,std::string cmd) {
	cmd = std::string("cd ") + dir + ";" + cmd;
	FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while(!feof(pipe)) {
    	if(fgets(buffer, 128, pipe) != NULL)
    		result += buffer;
    }
    pclose(pipe);
    return result.substr(0,result.length()-1); //HACK
}

struct WorkingDir {
	std::string repository;
	std::string path;
	bool slave;

	/// Constructor
	WorkingDir(std::string repository,std::string path,bool slave=false) : repository(repository),path(path),slave(slave) {
		// Remove old directory
		assert(path.length() > 3);
		run(".",std::string("rm -rf ") + path);

		// Clone fresh one
		if (slave) {
			run(".",std::string("cp -rpf wdmonitor ") + path);		
		} else {
			std::stringstream cmd; cmd << "git clone " << repository << " " << path << " > /dev/null 2>&1";
			assert(mySystem(".",cmd.str().c_str())==0&&"Clone failed!");
		}
	}

	/// Destructor
	~WorkingDir() {
		run(".",std::string("rm -rf ") + path);
	}

	/// Reset working directory
	void reset() {
		if (slave) {
			assert(mySystem(path,"git fetch ../wdmonitor > /dev/null 2>&1")==0);
			assert(mySystem(path,"git reset --hard FETCH_HEAD > /dev/null 2>&1")==0);
		} else {
			assert(mySystem(path,"git fetch > /dev/null 2>&1")==0);
			assert(mySystem(path,"git reset --hard origin/HEAD > /dev/null 2>&1")==0);
		}
	}

	/// Is there an upstream commit I need to know about?
	bool upstreamChanged() {
		std::string t=run(path,"git pull 2>&1");
		return t.find("From")!=std::string::npos;
	}

	/// Add commit
	bool commit(const char* ptr,int len) {
		ofstream f(path + "/commit"); 
		f.write(ptr,len);
		f.close();
		std::string hash=run(path,"git hash-object -t commit -w commit");

		assert(0==mySystem(path,std::string("git reset --hard ") + hash));
		run(path,"git push origin master");
		return true;
	}

	/// Add / fix ledger file
	void fixLedger(std::string username) {
		std::stringstream cmd;
		cmd << "perl -i -pe 's/(" << username << ": )(\\d+)/$1 . ($2+1)/e' LEDGER.txt";
		assert(0==mySystem(path,cmd.str().c_str()));
		cmd.str(std::string()); cmd.clear();
		cmd << "grep -q \"" << username << "\" LEDGER.txt || echo \"" << username << ": 1\" >> LEDGER.txt";
		assert(0==mySystem(path,cmd.str().c_str()));
		run(path,"git add LEDGER.txt");
	}

	/// Get difficulty
	std::string getDifficulty() {
		std::ifstream f(path + "/difficulty.txt");
		std::string d; f>>d; 
		return d;
	}

	/// Build commit text
	std::vector<char> buildCommit() {
		std::string tree=run(path,"git write-tree");
		std::string parent=run(path,"git rev-parse HEAD");
		std::string timestamp="0";//run("date +%s");

		std::string commit= 	"tree " + tree + "\n" +
				"parent " + parent + "\n" +
				"author a <a@b.de> " + timestamp + " +0000\n" +
				"committer a <a@b.de> " + timestamp + " +0000\n" +
				"\n        ";
		for (; commit.size()%8!=0; commit+= " ");

		std::stringstream head;
		head << "commit ";
		head << commit.length();
		std::vector<char> buffer;
		buffer.resize(4096);
		memcpy(buffer.data(),head.str().c_str(),head.str().length());
		buffer[head.str().length()] = 0;
		memcpy(buffer.data()+head.str().length()+1,commit.c_str(),commit.length());
		auto len=head.str().length()+1+commit.length();
		buffer.resize(len);
		return buffer;
	}
};




struct MonitoringThread {
	WorkingDir wd;
	std::atomic<int64_t> resetAll;

	/// Constructor
	MonitoringThread(std::string repository) : wd(repository,"wdmonitor"),resetAll(0) {}

	/// Thread body
	void operator()() {
		while (1) {
			if (wd.upstreamChanged()) {
				std::cout << "Reset" << std::endl;
				resetAll=ThreadCount;
				while(resetAll > 0) usleep(1);
			}
		}
	}
};




int main(int argc, char *argv[]) {
	srandom(time(NULL));

	assert(argc == 3);
	std::string repository(argv[1]);
	std::string username(argv[2]);

	// Start monitor
	MonitoringThread mon(repository);
	std::thread monThread(std::ref(mon));

	std::vector<std::thread> threads;
	std::vector<int64_t> seeds;
	for (unsigned i=0; i<ThreadCount; ++i) { seeds.push_back(random()); }

	for (unsigned i=0; i<ThreadCount; ++i) {
		threads.emplace_back([&mon,&seeds,repository,username,i]() {
			// Establish working directory
			std::stringstream str; str << "wd" << i << random();
			std::string wdStr = str.str();
			WorkingDir wd(repository,wdStr,true);
			std::cout << str.str() << std::endl;

			while (1) {
				// Retrieve difficulty
				auto difficulty=wd.getDifficulty();
				Sha1Digest difficultyDigest; difficultyDigest.fromHexString(difficulty.c_str(),difficulty.length());

				// Build commit structure
				wd.fixLedger(username);
				auto commitBuffer=wd.buildCommit();

				// Calculate header length
				int64_t headLen;
				{	
					char* ptr=commitBuffer.data();
					for (; (*ptr)!=0; ++ptr); ++ptr;
					headLen=ptr-commitBuffer.data();
				}

				// Find a good position for a counter
				auto ptr = reinterpret_cast<uintptr_t>(commitBuffer.data()+commitBuffer.size());
				ptr = ((ptr/8)*8)-8;
				int64_t& counter=*reinterpret_cast<int64_t*>(ptr);
				counter=seeds[i];
				int64_t staticLen = ((char*)&counter) - (char*)commitBuffer.data();
				int64_t variableLen = (commitBuffer.data()+commitBuffer.size()) - ((char*)&counter);
				assert(staticLen + variableLen == (int)commitBuffer.size());

				// Start the digest
				UpdatableSha1Digest ud;
				ud.setFixed(commitBuffer.data(),staticLen);
				assert((void*)(commitBuffer.data()+staticLen) == (void*)&counter);
				
				Timer t;
				int64_t lastCounter=counter;
				while (mon.resetAll == 0) {
					++counter;
					ud.setVariable(commitBuffer.data()+staticLen,variableLen);
					if (ud < difficultyDigest) {
						std::cout << "Found something in " << str.str() << std::endl;
						std::cout << ud.toHexString() << std::endl;
						wd.commit(commitBuffer.data() + headLen, commitBuffer.size()-headLen);
						break;
					}

					if (0&&(counter & ((1<<21)-1)) == 0) {
						std::cerr << "Hashrate: " << (((counter-lastCounter)*1000000)/t.getMicro()) << " sha1/s" << std::endl;
						lastCounter=counter;
						t.reset();
					}
				}
				--mon.resetAll;
				wd.reset();
			}
		});
	}

	for (auto& t : threads) t.join();

	monThread.join();
}


/*
    # Brute force until you find something that's lexicographically
    # small than $difficulty.
    difficulty=$(cat difficulty.txt)

    # Create a Git tree object reflecting our current working
    # directory
    tree=$(git write-tree)
    parent=$(git rev-parse HEAD)
    timestamp=$(date +%s)

    counter=0

    while let counter=counter+1; do
	echo -n .

	body="tree $tree
parent $parent
author CTF user <me@example.com> $timestamp +0000
committer CTF user <me@example.com> $timestamp +0000


$counter"

	# See http://git-scm.com/book/en/Git-Internals-Git-Objects for
	# details on Git objects.
	sha1=$(git hash-object -t commit --stdin <<< "$body")

	if [ "$sha1" "<" "$difficulty" ]; then
	    echo
	    echo "Mined a Gitcoin with commit: $sha1"
	    git hash-object -t commit --stdin -w <<< "$body"  > /dev/null
*/