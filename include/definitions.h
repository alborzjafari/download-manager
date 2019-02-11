#ifndef _DEFINITIONS_H
#define _DEFINITIONS_H
#include <iostream>
#include <mutex>
typedef struct{
	int port;
	std::string ip;
	std::string file_path_on_server;
	std::string file_name_on_server;
	std::string host_name;
	bool encrypted;
	int protocol;		// Enum protocol type
}addr_struct;

enum protocol_type {
	kHttp,
	kFtp,
};

typedef struct {
	FILE* fp;
	FILE* log_fp;
	void* node;
	bool resuming;
	std::string log_buffer_str;
	std::string file_name;
	std::mutex* file_mutex;
}node_struct;

constexpr size_t CHUNK_SIZE = 256 * 1024;

#define LOG cerr << "FILE"<<__FILE__<<" Line:"<<__LINE__<<std::endl;

#endif
