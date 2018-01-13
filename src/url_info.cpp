#include "url_info.h"
#include <string>
#include <regex>
#include <vector>
URLInfo::URLInfo(std::string url)
{
	this->url = url;
	dl_info.port = 0;
	smatch m;
	regex *e = new regex("((http://|ftp://|https://)|())(.*?)(/|:(.+?)/)");
	bool found;
	found = regex_search(url, m, *e);
	delete e;
	if(found){
		dl_info.host_name = m[4];
		if(m[6].length() >0)
			dl_info.port = stoi(m[6]);
		dl_info.file_path_on_server = '/'+m.suffix().str();
	}
	e = new regex("(.*?/.+/)(.*)");
	found = regex_search(url, m, *e);
	delete e;
	if(found){
		dl_info.file_name_on_server = m[2];
	}
	if(!dl_info.port){
		e = new regex("http:|ftp:");
		found = regex_search(url, m, *e);
		delete e;
		if(found){
			if(m[0].str()=="http:" || m[0].str()=="")
				dl_info.port = 80;
		}
	}
	struct hostent *server;
	server = gethostbyname(dl_info.host_name.c_str());
	if (server == NULL) {
		fprintf(stderr,"ERROR, no such host\n");
		exit(0);
	}
	dl_info.ip = string(inet_ntoa(*((struct in_addr*) server->h_addr)));




	e = new regex("%20");
	dl_info.file_name_on_server = regex_replace(dl_info.file_name_on_server,*e," ");
	delete e;
}
addr_info URLInfo::get_download_info(){
	return dl_info;
}
