// $Id: cxid.cpp,v 1.2 2020-11-29 12:38:28-08 - - $

#include <iostream>
#include <string>
#include <vector>
#include <fstream> 
#include <memory>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

void reply_ls (accepted_socket& client_sock, cxi_header& header) {
   const char* ls_cmd = "ls -l 2>&1";
   FILE* ls_pipe = popen (ls_cmd, "r");
   if (ls_pipe == NULL) { 
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
      return;
   }
   string ls_output;
   char buffer[0x1000];
   for (;;) {
      char* rc = fgets (buffer, sizeof buffer, ls_pipe);
      if (rc == nullptr) break;
      ls_output.append (buffer);
   }
   pclose (ls_pipe);
   header.command = cxi_command::LSOUT;
   header.nbytes = htonl (ls_output.size());
   memset (header.filename, 0, FILENAME_SIZE);
   send_packet (client_sock, &header, sizeof header);
   send_packet (client_sock, ls_output.c_str(), ls_output.size());
}
void reply_get(accepted_socket& client_sock, cxi_header& header){
  string get_output;
  header.command=cxi_command::GET;
  ifstream is(header.filename,ifstream::binary);
  if(is){
    is.seekg (0, is.end);
    int length = is.tellg();
    is.seekg (0, is.beg);
    char * buffer = new char [length];
    is.read(buffer,length);
    is.close();
    get_output.append(buffer);
    header.command=cxi_command::FILEOUT;
    header.nbytes= htonl(get_output.size());
    memset (header.filename, 0, FILENAME_SIZE);
    send_packet (client_sock, &header, sizeof header);
    send_packet (client_sock, get_output.c_str(), get_output.size());
    delete[] buffer;
  }
  else{
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
      return;
  }
}
void reply_put(accepted_socket& client_sock, cxi_header& header){
  if (header.command != cxi_command::PUT) {
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
   }
   else {
    size_t host_nbytes = ntohl (header.nbytes);
    auto buffer = make_unique<char[]> (host_nbytes + 1);
    recv_packet (client_sock, buffer.get(), host_nbytes);
    buffer[host_nbytes] = '\0';
    ofstream outfile(header.filename,ofstream::binary);
    outfile.write (buffer.get(),(host_nbytes));
    header.command=cxi_command::ACK;
    send_packet (client_sock, &header, sizeof header);
   }
}
void reply_rm(accepted_socket& client_sock, cxi_header& header){
  ifstream is(header.filename,ifstream::binary);
  if(is){
    unlink(header.filename);
    header.command=cxi_command::ACK;
    memset (header.filename,0, FILENAME_SIZE);
    send_packet (client_sock, &header, sizeof header);
  }
  else{
    header.command = cxi_command::NAK;
    header.nbytes = htonl (errno);
    send_packet (client_sock, &header, sizeof header);
    return;
  }
}

void run_server (accepted_socket& client_sock) {
   outlog.execname (outlog.execname() + "-server");
   try {   
      for (;;) {
         cxi_header header; 
         recv_packet (client_sock, &header, sizeof header);
         switch (header.command) {
            case cxi_command::LS: 
               reply_ls (client_sock, header);
               break;
            case cxi_command::GET: 
               reply_get (client_sock, header);
               break;
            case cxi_command::PUT: 
               reply_put (client_sock, header);
               break;
            case cxi_command::RM: 
               reply_rm (client_sock, header);
               break;
            default:
               outlog << "invalid client header:" << header << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
   }
   throw cxi_exit();
}

void fork_cxiserver (server_socket& server, accepted_socket& accept) {
   pid_t pid = fork();
   if (pid == 0) { // child
      server.close();
      run_server (accept);
      throw cxi_exit();
   }else {
      accept.close();
      if (pid < 0) {
         outlog << "fork failed: " << strerror (errno) << endl;
      }
   }
}


void reap_zombies() {
   for (;;) {
      int status;
      pid_t child = waitpid (-1, &status, WNOHANG);
      if (child <= 0) break;
   }
}

void signal_handler (int signal) {
   outlog << "signal_handler: caught " << strsignal (signal) << endl;
   reap_zombies();
}

void signal_action (int signal, void (*handler) (int)) {
   struct sigaction action;
   action.sa_handler = handler;
   sigfillset (&action.sa_mask);
   action.sa_flags = 0;
   int rc = sigaction (signal, &action, nullptr);
   if (rc < 0) outlog << "sigaction " << strsignal (signal)
                      << " failed: " << strerror (errno) << endl;
}


int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   vector<string> args (&argv[1], &argv[argc]);
   signal_action (SIGCHLD, signal_handler);
   in_port_t port = get_cxi_server_port (args, 0);
   try {
      server_socket listener (port);
      for (;;) {
         accepted_socket client_sock;
         for (;;) {
            try {
               listener.accept (client_sock);
               break;
            }catch (socket_sys_error& error) {
               switch (error.sys_errno) {
                  case EINTR:
                    outlog << "listener.accept caught "
                         << strerror (EINTR) << endl;
                     break;
                  default:
                     throw;
               }
            }
         }
         try {
            fork_cxiserver (listener, client_sock);
            reap_zombies();
         }catch (socket_error& error) {
            outlog << error.what() << endl;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
   }
   return 0;
}

