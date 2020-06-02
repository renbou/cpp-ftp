#ifndef CPP_FTP_GLOBALS_HPP
#define CPP_FTP_GLOBALS_HPP

#include <sockpp/socket.h>
#include <string>
#include <utility>

// server version
const std::string serverVersion("v0.1");
// default listen port for server
const in_port_t defaultPort = 2020;
// telnet end-of-line
const std::string CRLF = "\r\n";
const std::pair<unsigned char, unsigned char> CRLFp = {'\r', '\n'};
// list of users and passwords
const std::string defaultUserFile = "users.txt";
// the working directory for logged in users
const std::string defaultWorkdir = "myftpserver";
// the default size of a buffer
// large so that the reads are fast
const uint32_t BUFSIZE = (1 << 16);
// data type for sending bytes
typedef std::vector<unsigned char> dataT;
// hashmap type for user:pass
typedef std::unordered_map<std::string, std::string> stringHashMap;
// byte type
typedef unsigned char byte;
// type of return string from command
typedef std::pair<int, std::string> response;

#endif //CPP_FTP_GLOBALS_HPP
