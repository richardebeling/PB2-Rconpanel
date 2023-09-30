/*
	Copyright (C) 2023 Richard Ebeling

	This file is part of "DP:PB2 Rconpanel".
	"DP:PB2 Rconpanel" is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program (Filename: COPYING).
	If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PB2LIB_H_INCLUDED
#define PB2LIB_H_INCLUDED

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


struct Player
{
	std::string name;
	std::string ip;
	int port = 0;
	int number = 0;
	int id = 0;  // TODO: Make optional (other fields as well, only keep those that are set)
	int build = 0;
	int op = 0;
	int ping = 0;
	int score = 0;
	Team team = Team::OBSERVER;
};


struct Address {
	std::string ip = "";
	int port = 0;
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
void annotate_score_ping_port_ip_from_rcon_status(std::vector<Player>* players, const Address& address, std::string_view rcon_password, double timeout);
void annotate_team_from_status(std::vector<Player>* players, const Address& address, double timeout);
std::vector<Player> get_players(const Address& address, std::string_view rcon_password, double timeout);
}

#endif // PB2LIB_H_INCLUDED