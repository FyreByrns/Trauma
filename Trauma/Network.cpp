#include "Network.h"
#include <timeapi.h>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

namespace Trauma::Net {
	Network::Network() {
		WSADATA data{};
		bReady = WSAStartup(MAKEWORD(2, 2), &data) == 0;
		if (bReady)
			timeBeginPeriod(1);
	}

	Network::~Network() {
		if (!bReady)
			return;

		timeEndPeriod(1);
		WSACleanup();
	}

	bool Network::IsReady() const {
		return bReady;
	}

	bool Endpoint::operator==(const Endpoint& other) const {
		return address.sin_addr.s_addr == other.address.sin_addr.s_addr
			&& address.sin_port == other.address.sin_port;
	}

	std::string Endpoint::ToString() const {
		char buffer[INET_ADDRSTRLEN]{};
		InetNtopA(AF_INET, &address.sin_addr, buffer, INET_ADDRSTRLEN);
		return std::string(buffer) + ":" + std::to_string(ntohs(address.sin_port));
	}

	Socket::~Socket() {
		Close();
	}

	bool Socket::Open(uint16_t port) {
		Close();

		handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (handle == INVALID_SOCKET)
			return false;

		sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_port = htons(port);

		if (bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
			Close();
			return false;
		}

		u_long nonBlocking = 1;
		if (ioctlsocket(handle, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
			Close();
			return false;
		}

		return true;
	}

	void Socket::Close() {
		if (handle == INVALID_SOCKET)
			return;

		closesocket(handle);
		handle = INVALID_SOCKET;
	}

	bool Socket::SendTo(const Endpoint& endpoint, const void* data, size_t size) const {
		if (handle == INVALID_SOCKET)
			return false;

		const int sent = sendto(
			handle,
			static_cast<const char*>(data),
			static_cast<int>(size),
			0,
			reinterpret_cast<const sockaddr*>(&endpoint.address),
			sizeof(endpoint.address));

		return sent == static_cast<int>(size);
	}

	int Socket::ReceiveFrom(Endpoint& endpoint, void* buffer, size_t capacity) const {
		if (handle == INVALID_SOCKET)
			return -1;

		int addressSize = sizeof(endpoint.address);
		return recvfrom(
			handle,
			static_cast<char*>(buffer),
			static_cast<int>(capacity),
			0,
			reinterpret_cast<sockaddr*>(&endpoint.address),
			&addressSize);
	}

	uint16_t Socket::GetPort() const {
		if (handle == INVALID_SOCKET)
			return 0;

		sockaddr_in local{};
		int size = sizeof(local);
		if (getsockname(handle, reinterpret_cast<sockaddr*>(&local), &size) == SOCKET_ERROR)
			return 0;

		return ntohs(local.sin_port);
	}

	bool Socket::IsOpen() const {
		return handle != INVALID_SOCKET;
	}

	bool ResolveEndpoint(const std::string& host, uint16_t port, Endpoint& endpoint) {
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;

		addrinfo* result = nullptr;
		const std::string portText = std::to_string(port);
		if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0)
			return false;

		endpoint.address = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
		freeaddrinfo(result);
		return true;
	}
}
