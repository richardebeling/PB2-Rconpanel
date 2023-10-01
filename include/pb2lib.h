#ifndef __PB2LIB_H_INCLUDED
#define __PB2LIB_H_INCLUDED

#include <winsock2.h>

#include <vector>
#include <string>
#include <optional>
#include <stdexcept>


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
};


struct Player
{
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


struct Server
{
	Address address;
	std::string hostname;
	std::string rcon_password;

	Server() = default;
	Server(Address address) : address{ address } {};
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
class SocketRaiiWrapper {
public:
	SocketRaiiWrapper(const Address& remote_address);
	~SocketRaiiWrapper();

	SocketRaiiWrapper(const SocketRaiiWrapper&) = delete;
	SocketRaiiWrapper(SocketRaiiWrapper&&) = delete;
	SocketRaiiWrapper& operator=(const SocketRaiiWrapper&) = delete;
	SocketRaiiWrapper& operator=(SocketRaiiWrapper&&) = delete;

	void clear_receive_queue(void) noexcept;
	void send(const std::string& packet_content);  // require std::string because pb2 requires terminating NULL in the packet.
	void receive(std::vector<char>* buffer, double timeout);

private:
	SOCKET socket_handle_;
	sockaddr_in remote_address_;
};


Team team_from_string(std::string_view team_string);

std::string send_connectionless(const Address& address, std::string_view message, double timeout);
std::string send_rcon(const Address& address, std::string_view rcon_password, std::string_view message, double timeout);

std::optional<std::string> get_hostname_or_nullopt(const Address& address, double timeout) noexcept;

std::string get_cvar(const Address& address, std::string_view rcon_password, std::string_view cvar, double timeout);
std::vector<std::string> get_cvars(const Address& address, std::string_view rcon_password, const std::vector<std::string>& cvars, double timeout);

std::vector<Player> get_players_from_rcon_sv_players(const Address& address, std::string_view rcon_password, double timeout);
void annotate_score_ping_address_from_rcon_status(std::vector<Player>* players, const Address& address, std::string_view rcon_password, double timeout);
void annotate_team_from_status(std::vector<Player>* players, const Address& address, double timeout);
std::vector<Player> get_players(const Address& address, std::string_view rcon_password, double timeout);
}

#endif // __PB2LIB_H_INCLUDED