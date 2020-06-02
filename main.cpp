#include <iostream>
#include <fstream>
#include <thread>
#include <sockpp/socket.h>
#include <sockpp/tcp_acceptor.h>
#include <unordered_map>
#include <string>
#include <functional>
#include <utility>
#include <algorithm>

// header with various global variables
#include "globals.hpp"
// header with function for parsing arguments
#include "argparse.hpp"
// header with buffer for reading line-by-line CRLF
#include "netbuffer.hpp"
// header with util logger class as well as helper functions and helper filesystem library
#include "utils.hpp"
// header with the main ftp structure and functions related to sending data over ftp and handling ftp commands
// rfc 959 compliant
#include "ftp.hpp"

const std::unordered_map<std::string,
                   std::function<response(FTP&, const std::string)>>
                   funcMap = {{"USER", userFTP}, {"PASS", passFTP}, {"REIN", reinFTP}, {"QUIT", quitFTP},
							  {"TYPE", typeFTP}, {"MODE", modeFTP}, {"STRU", struFTP},
							  {"PASV", pasvFTP}, {"PORT", portFTP},
							  {"PWD", pwdFTP}, {"CWD", cwdFTP}, {"CDUP", cdupFTP}, {"MKD", mkdFTP}, {"LIST", listFTP}};


void runFtpPI(stringHashMap users_t, sockpp::tcp_socket sock, sockpp::inet_address peer, fs::path workdir, loggerT& logger) {
	FTP ftp(users_t, std::move(sock), peer, workdir, logger);
	// send 220 code since we are ready for working
	sendReply(ftp, 220, "Ready for service, waiting for authorization");

	// wait for commands from user
	do {
		const dataT buf = readline(ftp.controlSock, ftp.ftpBuf);
		// if an error happened during reading
		if (buf.empty()) {
			sendReply(ftp, 500, "Invalid command (too long or can't read command)");
			continue;
		}
		// non ascii printable characters in command
		if (std::find_if(buf.begin(), buf.end(), [&](byte val){ return val < 0x20 or val > 0x7f; }) != buf.end()) {
			sendReply(ftp, 500, "Invalid chars in command");
			continue;
		}
		// convert safe buffer to string
		std::string cmdString(buf.begin(), buf.end());
		auto [command, params] = getNextParam(cmdString);
		// convert the string to uppercase
		std::transform(command.begin(), command.end(), command.begin(), toupper);

		// if we need to just quit right now, then lets just break the loop
		// first check is just an optimization
		if (command[0] == 'X' and command == "XQUITNOW") {
			shutdownError(ftp, "Bad error during trying to receive command");
			break;
		}

		// find the corresponding function in the hashmap
		auto commandFunction = funcMap.find(command);
		// check if we received an invalid command
		if (commandFunction == funcMap.end()) {
			sendReply(ftp, 502, "Command unknown or not implemented");
			ftp.prevCommand = command;
			continue;
		}
		// execute the command
		auto [responseCode, responseString] = commandFunction->second(ftp, params);
		ftp.prevCommand = command;
		// send the reply
		sendReply(ftp, responseCode, responseString);

	} while (ftp.controlSock.is_open() and ftp.active);
}


int main(int argc, char* argv[]) {
	// initialize sockpp library
	const sockpp::socket_initializer sockInit;
	std::cout << "Baseline FTP server " << serverVersion << std::endl;

	// parse the arguments
	const auto [serverPort, logFileName, dirPath, needToClose] = parseArgs(argc, const_cast<const char **>(argv));

	if (needToClose)
		return 0;

	// create the logger
	loggerT logger = loggerT(logFileName);

	// sockpp-based ftp server
	logger << "Listening on port " << serverPort << ENDL;
	sockpp::tcp_acceptor ftpServer(serverPort);

	// couldn't create the server for some reason, have to quit
	if (not ftpServer) {
		std::cerr << "ERROR! creating the acceptor: " << ftpServer.last_error_str() << std::endl;
		return 1;
	}

	// get the list of valid users
	const stringHashMap users = [&]() -> stringHashMap {
		std::ifstream userFile(defaultUserFile);
		// no file with usernames and passwords
		if (not userFile.is_open()) {
			std::cerr << "ERROR! no user file \"" << defaultUserFile << "\" with the list of valid users and passwords." <<
						 std::endl << "Put this file in the same folder as the executable." << std::endl <<
						 "The format is username:password." << std::endl;
			return {};
		}
		stringHashMap result;
		while (not userFile.eof()) {
			std::string cur;
			userFile >> cur;
			// location of ':' separator in string
			auto location = cur.find(':');
			result.insert({cur.substr(0, location), cur.substr(location + 1)});
		}
		return result;
	}();

	// if the server root directory isn't created, make it
	fs::path workDirectory(dirPath);
	if (not fs::is_directory(workDirectory))
		fs::create_directory(workDirectory);
	workDirectory = fs::weakly_canonical(workDirectory);
	workDirectory = fs::absolute(workDirectory);

	logger << "Server root is at " << workDirectory.generic_string() << ENDL;

	// try to execute the main loop of ftp server listener
	try {
		while (true) {
			sockpp::inet_address peer;

			// accept a new client connection
			sockpp::tcp_socket sock = ftpServer.accept(&peer);

			if (!sock) {
				logger << "Error accepting incoming connection from" << peer.to_string() << ": " <<
						  ftpServer.last_error_str() << ENDL;
			} else {
				logger << "Received a connection request from " << peer.to_string() << ENDL;
				// create a thread and transfer the new stream to it
				// this is so we handle the user traffic in a different thread
				// and we can talk to multiple users at the same time

				std::thread thr(runFtpPI, users, std::move(sock), peer, workDirectory, std::ref(logger));
				thr.detach();

				// for testing
				//runFtpPI(users, std::move(sock), peer, workDirectory, std::ref(logger));
			}
		}
	} catch (std::exception &e) {
		std::cerr << "ERROR! In main FTP server accept loop: " << e.what() << std::endl;
	}

	// close the logger file before exiting
	logger.close();
	return 0;
}