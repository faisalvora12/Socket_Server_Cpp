#include <sys/types.h>
#include <experimental/filesystem>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include "misc.hh"
#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <tuple>
#include <thread>
#include <mutex>
#include "server.hh"
#include "http_messages.hh"
#include "errors.hh"
#include "misc.hh"
#include "routes.hh"
#include "string.h"
namespace fs = std::experimental::filesystem;
using namespace std;
mutex m;
Server::Server(SocketAcceptor const& acceptor) : _acceptor(acceptor) { }

void Server::run_linear() const {
	while (1) {
		Socket_t sock = _acceptor.accept_connection();
		handle(sock);
	}
}
void Server::run_fork() const {
	while (1) {
		Socket_t sock = _acceptor.accept_connection();
		int back=fork();
		if ( back != 0 ) {
			handle(sock);
		}
	}
}

void Server::run_thread() const {
	while(1){
		Socket_t sock = _acceptor.accept_connection();
		std::thread thread1(&Server::handle,this,std::move(sock));
		thread1.detach();

	}
}
void Server::loop()const{
	while(1){
		m.lock();
		Socket_t sock = _acceptor.accept_connection();
		m.unlock();
		handle(sock);
	}
}
void Server::run_thread_pool(const int num_threads) const {
	//	while(1){
	//	Socket_t sock = _acceptor.accept_connection();

	for(int i=0;i<num_threads;i++)
	{
		std::thread thread1(&Server::loop,this);
		thread1.detach();
	}
	loop();	
	//	}
}

// example route map. you could loop through these routes and find the first route which
// matches the prefix and call the corresponding handler. You are free to implement
// the different routes however you please
/*
   std::vector<Route_t> route_map = {
   std::make_pair("/cgi-bin", handle_cgi_bin),
   std::make_pair("/", handle_htdocs),
   std::make_pair("", handle_default)
   };
 */
string content="";
int parse_request(const Socket_t& sock, HttpRequest* const request)
{

	string auth;
	string request_line=sock->readline();
	if(request_line.length()==0)
		return 200;
	string request_line1=request_line;
	string request_full=request_line;
	int found=0;
	string userpass="";
	while(request_line1.compare("\r\n")!=0)
	{
		request_line1=sock->readline();	
		request_full+=request_line1+"\n";
		if(request_line1.find("Authorization") != std::string::npos)
		{
			//std::cout<<"TEST AUTH:  "<<request_line1<<std::endl;
			stringstream ss(request_line1);
			//			ss << request_line1;
			for(int i=0;i<3;i++)
			{
				ss >> auth;
			}
			found=1;
		}
	}
	int pos=0;
	string met="";
	int space=0;
	int fslash=0;
	string url="";
	string version="";
	string fullscript="";
	while(pos<request_line.size()-1)	
	{
		if(request_line[pos]==' ')space=1;
		if(request_line[pos]=='/' && fslash==0){fslash=1;}
		if(space!=1 && fslash==0)
			met=met+request_line[pos];
		if(fslash==1)
		{	
			if(request_line[pos]!=' ')
				url+=request_line[pos];
			else{ 
				fslash=2;pos++;}
		}
		if(fslash==2)
		{
			//	pos++;
			version+=request_line[pos];
		}

		pos++;
	}
	//cout<<"\n\n\n\n\n"<<request_full<<"\n\n\n\n\n\n";
	//	cout<<"          " <<url<<"               "<<endl;
	request->method=met;
	request->request_uri=url;
	request->http_version=version;	
	string script="";
	string var="";
	if(url.find("cgi-bin")!= std::string::npos)
	{
		pos=1;int question=0;fslash=0;
		while(pos < url.size())
		{
			if(question==0 && url[pos]!='?')
				fullscript+=url[pos];

			if(url[pos]=='/'){fslash=1;pos++;fullscript+=url[pos];}

			if(url[pos]=='?')
			{
				fslash=2;
				question=1;pos++;

			}
			if(fslash==1 && question==0)
			{
				script=script+url[pos];
			}
			if(question==1)
			{
				var=var+url[pos];	
			}
			pos++;
		}
		//cout<<"          " <<url<<"               "<<endl;
		//	cout<<" \n\n\n\n\n   "<<cgis<<"  \n\n\n\n\n"<<endl;
		//      cout<<"     \n\n\n\n      "<<script<<"  \n\n\n\n\n"<<endl;
		//	cout<<"  \n\n\n\n\n\n "<<var<<" \n\n\n\n\n\n\n      "<<endl;	
		//	std::string response;
		int pipe_fd[2];
		if (pipe(pipe_fd) == -1) {
			perror("get_content_type pipe error");
			exit(-1);
		}
		int pid = fork();

		if (pid == -1) {
			perror("get_content_type fork error");
			exit(-1);
		}
		if (pid == 0) {
			close(pipe_fd[0]);  // close read end
			dup2(pipe_fd[1], STDOUT_FILENO);
			dup2(pipe_fd[1], STDOUT_FILENO);
			close(pipe_fd[1]);
			setenv("REQUEST_METHOD","GET",1);
			setenv("QUERY_STRING",var.c_str(),1);

			execl(("http-root-dir/"+fullscript).c_str(), script.c_str(), NULL);
			perror("get_content_type execl error");
			exit(-1);
		} else {
			close(pipe_fd[1]);  // close write end

			char buf;
			int b=0;
			while (read(pipe_fd[0], &buf, 1) > 0) {
				if(buf=='\n' && b==0)
					b=1;
				if(b==1)
					content += buf;
			}

			//return 200;
			close(pipe_fd[0]);return 200;  // close read end
		}
	}
	else {

		string file;

		if(found==0)
		{
			return 401;		
		}
		else{	


			string line;
			ifstream myfile ("credentials64.txt");

			if (myfile.is_open())
			{
				getline (myfile,line);

				//	cout << "\n\n\n\n"<<line<< "\n\n\n\n"<<auth<<"\n\n\n";
				if(auth.compare(line)!=0)
				{
					return 401;
				}
				else 
				{
					string line2;
					if(url=="/")
					{	
						file="http-root-dir/htdocs/index.html";}

					else if(url.find("/dir1/") != std::string::npos)
					{
						string newurl="http-root-dir/htdocs/dir1";
						DIR * d = opendir(newurl.c_str());
						if (NULL == d) {
							perror("opendir: ");

							exit(1);
						}
						string subdir="\n<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n<html>\n<head>\n<title>CS422: HTTP Server</title>\n</head>\n<body>\n<h1>CS252: HTTP Server</h1>\n<ul>\n";
string mid="";

 string sub2="</ul>\n<hr>\n<address><a href=\"mailto:grr@cs.purdue.edu\">Gustavo Rodriguez-Rivera</$\n<!-- Created: Fri Dec  5 13:12:48 EST 1997 -->\n<!-- hhmts start -->\nLast modified: Fri Dec  5 13:14:47 EST 1997\n<!-- hhmts end -->\n</body>\n</html>\n";
						for (dirent * ent = readdir(d); NULL != ent; ent = readdir(d)) {
						mid=mid+"<li><A HREF=\""+ent->d_name+"\">"+ent->d_name+"</A>\n";	
						}
		
						string combine=subdir+mid+sub2;
						//cout<<combine<<endl;
						content=combine;
						closedir(d);	
						
						return 200;

					}
					else {
						file="http-root-dir/htdocs"+url;}
					//	cout<<"\n\n\n\n "<<request->request_uri<<"   "<<file<<"\n\n\n\n";
					if(url.find("/dir") != std::string::npos)
						file="http-root-dir/htdocs/dir1/index.html";
					string line;
					ifstream file2(file);

					if ( file2 )
					{
						std::stringstream buffer;

						buffer << file2.rdbuf();

						file2.close();
						content=buffer.str();
						return 200;
					}

					else 
					{
						return 404;
					}	



				}		
				myfile.close();
			}	
			else 
			{
				return 404;
			}	
			return 200;
		}
	}
}
void Server::handle(const Socket_t& sock) const {
	HttpRequest request;

	// TODO: implement parsing HTTP requests
	// recommendation:
	int s_code=parse_request(sock ,&request);


	HttpResponse resp;
	// TODO: Make a response for the HTTP request

	resp.message_body=content;
	//cout<<content<<endl;
	content="";
	resp.http_version = "HTTP/1.1";
	//if(request.length()==0)
	//resp.status_code=200;

	resp.status_code = s_code;
	//std::cout << resp.to_string() << std::endl;
	//std::cout << resp.to_string() << std::endl;
	sock->write(resp.to_string());
}
