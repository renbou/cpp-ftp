#ifndef CPP_FTP_ARGPARSE_HPP
#define CPP_FTP_ARGPARSE_HPP

#include <sockpp/socket.h>
#include <iostream>
#include "globals.hpp"

std::tuple<in_port_t, std::string, std::string, bool> parseArgs(int argc, const char *argv[]) {
	// options for program launch
	typedef std::pair<std::string, std::string> optionPair;
	static const optionPair portOption = {"-p", "--port"};
	static const optionPair helpOption = {"-h", "--help"};
	static const optionPair logOption = {"-l", "--log"};
	static const optionPair dirOption = {"-d", "--directory"};

	// no arguments - launch on default port and without logging and no need to close
	if (argc == 1) {
		std::cout << "Port not specified, will use default port" << std::endl;
		std::cout << "Start with \"" << helpOption.first << "\" or \"" << helpOption.second << "\" for help." << std::endl;
		return {defaultPort, "", defaultWorkdir, false};
	}

	// lambda which creates a function for checking if arg is equal to an optionList
	const auto findIfOption = [](optionPair optionList){
		return [=](std::string argument){
			return (argument == optionList.first || argument == optionList.second);
		};
	};

	const auto portOptionFinder = findIfOption(portOption);
	const auto helpOptionFinder = findIfOption(helpOption);
	const auto logOptionFinder = findIfOption(logOption);
	const auto dirOptionFinder = findIfOption(dirOption);

	const auto portOptionLoc = std::find_if(argv, argv + argc, portOptionFinder);
	const auto helpOptionLoc = std::find_if(argv, argv + argc, helpOptionFinder);
	const auto logOptionLoc = std::find_if(argv, argv + argc, logOptionFinder);
	const auto dirOptionLoc = std::find_if(argv, argv + argc, dirOptionFinder);

	// check if the option has an argument, i.e. option isn't the last string in argv
	const auto isPresent = [=](const auto location){ return location < (argv + argc); };

	// we need to print out the help
	// return default port, no logging, but help printed so we close
	if (isPresent(helpOptionLoc)) {
		std::cout << "Usage: " << argv[0] << " [port]" << std::endl;
		std::cout <<
				  "Extra options:\n"
				  "\t-h/--help -- Print this help message\n"
				  "\t-p/--port [PORT] -- Specify port in a different manner, overrides the other port specified\n"
				  "\t-l/--log [LOGFILE] -- Enable logging to LOGFILE\n"
				  "\t-d/--directory [DIRPATH] -- launch server with server root in a different directory (default is myftpserver)\n"
				  "Creator: @renbou :)" << std::endl;
		return {defaultPort, "", defaultWorkdir, true};
	}

	// get the log filename if logging is enabled
	const auto [logString, logError] = [=]() -> std::pair<std::string, bool> {
		if (isPresent(logOptionLoc)) {
			// log option is present but log file isn't specified then close
			if (logOptionLoc == (argv + argc - 1)) {
				std::cerr << "ERROR! Log option specified without a log file." << std::endl;
				return {"", true};
			}
			// otherwise return the next parameter as the logfile
			return {argv[logOptionLoc - argv + 1], false};
		}
		return {"", false};
	}();

	// get the directory path if present as option
	const auto [dirPath, dirError] = [=]() -> std::pair<std::string, bool> {
		if (isPresent(dirOptionLoc)) {
			// directory option is present but path isn't specified then close
			if (dirOptionLoc == (argv + argc - 1)) {
				std::cerr << "ERROR! Directory option specified without a path." << std::endl;
				return {defaultWorkdir, true};
			}
			// otherwise return the next parameter as the path
			return {argv[dirOptionLoc - argv + 1], false};
		}
		return {defaultWorkdir, false};
	}();

	// get the port if specified
	// if -p specified it overrides other params
	const auto [port, portError] = [=]() -> std::pair<in_port_t, bool> {
		const auto [tmpPort, tmpError] = [=]() -> std::pair<int64_t, bool> {
			// if the port option is specified, don't care about any other port values
			if (isPresent(portOptionLoc)) {
				// port option is present but no port is specified afterwards
				if (portOptionLoc == (argv + argc - 1)) {
					std::cerr << "ERROR! Port option specified but without a port after it." << std::endl;
					return {defaultPort, true};
				}
				return {std::stol(argv[portOptionLoc - argv + 1]), false};
			}
			// find the port value
			// it will be the first value without a named option
			std::function<const char **(const char **)> findPort;
			findPort = [=, &findPort](const char **location) -> const char** {
				// if we have reached the end then there is no port option
				if (location == (argv + argc))
					return nullptr;
				// if the current or previous argument is an option, skip
				if ((
						logOptionFinder(*(location - 1)) or
						helpOptionFinder(*(location - 1)) or
						dirOptionFinder(*(location - 1)) or
						portOptionFinder(*(location - 1)))
					or (**location == '-'))
				{
					return findPort(location + 1);
				}
				// we have found the port option!
				return location;
			};
			// try to find the port starting from second argument
			const char ** portTmp = findPort(argv + 1);
			// no port
			if (portTmp == nullptr)
				return {defaultPort, false};
			try {
				return {std::stoll(*portTmp), false};
			} catch (std::exception &e) {
				std::cerr<< "ERROR! while parsing the port from option \"" << *portTmp << "\": " << e.what() << std::endl;
				return {defaultPort, true};
			}
		}();

		if (tmpPort < 0 or tmpPort > 65535) {
			std::cerr << "ERROR! Invalid port " << tmpPort << std::endl;
			return {defaultPort, true};
		}
		return {tmpPort, tmpError};
	}();

	// finally return parsed variables
	return {port, logString, dirPath, logError or portError or dirError};
}

#endif //CPP_FTP_ARGPARSE_HPP
