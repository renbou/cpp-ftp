#ifndef CPP_FTP_NETBUFFER_HPP
#define CPP_FTP_NETBUFFER_HPP

#include "globals.hpp"
#include <sockpp/socket.h>

// class for reading from sockpp line-by-line
struct netbuffer {
	dataT buffer;
	netbuffer() {
		buffer.reserve(BUFSIZE);
	};
};

// function for cleaning up the buffer
inline void clearBuffer(netbuffer &netbuff) {
	netbuff.buffer.clear();
	netbuff.buffer.reserve(BUFSIZE);
}

// function for moving leftovers beginning at pos to the beginning of the buffer (-2 to account for CRLF)
inline void readyBuffer(netbuffer &netbuff, dataT::iterator pos) {
	std::move_backward(pos + 2, netbuff.buffer.end(), netbuff.buffer.begin() + (netbuff.buffer.end() - pos - 2));
	netbuff.buffer.resize(netbuff.buffer.end() - pos - 2);
}

// function to find a pair of bytes in dataT
// for finding CRLF
dataT::iterator findPair(dataT::iterator start, dataT::iterator end, const std::pair<byte, byte> &toFind) {
	// if we reached the end (only one char left)
	if (start == end or start + 1 == end)
		return end;
	if (*start == toFind.first and *(start + 1) == toFind.second)
		return start;
	return findPair(start + 1, end, toFind);
}

// read CRLF line from net buffer and return the line read
dataT readline(sockpp::tcp_socket &socket, netbuffer &netbuff) {
	dataT::iterator ptrToCRLF;
	// while we can't find CRLF and while the buffer isn't full
	// if buffer is full then let's return a zero sized buffer, and cause a 500 error
	while ((ptrToCRLF = findPair(netbuff.buffer.begin(), netbuff.buffer.end(), CRLFp)) == netbuff.buffer.end() and
		   netbuff.buffer.size() != netbuff.buffer.capacity()) {
		// number of bytes read
		int32_t readn = socket.read(netbuff.buffer.data() + netbuff.buffer.size(), netbuff.buffer.capacity() - netbuff.buffer.size());
		// we can't read anymore
		// connection either ended, reset or dropped
		// OR
		// some error happened, we can't read anymore, we should close
		if (readn <= 0) {
			return {'X','Q','U','I','T','N','O','W'};
		} else {
			// resize vector so that the size is correct
			netbuff.buffer.assign(netbuff.buffer.data(), netbuff.buffer.data() + netbuff.buffer.size() + readn);
		}
	}
	// if the command is too long return empty buffer
	if (ptrToCRLF == netbuff.buffer.end()) {
		clearBuffer(netbuff);
		return {};
	}
	const dataT returnBuffer(netbuff.buffer.begin(), ptrToCRLF);
	readyBuffer(netbuff, ptrToCRLF);
	return returnBuffer;
}

#endif //CPP_FTP_NETBUFFER_HPP
