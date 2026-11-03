#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <string>

namespace Trauma::Net {
	class Network {
	public:
		Network();
		~Network();
		Network(const Network&) = delete;
		Network& operator=(const Network&) = delete;

		bool IsReady() const;

	private:
		bool bReady = false;
	};

	struct Endpoint {
		sockaddr_in address{};

		bool operator==(const Endpoint& other) const;
		std::string ToString() const;
	};

	class Socket {
	public:
		Socket() = default;
		~Socket();
		Socket(const Socket&) = delete;
		Socket& operator=(const Socket&) = delete;

		bool Open(uint16_t port);
		void Close();
		bool SendTo(const Endpoint& endpoint, const void* data, size_t size) const;
		int ReceiveFrom(Endpoint& endpoint, void* buffer, size_t capacity) const;
		uint16_t GetPort() const;

	private:
		SOCKET handle = INVALID_SOCKET;
	};

	bool ResolveEndpoint(const std::string& host, uint16_t port, Endpoint& endpoint);
}
