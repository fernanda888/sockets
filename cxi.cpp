// $Id: cxi.cpp,v 1.1 2020-11-22 16:51:43-08 - - $

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"ls"  , cxi_command::LS  },
   {"get" , cxi_command::GET },
   {"put" , cxi_command::PUT },
   {"rm"  , cxi_command::RM  },
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cxi_help() {
   cout << help;
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   if (header.command != cxi_command::LSOUT) {
      outlog << "ls error" << endl;
   }else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
   }
}

void cxi_get(client_socket& server, string filename){
  cxi_header header;
  header.command = cxi_command::GET;
  strcpy(header.filename, filename.c_str());
  send_packet (server, &header, sizeof header);
  recv_packet (server, &header, sizeof header);
  if (header.command != cxi_command::FILEOUT) {
    outlog << "get error" << ": " << strerror (errno) << endl;
   }
   else {
    size_t host_nbytes = ntohl (header.nbytes);
    auto buffer = make_unique<char[]> (host_nbytes+1);
    recv_packet (server, buffer.get(), host_nbytes);
    buffer[host_nbytes] = '\0';
    ofstream outfile(filename,ofstream::binary);
    outfile.write (buffer.get(),(host_nbytes));
   }
}
void cxi_put(client_socket& server, string filename){
  cxi_header header;
  string put_output;
  strcpy(header.filename, filename.c_str());
  ifstream is(filename,ifstream::binary);
  if(is){
    is.seekg (0, is.end);
    int length = is.tellg();
    is.seekg (0, is.beg);
    char * buffer = new char [length];
    is.read(buffer,length);
    is.close();
    put_output.append(buffer);
    header.command=cxi_command::PUT;
    header.nbytes= htonl(put_output.size());
    send_packet (server, &header, sizeof header);
    send_packet (server, put_output.c_str(), put_output.size());
    delete[] buffer;
    memset (header.filename,0, FILENAME_SIZE);
    recv_packet(server, &header, sizeof header);
    if(header.command!=cxi_command::ACK){
      header.command = cxi_command::NAK;
      outlog << "put error" << ": " << strerror (errno) << endl;
      header.nbytes = htonl (errno);
      send_packet (server, &header, sizeof header);
      return;
    }
  }
  else{
      outlog << "put error" << ": " << strerror (errno) << endl;
  }
}
void cxi_rm(client_socket& server, string filename){
  cxi_header header;
  header.command = cxi_command::RM;
  strcpy(header.filename, filename.c_str());
  send_packet (server, &header, sizeof header);
  recv_packet (server, &header, sizeof header);
  if (header.command != cxi_command::ACK) {
      outlog << "rm error" << ": no such file or directory" << endl;
   }
}

void usage() {
   cerr << "Usage: " << outlog.execname() << " [host] [port]" << endl;
   throw cxi_exit();
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cxi_server_host (args, 0);
   in_port_t port = get_cxi_server_port (args, 1);
//   outlog << to_string (hostinfo()) << endl;
   try {
//      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
//      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         string filename;
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();
         if(line.find(' ')!=string::npos){
          filename=line.substr(line.find(' ')+1);
          line.erase(line.find(' ')); 
         }
         const auto& itor = command_map.find (line);
         cxi_command cmd = itor == command_map.end()
                         ? cxi_command::ERROR : itor->second;
         switch (cmd) {
            case cxi_command::EXIT:
               throw cxi_exit();
               break;
            case cxi_command::HELP:
               cxi_help();
               break;
            case cxi_command::LS:
               cxi_ls (server);
               break;
            case cxi_command::GET:
               cxi_get (server, filename);
               break;
            case cxi_command::PUT:
               cxi_put (server, filename);
               break;
            case cxi_command::RM:
               cxi_rm (server, filename);
               break;
            default:
               outlog << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      
   }
   return 0;
}

