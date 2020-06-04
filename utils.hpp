#ifndef CPP_FTP_UTILS_HPP
#define CPP_FTP_UTILS_HPP

#include <fstream>
#include <iostream>
#include <utility>
#include <vector>
// for working with filesystem
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;

// endline for logger
enum loggerEndl {ENDL};

// logger class which outputs to stdout as well to log
// if we don't supply a log file name then we simply set the logfile ptr to nullptr
struct loggerT {

	// unique_ptr for easier checking
	// also it will automatically close on really bad errors
	std::unique_ptr<std::ofstream> logFile;

	loggerT(const std::string logFileName) {
		if (logFileName == "")
			logFile = std::make_unique<std::ofstream>(nullptr);
		else {
			logFile = std::make_unique<std::ofstream>(std::ofstream(logFileName.c_str()));
			std::cout << "Logging to file " << logFileName << std::endl;
		}
	}

	// operators for outputting various values
	template<typename T>
	loggerT& operator<<(T value) {
		std::cout << value;
		if (logFile)
			*logFile << value;
		return *this;
	}

	// only accept the specific ENDL value
	// send std::endl to both streams, effectively flushing them
	loggerT& operator<<(const loggerEndl) {
		std::cout << std::endl;
		if (logFile)
			*logFile << std::endl;
		return *this;
	}

	// correctly handle the closing of required file
	void close() {
		if (logFile)
			logFile->close();
	}
};

// function which returns current parameter and the rest of the string (separated by space)
const std::pair<std::string, std::string> getNextParam(const std::string str) {
	auto pos = str.find(' ');
	if (pos == std::string::npos)
		return {str, ""};
	return {str.substr(0, pos), str.substr(pos + 1)};
}

// recursively splits a string into vector by delimiter
const std::vector<std::string> splitByDelim(std::string str, std::string delim) {
	if (str == "")
		return {};
	int32_t pos = str.find(delim);
	if (pos == std::string::npos)
		return {str};
	std::vector<std::string> tmp {str.substr(0, pos)};
	const std::vector<std::string> append = splitByDelim(str.substr(pos + 1), delim);
	tmp.insert(tmp.end(), append.begin(), append.end());
	return tmp;
}

// returns a string of file permissions
// linux-like way
const std::string getFilePerms (fs::path path) {
	const fs::file_status fileStat = fs::status(path);
	const fs::perms filePerms = fileStat.permissions();
	return std::string{} + (fs::is_directory(fileStat) ? "d" : "-") +
	       ((filePerms & fs::perms::owner_read) != fs::perms::none ? "r": "-") +
	       ((filePerms & fs::perms::owner_write) != fs::perms::none ? "w": "-") +
	       ((filePerms & fs::perms::owner_exec) != fs::perms::none ? "x": "-") +
	       ((filePerms & fs::perms::group_read) != fs::perms::none ? "r": "-") +
	       ((filePerms & fs::perms::group_write) != fs::perms::none ? "w": "-") +
	       ((filePerms & fs::perms::group_exec) != fs::perms::none ? "x": "-") +
	       ((filePerms & fs::perms::others_read) != fs::perms::none ? "r": "-") +
	       ((filePerms & fs::perms::others_write) != fs::perms::none ? "w": "-") +
	       ((filePerms & fs::perms::others_exec) != fs::perms::none ? "x": "-");
}
#endif //CPP_FTP_UTILS_HPP
