#ifndef CPP_FTP_FTP_HPP
#define CPP_FTP_FTP_HPP

#include <sockpp/tcp_socket.h>
#include <sockpp/tcp_acceptor.h>
#include <sockpp/tcp_connector.h>
#include <sockpp/inet_address.h>
#include <algorithm>
#include "globals.hpp"
#include "utils.hpp"
#include "netbuffer.hpp"
#include "ftptransfer.h"

// ftp structure for holding the connections and the state of the ftp control connection
struct FTP {
	// control connection and data connection as specified in rfc 959
	sockpp::tcp_socket controlSock;
	sockpp::tcp_acceptor pasvSock;
	sockpp::tcp_socket dataSocket;
	sockpp::inet_address peer, dataSockAddr;
	loggerT &logger;
	// server root directory path and current path
	fs::path serverRoot, workDir, curDir;
	stringHashMap users;
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


	FTP(stringHashMap users_t, sockpp::tcp_socket controlSock_t, sockpp::inet_address peer_t, fs::path workDir_t, loggerT &logger_t)
		: logger(logger_t), ftpBuf() {
		users = users_t;
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

// helper function for sending buffer to socket
const bool sendBuf(FTP& ftp, dataT &buffer) {
	if (ftp.controlSock.write_n(buffer.data(), buffer.size()) < buffer.size())
		return shutdownError(ftp, "error while sending data");
	return false;
}

// helper function to validate path
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
const bool isAuthed(FTP& ftp) {
	return ftp.user.first != "" and ftp.user.second != "";
}

// function to handle USER
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
const response reinFTP(FTP &ftp, const std::string command) {
	const auto [tmp1, tmp2] = getNextParam(command);
	if (tmp1 != "")
		return {501, "REIN can't have params"};
	ftp.user = {};
	return {220, "Server ready for new user"};
}

// handle FTP quit
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
// however with ascii mode we can't send files which aren't in ascii 7-bit range
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
// we only support block and stream mode
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
const response portFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "PORT command requires an authenticated session"};
	const auto [address, leftover] = getNextParam(command);
	// can't have leftover parameters in port
	if (leftover != "")
		return {501, "PORT command accepts only one argument"};
	// close passive connection if it is open
	if (ftp.pasvSock.is_open()) {
		ftp.passiveMode = false;
		ftp.pasvSock.shutdown();
		ftp.pasvSock.close();
	}

	auto tokens = splitByDelim(address, ",");
	ftp.dataSockAddr = sockpp::inet_address(tokens[0] + "." + tokens[1] + "." + tokens[2] + "." + tokens[3],
											std::stoi(tokens[4]) * 256 + std::stoi(tokens[5]));
	ftp.logger << getPeer(ftp) << " - user initialized port - " << ftp.dataSockAddr.to_string() << ENDL;
	return {200, "Data connection port set successfully to " + ftp.dataSockAddr.to_string()};
}

// handle FTP cwd
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
const response cdupFTP(FTP &ftp, const std::string command) {
	return cwdFTP(ftp, ".. " + command);
}

// handle FTP mkd
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

// handle FTP LIST
const response listFTP(FTP &ftp, const std::string command) {
	if (not isAuthed(ftp))
		return {530, "LIST command requires an authenticated session"};
	auto [path, tmp2] = getNextParam(command);
	if (tmp2 != "")
		return {501, "LIST command can't have extra params"};
	fs::path requestPath = ftp.curDir;
	if (path != "") {
		// check if we have access to this path
		const auto [resPath, error] = getPath(ftp, path);
		if (error)
			return {550, "Invalid path or no access"};
		requestPath = resPath;
	}
	// if we have passive mode enabled
	if (ftp.passiveMode) {
		ftp.dataSocket = ftp.pasvSock.accept(&ftp.dataSockAddr);
		// can't connect
		if (not ftp.dataSocket) {
			ftp.logger << getPeer(ftp) << " - error accepting passive connection from " << ftp.dataSockAddr.to_string() <<
						  ": " << ftp.dataSocket.last_error_str() << ENDL;
			ftp.dataSocket.close();
			return {425, "Error accepting connection"};
		}
	} else {
		sockpp::tcp_connector dataConnection(ftp.dataSockAddr);
		// can't connect
		if (not dataConnection) {
			ftp.logger << getPeer(ftp) << " - error making data connection to " << ftp.dataSockAddr.to_string() <<
						  ": " << dataConnection.last_error_str() << ENDL;
			dataConnection.close();
			return {425, "Error making connection"};
		}
		ftp.dataSocket = std::move(dataConnection);
	}
	ftp.logger << getPeer(ftp) << " - data connection opened for directory listing of " << ftp.curDir.generic_string() << ENDL;
	// successfully opened connection, send good code
	//sendReply(ftp, 125, "Opened connection, about to begin transfer of directory listing");
	streamTransferWriter listWriter;
	for (auto entry: fs::directory_iterator(requestPath)) {
		const std::string currentName = getFilePerms(entry.path()) + " " + entry.path().filename().generic_string() + CRLF;
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

#endif //CPP_FTP_FTP_HPP
