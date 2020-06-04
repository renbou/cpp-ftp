#ifndef CPP_FTP_NETBUFFER_HPP
#define CPP_FTP_NETBUFFER_HPP

#include "globals.hpp"
#include <sockpp/socket.h>

// class for reading from sockpp line-by-line
// we simply read into the buffer chunk-by-chunk
// if we encounter CRLF in the buffer then return up until CRLF
// and move the rest of the contents to the beginning
// this way we can get large amounts of data and don't have to call socket read char-by-char
// also we can use the same class for simply reading into the buffer until the connection is closed
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
const dataT readline(sockpp::tcp_socket &socket, netbuffer &netbuff) {
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
			// fix up the vector structure
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

// function for simply reading the full buffer if we can and returning it
// if some error happened then simply return zero size buffer
const dataT read(sockpp::tcp_socket &socket, netbuffer &netbuff) {
	// while the socket is open and while the buffer still has free space try to read
	while(socket and netbuff.buffer.size() != netbuff.buffer.capacity()) {
		int32_t readn = socket.read(netbuff.buffer.data() + netbuff.buffer.size(), netbuff.buffer.capacity() - netbuff.buffer.size());
		// oops, can't read anymore
		if (readn <= 0)
			break;
		// make the vector fix its structure and size for memory safe handling of data
		netbuff.buffer.assign(netbuff.buffer.data(), netbuff.buffer.data() + netbuff.buffer.size() + readn);
	}
	// copy to a new buffer the data and then clear the buffer
	// so that if we call again and sock is closed we get a correctly empty buffer
	const dataT returnVal(netbuff.buffer.begin(), netbuff.buffer.end());
	netbuff.buffer.clear();
	return returnVal;
}

#endif //CPP_FTP_NETBUFFER_HPP
