#ifndef CPP_FTP_FTPTRANSFER_H
#define CPP_FTP_FTPTRANSFER_H

#include <sockpp/tcp_socket.h>
#include "globals.hpp"

class streamTransferWriter {
public:
	dataT buffer;
	streamTransferWriter() {
		buffer.reserve(BUFSIZE);
	}

	// write remaining data to socket
	const bool flush(sockpp::stream_socket &sock) {
		// error happened
		if (sock.write_n(buffer.data(), buffer.size()) < buffer.size())
			return true;
		return false;
	}

	// lazily write data to socket
	const bool write(sockpp::stream_socket &sock, dataT data) {
		// if we don't have enough space then flush
		int32_t leftspace = buffer.capacity() - buffer.size();
		if (leftspace < data.size()) {
			buffer.insert(buffer.end(), data.begin(), data.begin() + leftspace);
			if (flush(sock))
				return true;
			buffer.clear();
			buffer.insert(buffer.end(), data.begin() + leftspace, data.end());
			return false;
		}
		buffer.insert(buffer.end(), data.begin(), data.end());
		return false;
	}
};

#endif //CPP_FTP_FTPTRANSFER_H
