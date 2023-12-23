#ifndef __PB2LIB_H_INCLUDED
#define __PB2LIB_H_INCLUDED

#include <winsock2.h>

#include <vector>
#include <string>
#include <optional>
#include <stdexcept>
#include <thread>
#include <map>
#include <future>
#include <utility>
#include <chrono>

using namespace std::literals::chrono_literals;


namespace pb2lib {
class Exception : private std::runtime_error {
public:
	using std::runtime_error::runtime_error;
	using std::runtime_error::what;
};


class TimeoutException : public Exception {
public:
	TimeoutException() : Exception("Timeout") {}
};


enum class Team : char {
	BLUE = 'b',
	RED = 'r',
	YELLOW = 'y',
	PURPLE = 'p',
	OBSERVER = 'o',
	AUTO = 'a',
};

struct Address {
	std::string ip = "";
	int port = 0;

	auto operator<=>(const Address&) const = default;

	explicit operator sockaddr_in() const noexcept;
	explicit operator std::string() const;
};

struct Player {
	std::string name;
	int number = 0;
	int op = 0;
	int build = 0;  // always 0 for bots
	std::optional<int> id = std::nullopt;  // might not be logged in
	std::optional<Address> address = std::nullopt;  // annotation might fail
	std::optional<int> ping = std::nullopt;  // annotation might fail
	std::optional<int> score = std::nullopt;  // annotation might fail
	std::optional<Team> team = std::nullopt;  // annotation might fail
};


class WsaRaiiWrapper {
public:
	WsaRaiiWrapper();
	~WsaRaiiWrapper();

	WsaRaiiWrapper(const WsaRaiiWrapper&) = delete;
	WsaRaiiWrapper(WsaRaiiWrapper&&) = delete;
	WsaRaiiWrapper& operator=(const WsaRaiiWrapper&) = delete;
	WsaRaiiWrapper& operator=(WsaRaiiWrapper&&) = delete;
};


/*
* Benchmarks regarding sockets on Windows 10, sending 10'000 UDP packets
* New socket for each packet: 0.514109s
* Reusing same socket for every packet: 0.079289
* -> Creating a new one is ~10x slower, but still fast enough for the pb2 context with <100 servers
*/
class UdpSocket {
public:
	UdpSocket();
	~UdpSocket();

	UdpSocket(const UdpSocket&) = delete;
	UdpSocket(UdpSocket&&) = delete;
	UdpSocket& operator=(const UdpSocket&) = delete;
	UdpSocket& operator=(UdpSocket&&) = delete;

	void clear_receive_queue(void);
	void send(const sockaddr_in& address, const std::string& packet_content);  // require std::string because pb2 requires terminating NULL in the packet.
	bool wait_for_data(std::chrono::milliseconds timeout) const;
	void receive(std::vector<char>* buffer, sockaddr_in* remote_address, std::chrono::milliseconds timeout);

private:
	WsaRaiiWrapper wsa_raii_wrapper_;
	SOCKET socket_handle_;
};


class SingleRemoteEndpointUdpSocket { // UdpSocket "bound" to a single remote endpoint (knows send address, checks receive address)
public:
	SingleRemoteEndpointUdpSocket(const Address& remote_address);
	~SingleRemoteEndpointUdpSocket() = default;

	SingleRemoteEndpointUdpSocket(const SingleRemoteEndpointUdpSocket&) = delete;
	SingleRemoteEndpointUdpSocket(SingleRemoteEndpointUdpSocket&&) = delete;
	SingleRemoteEndpointUdpSocket& operator=(const SingleRemoteEndpointUdpSocket&) = delete;
	SingleRemoteEndpointUdpSocket& operator=(SingleRemoteEndpointUdpSocket&&) = delete;

	void clear_receive_queue(void);
	void send(const std::string& packet_content);
	void receive(std::vector<char>* buffer, std::chrono::milliseconds timeout);

private:
	UdpSocket socket_;
	sockaddr_in remote_address_;
};


class AsyncHostnameResolver {
public:
	using CallbackT = std::function<void(const std::string&)>;
	std::future<std::string> resolve(const Address& address, CallbackT callback = {});
	void drop_outstanding(const Address& address);

private:
	void thread_func(std::stop_token);

	struct MapValue {
		std::promise<std::string> promise;
		CallbackT callback;
	};
	struct MapComparator {
		bool operator()(const sockaddr_in& lhs, const sockaddr_in& rhs) const {
			return std::tuple(lhs.sin_addr.S_un.S_addr, lhs.sin_port)
				< std::tuple(rhs.sin_addr.S_un.S_addr, rhs.sin_port);
		}
	};
	std::multimap<sockaddr_in, MapValue, MapComparator> requests_by_address_;
	std::mutex requests_by_address_mutex_;

	UdpSocket socket_;
	std::jthread receive_thread_{ [this](std::stop_token st) { thread_func(std::move(st)); } };
};


Team team_from_string(std::string_view team_string);


struct PacketAwareSendArgs {
	// RCON in general works through Printf-redirect in SVC_RemoteCommand, buffer size is set to 1384 here, flushing happens on every print call.
	// Out-of-band answer in SV_StatusString also truncates to this length.
	constexpr static size_t MAX_PACKET_SIZE = 1384;

	const Address* address = nullptr;
	std::chrono::milliseconds timeout = 0ms;
	size_t assume_additional_packet_if_packet_bigger_than = 0;
};

std::string send_connectionless(const PacketAwareSendArgs& args, std::string_view message);
std::string send_rcon(const PacketAwareSendArgs& args, std::string_view rcon_password, std::string_view message);


struct SendArgs {
	const Address* address;
	std::chrono::milliseconds timeout;
};
std::string send_rcon(const SendArgs& args, std::string_view rcon_password, std::string_view message);

std::string get_cvar(const SendArgs& args, std::string_view rcon_password, std::string_view cvar);
std::vector<std::string> get_cvars(const SendArgs& args, std::string_view rcon_password, const std::vector<std::string>& cvars);

// TODO: Send/receive asynchronously
std::vector<Player> get_players_from_rcon_sv_players(const SendArgs& args, std::string_view rcon_password);
void annotate_score_ping_address_from_rcon_status(std::vector<Player>* players, const SendArgs& args, std::string_view rcon_password);
void annotate_team_from_status(std::vector<Player>* players, const SendArgs& args);
std::vector<Player> get_players(const SendArgs& args, std::string_view rcon_password);

}

#endif // __PB2LIB_H_INCLUDED