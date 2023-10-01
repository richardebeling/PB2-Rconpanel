#include "pb2lib.h"
#include <ws2tcpip.h>
#include <winerror.h>
#include <regex>
#include <ranges>
#include <assert.h>

namespace pb2lib {

using namespace std::string_literals;
using namespace std::string_view_literals;


WsaRaiiWrapper::WsaRaiiWrapper() {
	WSADATA wsa;
	const auto ret = WSAStartup(MAKEWORD(2, 0), &wsa);
	if (ret != 0) {
		throw Exception("Error in WSAStartup: " + std::to_string(ret));
	}
}


WsaRaiiWrapper::~WsaRaiiWrapper() {
	(void) WSACleanup();
}


SocketRaiiWrapper::SocketRaiiWrapper(const Address& remote_address) {
	socket_handle_ = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handle_ == INVALID_SOCKET) {
		throw Exception("Error opening socket. WSAGetLastError() = " + std::to_string(WSAGetLastError()));
	}

	remote_address_ = { 0 };
	remote_address_.sin_family = AF_INET;
	remote_address_.sin_port = htons(remote_address.port);
	InetPton(AF_INET, remote_address.ip.c_str(), &remote_address_.sin_addr.s_addr);
}


SocketRaiiWrapper::~SocketRaiiWrapper() {
	closesocket(socket_handle_);
}


void SocketRaiiWrapper::clear_receive_queue() noexcept {
	fd_set fdset = { 0 };
	timeval tv = { 0 };
	FD_ZERO(&fdset);
	FD_SET(socket_handle_, &fdset);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	while (select(FD_SETSIZE, &fdset, 0, 0, &tv)) {
		recvfrom(socket_handle_, NULL, 0, 0, NULL, 0);
	}
}


void SocketRaiiWrapper::send(const std::string& packet_content) {
	const auto ret = sendto(socket_handle_, packet_content.c_str(),
		static_cast<int>(packet_content.size() + 1), 0,
		reinterpret_cast<const sockaddr*>(&remote_address_), sizeof(remote_address_));

	if (ret == SOCKET_ERROR) {
		throw Exception("sendto failed, WSAGetLastError() = " + std::to_string(WSAGetLastError()));
	}
}


void SocketRaiiWrapper::receive(std::vector<char>* buffer, double timeout) {
	// select is necessary to get timeout behavior
	fd_set fdset = { 0 };
	timeval time_val = { 0 };
	FD_ZERO(&fdset);
	FD_SET(socket_handle_, &fdset);
	time_val.tv_sec = static_cast<long>(timeout);
	time_val.tv_usec = static_cast<long>((fmod(timeout, 1)) * 1'000'000);

	const int select_result = select(FD_SETSIZE, &fdset, 0, 0, &time_val);
	if (select_result == SOCKET_ERROR) {
		throw Exception("select failed, WSAGetLastError() = " + std::to_string(WSAGetLastError()));
	}

	if (select_result == 0) {
		buffer->resize(0);
		return;
	}

	// Must be big enough to hold a single answer packet, otherwise excess data of that packet is discarded and our answer is lost
	buffer->resize(65535);
	sockaddr_in sender_address;
	int sender_address_size = sizeof(sender_address);

	int receive_result = recvfrom(socket_handle_, buffer->data(), static_cast<int>(buffer->size()), 0, reinterpret_cast<sockaddr*>(&sender_address), &sender_address_size);
	if (receive_result == SOCKET_ERROR) {
		auto last_error = WSAGetLastError();
		// From/To localhost, windows will give a WSAEConnReset error with UDP if the remove port is closed
		// https://stackoverflow.com/questions/30749423/is-winsock-error-10054-wsaeconnreset-normal-with-udp-to-from-localhost
		// We just want to handle that like a timeout / no response, since that is what happens with non-localhost servers
		if (last_error == WSAECONNRESET) {
			buffer->resize(0);
			return;
		}

		throw Exception("recvfrom failed, WSAGetLastError() = " + std::to_string(last_error));
	}

	assert(sender_address_size == sizeof(sockaddr_in));
	if (memcmp(&(sender_address.sin_addr), &(remote_address_.sin_addr), sizeof(sender_address.sin_addr)) != 0) {
		throw Exception("Received packet from wrong remote address");
	}

	buffer->at(receive_result) = '\0';
	buffer->resize(static_cast<size_t>(receive_result) + 1);
}


std::optional<std::string> get_hostname_or_nullopt(const Address& address, double timeout) noexcept {

	std::string response;
	try {
		response = send_connectionless(address, "status", timeout);
	}
	catch (pb2lib::Exception&) {
		return std::nullopt;
	}

	std::smatch matches;
	if (!std::regex_search(response, matches, std::regex(R"(\\hostname\\(.*?)\\)"))) {
		return std::nullopt;
	}
	return matches[1];
}


std::string send_connectionless(const Address& address, std::string_view message, double timeout) {
	static const WsaRaiiWrapper wra_wrapper;

	SocketRaiiWrapper socket(address);
	socket.clear_receive_queue();

	std::string packet_content = "\xFF\xFF\xFF\xFF";
	packet_content += message;
	socket.send(packet_content);

	std::string response = "print\n";
	bool received_anything = false;

	std::vector<char> buffer;
	while (true) {
		socket.receive(&buffer, timeout);

		if (buffer.empty()) { // timeout
			break;
		}

		if (buffer.back() != '\0') {  // should be ensured by `receive`, but we rely on it here.
			throw Exception("Received invalid not null-terminated packet");
		}

		std::string partial_response = buffer.data();

		constexpr std::string_view magic_start = "\xFF\xFF\xFF\xFFprint\n"sv;
		if (partial_response.size() < magic_start.length() || !partial_response.starts_with(magic_start)) {
			throw Exception("Invalid server packet start: " + partial_response);
		}
		partial_response = partial_response.substr(magic_start.length());

		response += partial_response;
		received_anything = true;

		if (partial_response.size() < 1200) {
			// early exit if we don't expect additional packets
			break;  // todo: possibly wrong, but can we do better?
		}
	}

	if (!received_anything) {
		throw TimeoutException();
	}

	return response;
}


std::string send_rcon(const Address& address, std::string_view rcon_password, std::string_view message, double timeout) {
	std::string packet = "rcon ";
	packet += rcon_password;
	packet += " ";
	packet += message;

	std::string response = send_connectionless(address, packet, timeout);

	if (response == "print\nBad rcon_password.\n") {
		throw Exception("Bad rcon password");
	}

	return response;
}


std::string get_cvar(const Address& address, std::string_view rcon_password, std::string_view cvar, double timeout) {
	// Separate implementation from get_cvars because it doesn't have the delimiter problem

	std::string response = send_rcon(address, rcon_password, cvar, timeout);

	std::smatch matches;
	if (std::regex_match(response, matches, std::regex("print\n\".*?\" is \"(.*)\"\n"))) {
		return matches[1];
	}


	if (std::regex_search(response, matches, std::regex("Unknown command \".*\""))) {
		throw Exception("Attempted to get value of undefined cvar " + std::string(cvar));
	}

	throw Exception("Unexpected error when retrieving value of cvar " + std::string(cvar) + ": " + response);
}


std::vector<std::string> get_cvars(const Address& address, std::string_view rcon_password, const std::vector<std::string>& cvars, double timeout) {
	// PB2 rcon tokenizing (Cmd_TokenizeString) will treat any char <= 32 as a space, so we can't use ASCII End-of-transmission-block or similar.
	static constexpr char delimiter = '\x9C'; // latin-1 "string terminator"

	std::string command = "echo ";
	for (size_t i = 0; i < cvars.size(); ++i) {
		command += "$" + cvars[i] + delimiter;
	}

	std::string response = send_rcon(address, rcon_password, command, timeout);
	response = response.substr(6);  // "print\n"

	if (std::ranges::count(response, delimiter) != cvars.size()) {
		throw Exception("Delimiter ("s + delimiter + ") contained in cvar value. Can not split correctly.");
	}

	auto splits = response
		| std::ranges::views::split(delimiter)
		| std::ranges::views::transform([](auto&& subrange) { return std::string_view(subrange.begin(), subrange.end()); });

	std::vector<std::string> result = { splits.begin(), splits.end() };
	result.resize(result.size() - 1); // remove part after last delimiter
	assert(result.size() == cvars.size());
	return result;
}


Team team_from_string(std::string_view team_string) {

	if (team_string.length() == 0) {
		throw Exception("Invalid empty team string");
	}

	switch (team_string[0]) {
		case 'b': return Team::BLUE;
		case 'r': return Team::RED;
		case 'y': return Team::YELLOW;
		case 'p': return Team::PURPLE;
		case 'o': return Team::OBSERVER;
		case 'a': return Team::AUTO;
	}

	throw Exception("Invalid team string: " + std::string(team_string));
}


std::vector<Player> get_players_from_rcon_sv_players(const Address& address, std::string_view rcon_password, double timeout) {
	std::vector<Player> result;

	const std::string response = send_rcon(address, rcon_password, "sv players", timeout);
	auto lines = response
		| std::ranges::views::split('\n')
		| std::ranges::views::transform([](auto&& rng) { return std::string(rng.begin(), rng.end()); });

	std::smatch matches;
	std::regex rx;

	for (const auto& line : lines)
	{
		rx = R"((\d+) \((\d+)\)\] \* OP (\d+), (.*?) \(b(\d+)\))";
		if (std::regex_search(line, matches, rx)) // Admin, logged in
		{
			Player player;
			player.number = std::stoi(matches[1]);
			player.id = std::stoi(matches[2]);
			player.op = std::stoi(matches[3]);
			player.build = std::stoi(matches[5]);
			player.name = matches[4];
			result.push_back(player);

			continue;
		}

		rx = R"((\d+) \((\d+)\)\] \* (.*?) \(b(\d+)\))";
		if (std::regex_search(line, matches, rx)) // Player, logged in
		{
			Player player;
			player.number = std::stoi(matches[1]);
			player.id = std::stoi(matches[2]);
			player.build = std::stoi(matches[4]);
			player.name = matches[3];
			result.push_back(player);

			continue;
		}

		rx = R"((\d+) \] \* OP (\d+), (.*?) \(b(\d+)\))";
		if (std::regex_search(line, matches, rx)) // Admin, not logged in
		{
			Player player;
			player.number = std::stoi(matches[1]);
			player.op = std::stoi(matches[2]);
			player.build = std::stoi(matches[4]);
			player.name = matches[3];
			result.push_back(player);

			continue;
		}

		rx = R"((\d+) \] \* (.*?) \(b(\d+)\))";
		if (std::regex_search(line, matches, rx)) // Player, not logged in
		{
			Player player;
			player.number = std::stoi(matches[1]);
			player.build = std::stoi(matches[3]);
			player.name = matches[2];
			result.push_back(player);

			continue;
		}

		rx = R"((\d+) \(bot\)\] \* (.*?) \(b0\))";
		if (std::regex_search(line, matches, rx)) // Bot
		{
			Player player;
			player.number = std::stoi(matches[1]);
			player.name = matches[2];
			result.push_back(player);

			continue;
		}
	}

	return result;
}

void annotate_score_ping_address_from_rcon_status(std::vector<Player>* players, const Address& address, std::string_view rcon_password, double timeout) {
	const std::string response = send_rcon(address, rcon_password, "status", timeout);

	std::regex rx(R"((\d+)\s*(\d+)\s*(\d+)\s{0,1}(.+?)\s*(\d+|CNCT)\s*(\d+\.\d+\.\d+\.\d+):(\d{1,5})\s*(\d{2,5}))");
	//							 NUM-1	 SCORE-2 PING-3		 NAME-4	  LASTMSG-5    IP-6					PORT-7		QPORT-8

	std::string::const_iterator start = response.begin();
	std::string::const_iterator end = response.end();
	std::smatch matches;
	while (std::regex_search(start, end, matches, rx)) {
		start = matches[0].second;

		const std::string name = matches[4];
		const int number = std::stoi(matches[1]);
		if (name == "CNCT") {
			continue;
		}

		// If the player disconnected and someone else connected in the meantime, we will annotate the new ip to the old player
		// However, matching names doesn't work because the "you can only change your name 3 times"-limit is not reflected in the
		// rcon status answer, so we have a name mismatch here.
		auto player_it = std::find_if(players->begin(), players->end(), [&](const Player& player){
			return player.number == number;
			
			/*  // Old name-matching code
			return player.name.compare(0, 16, name) == 0
				|| (player.name.starts_with("noname") && name.starts_with("noname"))
				|| (player.name.starts_with("newbie") && name.starts_with("newbie"))
				|| (player.name.find_first_not_of(' ') == std::string::npos && name.find_first_not_of(' ') == std::string::npos);
			*/
		});

		if (player_it == players->end()) {
			continue;
		}

		player_it->score = std::stoi(matches[2]);
		player_it->ping = std::stoi(matches[3]);
		player_it->address = { matches[6], std::stoi(matches[7]) };
	}
}

void annotate_team_from_status(std::vector<Player>* players, const Address& address, double timeout) {
	const std::string response = send_connectionless(address, "status", timeout);

	std::regex rx(R"(p([byrpo])\\((\!\d+)+))");
	std::string::const_iterator start = response.begin();
	std::string::const_iterator end = response.end();
	std::smatch matches;

	while (std::regex_search(start, end, matches, rx))
	{
		start = matches[0].second;

		const std::string team_string = matches[1];
		const Team team = team_from_string(team_string);

		std::string concatenated_numbers = matches[2];
		auto number_strings = concatenated_numbers
			| std::ranges::views::split('!')
			| std::ranges::views::transform([](auto&& rng) { return std::string(rng.begin(), rng.end()); })
			| std::ranges::views::filter([](auto&& str) { return !str.empty(); });

		for (const auto& number_string : number_strings) {
			int number;
			try {
				number = std::stoi(number_string);
			}
			catch (std::exception&) {
				continue;
			}

			auto player_it = std::find_if(players->begin(), players->end(), [&](const Player& player) {
				return player.number == number;
			});

			if (player_it == players->end()) {
				continue;
			}

			player_it->team = team;
		}
	}
}

std::vector<Player> get_players(const Address& address, std::string_view rcon_password, double timeout) {
	std::vector<Player> result = get_players_from_rcon_sv_players(address, rcon_password, timeout);
	annotate_score_ping_address_from_rcon_status(&result, address, rcon_password, timeout);
	annotate_team_from_status(&result, address, timeout);

	return result;
}


};