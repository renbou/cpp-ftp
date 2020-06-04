#ifndef CPP_FTP_FTP_HPP
#define CPP_FTP_FTP_HPP

#include <sockpp/tcp_socket.h>
#include <sockpp/tcp_acceptor.h>
#include <sockpp/tcp_connector.h>
#include <sockpp/inet_address.h>
#include <algorithm>
#include <fstream>
#include "globals.hpp"
#include "utils.hpp"
#include "netbuffer.hpp"
#include "ftptransfer.h"

// ftp structure for holding the connections and the state of the ftp control connection
struct FTP {
	// control connection and data connection as specified in rfc 959
	sockpp::tcp_socket controlSock;
	// we need to store the passive socket separately cause we need to listen and accept on it multiple times
	// after we accepted we just write the socket to dataSocket
	sockpp::tcp_acceptor pasvSock;
	// if we are using an active data connection (server connects to client)
	// then we use tcp_connector and then store the socket here
	sockpp::tcp_socket dataSocket;
	// we need to store the control connection peer for logging
	// and data socket peer for error logging
	sockpp::inet_address peer, dataSockAddr;
	// logger class
	// if we don't have logging to file enabled then we don't need to worry
	// because logger handles it on its own
	loggerT &logger;
	// server root directory path and current path
	fs::path serverRoot, workDir, curDir;
	// we store the valid users in this map so we can authenticate easily
	stringHashMap &users;
	// the buffer of the ftp control socket
	netbuffer ftpBuf;

	// set active to false and the server quits
	bool passiveMode = false, active = true;
	// store the user here for auth check
	std::pair<std::string, std::string> user {};
	// needed to check previous received command to validate the order
	std::string prevCommand {};

	// ftp data transfer formatting
	// we only support ascii-nonprint and image(binary), everything else is obsolete
	enum FMTTYPE {ASCII_N, IMAGE} ftpFormatType = ASCII_N;
	// we don't support compressed mode or block mode
	enum FMTMODE {STREAM} ftpFormatMode = STREAM;
	// accept only file structure
	enum FTPSTRU {FILE} ftpFormatStru = FILE;


	// we use std::move to move unique_ptr type variables that can't be copied
	FTP(stringHashMap &users_t, sockpp::tcp_socket controlSock_t, sockpp::inet_address peer_t, fs::path workDir_t, loggerT &logger_t)
		: logger(logger_t), ftpBuf(), users(users_t) {
		controlSock = std::move(controlSock_t);
		curDir = workDir = workDir_t;
		serverRoot = workDir.parent_path();
		peer = peer_t;
	}
};

// helper function to get the peer of ftp connection
const std::string getPeer(FTP &ftp) {
	return "[" + ftp.peer.to_string() + "]";
}

// helper function which sends an error string and writes it to the logger
const bool shutdownError(FTP& ftp, std::string error) {
	const std::string shutdownErrorString = "421 Error - " + error + CRLF;
	ftp.logger << getPeer(ftp) << " - Have to shutdown the connection because of error - " <<
			   error << " " << ftp.controlSock.last_error_str() << ENDL;
	ftp.controlSock.write_n(shutdownErrorString.c_str(), shutdownErrorString.size());
	return true;
}

// helper function for sending simple c++ string replies
const bool sendString(FTP& ftp, std::string str) {
	if (ftp.controlSock.write_n(str.data(), str.size()) < str.size())
		return shutdownError(ftp, "error while sending string");
	return false;
}

// helper function for sending simple replies
const bool sendReply(FTP& ftp, uint32_t code, std::string str) {
	return sendString(ftp, std::to_string(code) + " " + str + CRLF);
}

// function to setup the data connection
const std::tuple<bool, int32_t, std::string> initDataConnection(FTP &ftp) {
	// if we have passive mode enabled
	if (ftp.passiveMode) {
		ftp.dataSocket = ftp.pasvSock.accept(&ftp.dataSockAddr);
		// can't connect
		if (not ftp.dataSocket) {
			ftp.logger << getPeer(ftp) << " - error accepting passive connection from " << ftp.dataSockAddr.to_string() <<
					   ": " << ftp.dataSocket.last_error_str() << ENDL;
			ftp.dataSocket.close();
			return {true, 425, "Error accepting connection"};
		}
	} else {
		sockpp::tcp_connector dataConnection(ftp.dataSockAddr);
		// can't connect
		if (not dataConnection) {
			ftp.logger << getPeer(ftp) << " - error making data connection to " << ftp.dataSockAddr.to_string() <<
					   ": " << dataConnection.last_error_str() << ENDL;
			dataConnection.close();
			return {true, 425, "Error making connection"};
		}
		ftp.dataSocket = std::move(dataConnection);
	}
	return {false, 225, "Data connection successfully established"};
}

// helper function to validate path
// tries to get the canonical path and then the absolute path
// and then checks if the path starts with the serverRoot path
// this is secure, we can't go out of our secure directory
const std::pair<fs::path, bool> getPath(FTP &ftp, std::string path) {
	// replace all backslashes
	std::replace(path.begin(), path.end(), '\\', '/');
	fs::path resultPath;
	// if path is not relative
	if (path[0] == '/')
		resultPath = ftp.serverRoot.generic_string() + path;
	else
		resultPath = ftp.curDir.generic_string() + "/" + path;
	resultPath = fs::weakly_canonical(resultPath);
	resultPath = fs::absolute(resultPath);
	// if the path doesn't begin with work directory then quit
	const std::string workDirStr = ftp.workDir.generic_string();
	const std::string resultDirStr = resultPath.generic_string();
	if ((workDirStr.size() == resultDirStr.size() and workDirStr == resultDirStr) or
		(workDirStr + "/" == resultDirStr.substr(0, workDirStr.size() + 1))) {
		return {resultPath, false};
	}
	return {{}, true};
}

// helper function to check if user is logged in
// we don't support anonymous FTP connection without a password
// because it isn't a necessity
const bool isAuthed(FTP& ftp) {
	return ftp.user.first != "" and ftp.user.second != "";
}

// ftp noop
// doesn't do anything
const response noopFTP(FTP &ftp, const std::string command) {
	return {200, "NOOP"};
}

// ftp help
// sends multiline reply of available commands and help message
const response helpFTP(FTP &ftp, const std::string command) {
	const auto [tmp1, tmp2] = getNextParam(command);
	if (tmp1 != "")
		return {502, "HELP command can't have any params"};
	sendString(ftp, "214-HELP message for server" + CRLF);
	sendString(ftp, "FTP server " + serverVersion + " based on RFC 959" + CRLF);
	for (auto message: commandHelp)
		sendString(ftp, message.first + " - " + message.second + CRLF);
	return {214, "HELP message for server"};
}

// function to handle USER
// USER [username] tries to begin authentication with the specified username
// if the username is invalid then the process must start again
// susceptible to username enumeration through bruteforce
const response userFTP(FTP &ftp, const std::string command) {
	// invalidate the user as specified in RFC 959
	ftp.user = {};
	const auto [username, leftover] = getNextParam(command);
	// if there is no username
	if (username == "")
		return {501, "Username not specified"};
	// if there are more params left
	if (leftover != "")
		return {501, "Excess parameters in command"};
	// invalid user
	if (ftp.users.find(username) == ftp.users.end())
		return {430, "Invalid username"};
	// set username and respond with "need password"
	ftp.user.first = username;
	return {331, "Need user password"};
}

// function to handle PASS
// PASS [password] tries to authenticate the user after the username has been specified with USER command
// if incorrect then the process must start again
// no bruteforce protection because that shouldn't be the worries of the server
const response passFTP(FTP &ftp, const std::string command) {
	// PASS must be preceded by USER, otherwise it's incorrect
	if (ftp.prevCommand != "USER") {
		ftp.user = {};
		return {503, "PASS command must be preceded by USER"};
	}
	// USER command should be successful
	if (ftp.user.first == "")
		return {530, "You should supply a valid username"};
	const auto [password, leftover] = getNextParam(command);
	// if the password isn't specified
	if (password == "") {
		ftp.user = {};
		return {501, "Password not supplied"};
	}
	// excess parameters
	if (leftover != "") {
		ftp.user = {};
		return {501, "Excess parameters in command"};
	}
	// invalid password has been supplied, must relogin
	if (ftp.users.find(ftp.user.first)->second != password) {
		ftp.user = {};
		return {430, "Invalid password supplied, relogin"};
	}
	// successful login
	ftp.user.second = password;
	ftp.logger << getPeer(ftp) << " - user logged in as " << ftp.user.first << ":" << ftp.user.second << ENDL;
	return {230, "Successfully authorized"};
}

// function to handle REIN
// REIN logs out the user, allowing a new user to login on the same control connection
const response reinFTP(FTP &ftp, const std::string command) {
	const auto [tmp1, tmp2] = getNextParam(command);
	if (tmp1 != "")
		return {501, "REIN can't have params"};
	ftp.logger << getPeer(ftp) << " - user \"" << ftp.user.first << "\" signed out" << ENDL;
	ftp.user = {};
	return {220, "Server ready for new user"};
}

// handle FTP quit
// QUIT just stops the control connection
const response quitFTP(FTP &ftp, const std::string command) {
	const auto [param1, leftover] = getNextParam(command);
	if (param1 != "" or leftover != "")
		return {501, "QUIT can't have any parameters"};
	ftp.active = false;
	ftp.logger << getPeer(ftp) << " - user \"" << ftp.user.first << "\" quit the session" << ENDL;
	return {221, "Successfully quit"};
}

// handle FTP pwd
// we create a fake filesystem where we are in /$workdir and can't go up
// PWD prints the current directory (starting from the server root)
const response pwdFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "PWD command requires an authenticated session"};
	const auto [param1, leftover] = getNextParam(command);
	if (param1 != "" or leftover != "")
		return {501, "PWD can't have any parameters"};
	// return current directory starting from server root
	return {257, ftp.curDir.generic_string().substr(ftp.serverRoot.generic_string().size())};
}

// handle FTP type
// ASCII and binary format are pretty much indifferent nowadays
// ASCII used to support different newline sequences but nowadays it doesn't matter
// so we don't need to convert CRLF to LF and vice-versa
const response typeFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "TYPE command requires an authenticated session"};
	const auto [type, leftover] = getNextParam(command);
	// we only support ascii and binary
	if (type != "A" and type != "I")
		return {504, "Server supports only ASCII non-printable and Image types"};
	if (type == "I") {
		if (leftover != "")
			return {501, "Image type may not have any extra params"};
		ftp.ftpFormatType = FTP::IMAGE;
		return {200, "Set type to Image"};
	}
	if (leftover != "") {
		const auto [asciitype, leftover_t] = getNextParam(leftover);
		// we only support non-printable
		if (asciitype != "N")
			return {504, "Server only supports non-printable Ascii"};
	}
	ftp.ftpFormatType = FTP::ASCII_N;
	return {200, "Set type to Ascii non-printable"};
}

// handle FTP mode
// we only support stream mode
// MODE [MODE]
const response modeFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "MODE command requires authenticated session"};
	const auto [mode, leftover] = getNextParam(command);
	if (mode != "S")
		return {504, "Server supports only Stream mode"};
	if (leftover != "")
		return {501, "MODE command can't have extra params"};
	ftp.ftpFormatMode = FTP::STREAM;
	return {200, "Set mode to stream"};
}

// handle FTP structure
// we don't support anything other than file
const response struFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "STRU command requires an authenticated sesson"};
	const auto [stru, leftover] = getNextParam(command);
	if (leftover != "")
		return {501, "STRU command can't have extra params"};
	if (stru != "F")
		return {504, "This server supports only File structure"};
	ftp.ftpFormatStru = FTP::FILE;
	return {200, "Set file structure to File (no record)"};
}

// handle FTP pasv
// PASV requests passive data connection
// the return is a code and the connection in the form ip1, ip2, ip3, ip4, port1, port2
// which the client transforms into ip1.ip2.ip3.ip4:port1*256+port
// the client must connect to the specified connection for data transfer commands
// YOU SHOULDN'T rely on the ip1.ip2.ip3.ip4 for connections and should use the server's actual IP
// because the server listens on ALL network interfaces
const response pasvFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "PASV command requires an authenticated session"};
	const auto [tmp1, tmp2] = getNextParam(command);
	if (tmp1 != "")
		return {501, "PASV command can't have any parameters"};
	// if we already have a socket open then close it
	if (ftp.pasvSock.is_open()) {
		ftp.pasvSock.shutdown();
		ftp.pasvSock.close();
	}
	// bind to any address and start listening
	ftp.pasvSock.open(sockpp::inet_address(0, 0));
	if (not ftp.pasvSock) {
		ftp.logger << getPeer(ftp) << " - cannot open a passive connection: " << ftp.pasvSock.last_error_str() << ENDL;
		return {425, "Error opening passive connection"};
	}
	ftp.dataSockAddr = ftp.pasvSock.address();
	const std::string passiveAddress = ftp.dataSockAddr.to_string();
	auto [ip, port] = [&]() -> std::pair<std::string, std::string>{
		int32_t loc = passiveAddress.find(':');
		return {passiveAddress.substr(0, loc), passiveAddress.substr(loc + 1)};
	}();
	std::replace(ip.begin(), ip.end(), '.', ',');
	const int32_t port_t = std::stoi(port);
	ftp.passiveMode = true;
	ftp.logger << getPeer(ftp) << " - started passive listening on " << ftp.dataSockAddr.to_string() << ENDL;
	return {227,  ip + "," + std::to_string(port_t / 256) + "," + std::to_string(port_t % 256)};
}

// handle FTP port
// PORT ip1, ip2, ip3, ip4, port1, port2
// specifies the active connection address as ip1.ip2.ip3.ip4:port1*256+port
// as specified in RFC 959
// the client must listen on this address for data connections
const response portFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "PORT command requires an authenticated session"};
	const auto [address, leftover] = getNextParam(command);
	// can't have leftover parameters in port
	if (leftover != "")
		return {501, "PORT command accepts only one argument"};
	// close passive connection if it is open
	if (ftp.pasvSock.is_open() || ftp.passiveMode) {
		ftp.passiveMode = false;
		ftp.pasvSock.shutdown();
		ftp.pasvSock.close();
	}

	auto tokens = splitByDelim(address, ",");
	// we must correctly check that there are 6 values specified and that they are all numbers
	// simply check by converting to number and back to string
	const auto checkInt = [](const std::string &value) -> std::string {
		return std::to_string(std::stoi(value));
	};
	if (tokens.size() != 6)
		return {501, "PORT command must be in form ip1, ip2, ip3, ip4, port1, port2. Check RFC 959"};
	try {
		ftp.dataSockAddr = sockpp::inet_address(checkInt(tokens[0]) + "." + checkInt(tokens[1]) + "."
			   									+ checkInt(tokens[2]) + "." + checkInt(tokens[3]),
												std::stoi(tokens[4]) * 256 + std::stoi(tokens[5]));
	} catch (std::exception &e) {
		return {501, "Invalid parameters for PORT command. Check RFC 959"};
	}
	ftp.logger << getPeer(ftp) << " - user initialized port - " << ftp.dataSockAddr.to_string() << ENDL;
	return {200, "Data connection port set successfully to " + ftp.dataSockAddr.to_string()};
}

// handle FTP cwd
// CWD [PATH] tries to change the working directory to PATH
// we don't actually cd anywhere, we just change the curDir variable in the class
const response cwdFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "CWD command requires an authenticated session"};
	auto [path, leftover] = getNextParam(command);
	if (leftover != "")
		return {501, "CWD command can't have extra params"};
	const auto [resPath, error] = getPath(ftp, path);
	if (error or not fs::exists(resPath))
		return {550, "Invalid path or no access"};
	ftp.curDir = resPath;
	return {200, "Successfully changed directory"};
}

// handle FTP cdup, just call cwd with .. parameter
// CDUP goes up one directory, but it's basically cwd ..
const response cdupFTP(FTP &ftp, const std::string command) {
	return cwdFTP(ftp, ".. " + command);
}

// handle FTP mkd
// MKD [PATH] tries to create the directories in PATH
const response mkdFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "MKD command requires an authenticated session"};
	auto [path, leftover] = getNextParam(command);
	if (leftover != "")
		return {501, "MKD command can't have extra params"};
	// if we have access to this path then let's create it
	const auto [resPath, error] = getPath(ftp, path);
	if (error)
		return {550, "Invalid path or no access"};
	//
	fs::create_directories(resPath);
	ftp.logger << getPeer(ftp) << " - user created dir " << resPath.generic_string() << ENDL;
	return {200, "Directory created"};
}

// handle FTP SYST
// let's just fake our server type and always say we are on linux
const response systFTP(FTP &ftp, const std::string command) {
	return {200, "UNIX Type: L8"};
}

// handle FTP LIST
// LIST [PATH/-a]
// LIST -a/-al/-la prints "verbose" output with . and ..
// default LIST sends the directory listing to the data connection
const response listFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "LIST command requires an authenticated session"};
	const auto [path, tmp2] = getNextParam(command);
	if (tmp2 != "")
		return {501, "LIST command can't have extra params"};
	fs::path requestPath = ftp.curDir;
	// the path isn't actually a request to send verbose output then check file permissions
	if (path != "-a" and path != "-al" and path != "-la") {
		// check if we have access to this path
		if (path != "") {
			const auto[resPath, error] = getPath(ftp, path);
			if (error or not fs::exists(resPath))
				return {550, "Invalid path or no access"};
			requestPath = resPath;
		}
	}
	// try to establish data connection
	const auto [connectionError, connectionCode, errorString] = initDataConnection(ftp);
	// couldn't successfully connect for data transmission
	if (connectionError)
		return {connectionCode, errorString};
	ftp.logger << getPeer(ftp) << " - data connection opened for directory listing of " << ftp.curDir.generic_string() << ENDL;
	// successfully opened connection, send good code
	sendReply(ftp, 125, "Opened connection, about to begin transfer of directory listing");
	streamTransferWriter listWriter;
	// if we requested verbose output then send classic . and .. directories
	if (path == "-a" or path == "-al" or path == "-la") {
		// error during writing
		if (listWriter.write(ftp.dataSocket, listVerboseData)) {
			ftp.logger << getPeer(ftp) << " - error during sending data: " << ftp.dataSocket.last_error_str() << ENDL;
			ftp.dataSocket.shutdown();
			ftp.dataSocket.close();
			return {426, "Error during dir listing transmission"};
		}
	}
	for (auto entry: fs::directory_iterator(requestPath)) {
		const std::string currentName = getFilePerms(entry.path()) + " " +
										std::to_string(fs::file_size(entry.path())) + "b " + entry.path().filename().generic_string() + CRLF;
		const dataT currentNameData(currentName.begin(), currentName.end());
		// error happened during writing
		if (listWriter.write(ftp.dataSocket, currentNameData)) {
			ftp.logger << getPeer(ftp) << " - error during sending data: " << ftp.dataSocket.last_error_str() << ENDL;
			ftp.dataSocket.shutdown();
			ftp.dataSocket.close();
			return {426, "Error during dir listing transmission"};
		}
	}
	// error during flushing leftover data to the socket
	if (listWriter.flush(ftp.dataSocket)) {
		ftp.logger << getPeer(ftp) << " - error during flushing leftover data: " << ftp.dataSocket.last_error_str() << ENDL;
		ftp.dataSocket.shutdown();
		ftp.dataSocket.close();
		return {426, "Error during dir listing transmission"};
	}
	ftp.dataSocket.shutdown();
	ftp.dataSocket.close();
	ftp.logger << getPeer(ftp) << " - directory listing was successful, sent all data" << ENDL;
	return {226, "Successfully transferred directory listing"};
}

// handle FTP STOR
// STOR [PATH] tries to write the file to path
// only writes if we have access to this path and if the path points to a file in an existing folder
const response storFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "STOR command requires an authenticated session"};
	auto [path, tmp2] = getNextParam(command);
	if (tmp2 != "")
		return {501, "STOR command can't have extra params"};
	if (path == "")
		return {501, "You have to specify result filename or path"};
	const auto [resPath, pathError] = getPath(ftp, path);
	// if the path is illegal or if the path to the file doesn't exist then we can't write
	if (pathError or not fs::exists(resPath.parent_path()))
		return {550, "Invalid file path"};
	// if the specified filename/path points to directory then we can't convert it to a file
	if (fs::exists(resPath) and fs::is_directory(resPath))
		return {550, "Invalid file path"};
	// the filepath is correct, we can write to it
	// try to establish data connection
	const auto [connectionError, connectionCode, errorString] = initDataConnection(ftp);
	// couldn't successfully connect for data transmission
	if (connectionError)
		return {connectionCode, errorString};
	sendReply(ftp, 125, "Beginning file transfer");
	try {
		ftp.logger << getPeer(ftp) << " - user stored file " << resPath.generic_string() << ENDL;
		// open the file in binary output mode and write blocks of bytes
		std::ofstream file(resPath.generic_string(), std::ofstream::binary);
		// initialize the local buffer
		netbuffer localNetbuff;
		// try to get data and write to file while we can
		while (true) {
			const dataT block = read(ftp.dataSocket, localNetbuff);
			// if the block is empty then finish reading
			if (block.empty())
				break;
			// write the block to the file
			file.write((char *)block.data(), block.size());
		}
		file.close();
		ftp.dataSocket.shutdown();
		ftp.dataSocket.close();
		return {226, "Successful file transfer"};
	} catch (std::exception &e) {
		ftp.logger << getPeer(ftp) << " - Error trying to write to file (STOR): " << resPath.generic_string() << " : " << e.what();
		return {426, "Error during storing the file"};
	}
}

// handle FTP RETR
// RETR [PATH] tries to retrieve requested file
const response retrFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "STOR command requires an authenticated session"};
	auto [path, tmp2] = getNextParam(command);
	if (tmp2 != "")
		return {501, "STOR command can't have extra params"};
	if (path == "")
		return {501, "You have to specify requested filename or path"};
	const auto [resPath, pathError] = getPath(ftp, path);
	// if the path is illegal or if the path to the file doesn't exist then we can't write
	if (pathError or not fs::exists(resPath))
		return {550, "Invalid file path"};
	// if the specified filename/path points to directory then we can't send it as a file
	if (fs::exists(resPath) and fs::is_directory(resPath))
		return {550, "Invalid file path"};
	// the file exists so we could try sending it
	// try to establish data connection
	const auto [connectionError, connectionCode, errorString] = initDataConnection(ftp);
	// couldn't successfully connect for data transmission
	if (connectionError)
		return {connectionCode, errorString};
	sendReply(ftp, 125, "Beginning file transfer");
	try {
		ftp.logger << getPeer(ftp) << " - user requested file " << resPath.generic_string() << ENDL;
		// open the file in binary output mode and write blocks of bytes
		fs::ifstream file(resPath.generic_string(), std::ofstream::binary);
		// initialize the streamwriter class
		streamTransferWriter localWriter;
		// local buffer for reading
		dataT buffer;
		buffer.reserve(BUFSIZE);
		// try to get read data and send
		while (not file.eof()) {
			file.read(reinterpret_cast<char *>(buffer.data()), BUFSIZE);
			const int32_t numRead = file.gcount();
			// we read zero bytes so lets just quit
			if (!numRead)
				break;
			buffer.assign(buffer.data(), buffer.data() + numRead);
			// error happens during sending data
			if (localWriter.write(ftp.dataSocket, buffer)) {
				file.close();
				ftp.dataSocket.shutdown();
				ftp.dataSocket.close();
				return {426, "Error during file transmission"};
			}
			buffer.clear();
		}
		// try flushing the rest of the data
		if (localWriter.buffer.size() != 0 and localWriter.flush(ftp.dataSocket)) {
			file.close();
			ftp.dataSocket.shutdown();
			ftp.dataSocket.close();
			return {426, "Error during file transmission"};
		}
		file.close();
		ftp.dataSocket.shutdown();
		ftp.dataSocket.close();
		return {226, "Successful file transfer"};
	} catch (std::exception &e) {
		ftp.logger << getPeer(ftp) << " - Error trying to read from file (RETR): " << resPath.generic_string() << " : " << e.what();
		return {426, "Error during retrieving the file"};
	}
}

#endif //CPP_FTP_FTP_HPP
