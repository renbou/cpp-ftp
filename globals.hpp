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
// ftp LIST -a . and ..
const std::string listVerbose = "drwxr-xr-x 0b ."+CRLF+"drwxr-xr-x 0b .."+CRLF;
const dataT listVerboseData(listVerbose.begin(), listVerbose.end());
// hashmap type for user:pass
typedef std::unordered_map<std::string, std::string> stringHashMap;
// byte type
typedef unsigned char byte;
// type of return string from command
typedef std::pair<int, std::string> response;

// help command answers for available commands
std::vector<std::pair<std::string, std::string>> commandHelp = {
	{"HELP", "Prints the help message in multiline response"},
	{"USER [username]", "Tries to begin authentication with specified username. Must be followed by PASS"},
	{"PASS [password]", "Tries to authenticate using password, must be preceded by USER"},
	{"REIN", "Logs out the user, you can login with a different user"},
	{"QUIT", "Stops the control connection, disconnecting you from the server"},
	{"TYPE [TYPE]", "Specifies the type of data for transfer. Available: A - Ascii, I - Binary data. Doesn't matter, TYPE command is obsolete"},
	{"MODE [MODE]", "Specifies the mode of data transfer. Available: S - stream (simply sends data to the data connection and then closes)"},
	{"STRU [STRUCTURE]", "Specifies the structure of data transfer. Available: F - file (no structure). Obsolete command, but required by standard."},
	{"SYST", "Returns the system on which the FTP server is running"},
	{"PASV", "Initializes passive connection and returns the ip and port. You shouldn't use the returned IP and should instead use the main servers's IP address for data connections."},
	{"PORT [ip1, ip2, ip3, ip4, port1, port2]", "Specifies the address and port for an active data connection"},
	{"PWD", "Prints the current directory"},
	{"CWD [PATH]", "Changes the current directory to the specified one"},
	{"CDUP", "Tries to change current directory to parent directory"},
	{"MKD [PATH]", "Makes directory (and all intermediate and non-existent directories)"},
	{"LIST [PATH/-a/-al]", "Tries to list the directories contents on PATH (or current directory if path not specified) to the data connection. If -a or -al is specified instead of path, the LIST command also lists hidden files."},
	{"STOR [FILENAME]", "Tries to receive data from the data connection and stores them to the specified file/path"},
	{"RETR [FILENAME]", "Tries to send requested file to data connection"},
	{"NOOP", "No operation, just to test connection"}
};

#endif //CPP_FTP_GLOBALS_HPP
