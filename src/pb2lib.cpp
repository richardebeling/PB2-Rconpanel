#include "pb2lib.h"
#include <ws2tcpip.h>
#include <winerror.h>
#include <regex>
#include <ranges>
#include <assert.h>

namespace pb2lib {

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;


Address::operator sockaddr_in() const noexcept {
	sockaddr_in result = { 0 };
	result.sin_family = AF_INET;
	result.sin_port = htons(port);
	InetPton(AF_INET, ip.c_str(), &result.sin_addr.s_addr);
	return result;
}

Address::operator std::string() const {
	return ip + ":" + std::to_string(port);
}


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


UdpSocket::UdpSocket() {
	socket_handle_ = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_handle_ == INVALID_SOCKET) {
		throw Exception("Error opening socket. WSAGetLastError() = " + std::to_string(WSAGetLastError()));
	}
}

UdpSocket::~UdpSocket() {
	closesocket(socket_handle_);
}

void UdpSocket::clear_receive_queue() {
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

void UdpSocket::send(const sockaddr_in& address, const std::string& packet_content) {
	const auto ret = sendto(socket_handle_, packet_content.c_str(),
		static_cast<int>(packet_content.size() + 1), 0,
		reinterpret_cast<const sockaddr*>(&address), sizeof(address));

	if (ret == SOCKET_ERROR) {
		throw Exception("sendto failed, WSAGetLastError() = " + std::to_string(WSAGetLastError()));
	}
}

bool UdpSocket::wait_for_data(std::chrono::milliseconds timeout) const {
	fd_set fdset = { 0 };
	timeval time_val = { 0 };
	FD_ZERO(&fdset);
	FD_SET(socket_handle_, &fdset);
	time_val.tv_sec = static_cast<long>(timeout.count() / 1000);
	time_val.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000l);

	const int select_result = select(FD_SETSIZE, &fdset, 0, 0, &time_val);
	if (select_result == SOCKET_ERROR) {
		throw Exception("select failed, WSAGetLastError() = " + std::to_string(WSAGetLastError()));
	}

	return select_result;
}

void UdpSocket::receive(std::vector<char>* buffer, sockaddr_in* remote_address, std::chrono::milliseconds timeout) {
	const bool data_available = wait_for_data(timeout);
	if (!data_available) {
		buffer->resize(0);
		return;
	}

	// Must be big enough to hold a single answer packet, otherwise excess data of that packet is discarded and our answer is lost
	buffer->resize(65535);
	int sender_address_size = sizeof(*remote_address);

	int receive_result = recvfrom(socket_handle_, buffer->data(),
		static_cast<int>(buffer->size()), 0, reinterpret_cast<sockaddr*>(remote_address), &sender_address_size);
	if (receive_result == SOCKET_ERROR) {
		auto last_error = WSAGetLastError();
		// from/to localhost, windows will give a WSAEConnReset error with UDP if the remove port is closed
		// https://stackoverflow.com/questions/30749423/is-winsock-error-10054-wsaeconnreset-normal-with-udp-to-from-localhost
		// we just want to handle that like a timeout / no response, since that is what happens with non-localhost servers
		if (last_error == WSAECONNRESET) {
			buffer->resize(0);
			return;
		}

		throw Exception("recvfrom failed, WSAGetLastError() = " + std::to_string(last_error));
	}

	assert(sender_address_size == sizeof(sockaddr_in));
	buffer->at(receive_result) = '\0';
	buffer->resize(static_cast<size_t>(receive_result) + 1);
}


SingleRemoteEndpointUdpSocket::SingleRemoteEndpointUdpSocket(const Address& remote_address) {
	remote_address_ = static_cast<sockaddr_in>(remote_address);
	socket_.clear_receive_queue();
}

void SingleRemoteEndpointUdpSocket::send(const std::string& packet_content) {
	return socket_.send(remote_address_, packet_content);
}


void SingleRemoteEndpointUdpSocket::receive(std::vector<char>* buffer, std::chrono::milliseconds timeout) {
	sockaddr_in sender_address = { 0 };
	socket_.receive(buffer, &sender_address, timeout);

	if (!buffer->empty() && memcmp(&(sender_address.sin_addr), &(remote_address_.sin_addr), sizeof(sender_address.sin_addr)) != 0) {
		throw Exception("Received packet from wrong remote address");
	}
}

std::future<std::string> AsyncHostnameResolver::resolve(const Address& address, CallbackT callback) {
	sockaddr_in addr = static_cast<sockaddr_in>(address);
	std::lock_guard guard(requests_by_address_mutex_);

	auto new_entry_it = requests_by_address_.emplace(addr, MapValue{
		std::promise<std::string>{},
		std::move(callback),
	});

	try {
		socket_.send(addr, "\xFF\xFF\xFF\xFFstatus");
	}
	catch (pb2lib::Exception&) {
		requests_by_address_.erase(new_entry_it);
		std::promise<std::string> promise;
		promise.set_exception(std::current_exception());
		std::string result;
		return promise.get_future();
	}

	return new_entry_it->second.promise.get_future();
}

void AsyncHostnameResolver::drop_outstanding(const Address& address) {
	std::lock_guard guard(requests_by_address_mutex_);
	requests_by_address_.erase(static_cast<sockaddr_in>(address));
}

void AsyncHostnameResolver::thread_func(std::stop_token stop_token) {
	constexpr auto wake_up_interval = 100ms;

	sockaddr_in remote_address;
	std::vector<char> buffer;
	std::cmatch matches;
	std::regex rx(R"(\\hostname\\(.*?)\\)");

	while (!stop_token.stop_requested()) {
		bool data_available = socket_.wait_for_data(wake_up_interval);
		if (!data_available)
			continue;

		socket_.receive(&buffer, &remote_address, 0ms);
		const char* response_begin = buffer.data();
		const char* response_end = response_begin + buffer.size();

		if (!std::regex_search(response_begin, response_end, matches, rx)) {
			continue;
		}

		std::string hostname = matches[1];

		{
			std::lock_guard guard(requests_by_address_mutex_);

			auto [begin, end] = requests_by_address_.equal_range(remote_address);
			for (auto it = begin; it != end; ++it) {
				it->second.promise.set_value(hostname);
				it->second.callback(hostname);
			}

			requests_by_address_.erase(begin, end);
		}
	}
}


void async_send_connectionless(SingleRemoteEndpointUdpSocket& socket, const PacketAwareSendArgs& args, std::string_view message) {
	std::string packet_content = "\xFF\xFF\xFF\xFF";
	packet_content += message;
	socket.send(packet_content);
}

std::string async_receive_connectionless(SingleRemoteEndpointUdpSocket& socket, const PacketAwareSendArgs& args) {
	std::string response = "print\n";
	bool received_anything = false;

	std::vector<char> buffer;
	while (true) {
		socket.receive(&buffer, args.timeout);

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

		if (partial_response.size() < args.assume_additional_packet_if_packet_bigger_than) {
			break;
		}
	}

	if (!received_anything) {
		throw TimeoutException();
	}

	return response;
}


std::string send_connectionless(const PacketAwareSendArgs& args, std::string_view message) {
	SingleRemoteEndpointUdpSocket socket(*args.address);

	async_send_connectionless(socket, args, message);
	return async_receive_connectionless(socket, args);
}


std::string make_rcon_message(std::string_view command, std::string_view rcon_password) {
	std::string message = "rcon ";
	message += rcon_password;
	message += " ";
	message += command;

	return message;
}

std::string send_rcon(const SendArgs& args, std::string_view rcon_password, std::string_view command) {
	// If the caller doesn't want to bother with packet splitting, we'll just assume that no reasonable print with more than 512 chars will happen.
	PacketAwareSendArgs forward_args{
		.address = args.address,
		.timeout = args.timeout,
		.assume_additional_packet_if_packet_bigger_than = PacketAwareSendArgs::MAX_PACKET_SIZE - 512
	};

	std::string response = send_connectionless(forward_args, make_rcon_message(command, rcon_password));

	if (response == "print\nBad rcon_password.\n") {
		throw Exception("Bad rcon password");
	}

	return response;
}


std::string get_cvar(const SendArgs& args, std::string_view rcon_password, std::string_view cvar) {
	// Separate implementation from get_cvars because it doesn't have the delimiter problem

	std::string response = send_rcon(args, rcon_password, cvar);

	std::smatch matches;
	if (std::regex_match(response, matches, std::regex("print\n\".*?\" is \"(.*)\"\n"))) {
		return matches[1];
	}


	if (std::regex_search(response, matches, std::regex("Unknown command \".*\""))) {
		throw Exception("Attempted to get value of undefined cvar " + std::string(cvar));
	}

	throw Exception("Unexpected error when retrieving value of cvar " + std::string(cvar) + ": " + response);
}


std::vector<std::string> get_cvars(const SendArgs& args, std::string_view rcon_password, const std::vector<std::string>& cvars) {
	// PB2 rcon tokenizing (Cmd_TokenizeString) will treat any char <= 32 as a space, so we can't use ASCII End-of-transmission-block or similar.
	static constexpr char delimiter = '\x9C'; // latin-1 "string terminator"

	std::string command = "echo ";
	for (size_t i = 0; i < cvars.size(); ++i) {
		command += "$" + cvars[i] + delimiter;
	}

	std::string response = send_rcon(args, rcon_password, command);
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


std::vector<Player> get_players_from_rcon_sv_players_response(std::string_view response) {
	std::vector<Player> result;

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

std::vector<Player> get_players_from_rcon_sv_players(const SendArgs& args, std::string_view rcon_password) {
	std::string response = send_rcon(args, rcon_password, "sv players");
	return get_players_from_rcon_sv_players_response(response);
}

void annotate_score_ping_address_from_rcon_status_response(std::vector<Player>* players, std::string_view response) {
	const std::regex rx(R"((\d+)\s*(\d+)\s*(\d+)\s{0,1}(.+?)\s*(\d+|CNCT)\s*(\d+\.\d+\.\d+\.\d+):(\d{1,5})\s*(\d{2,5}))");
	//						 NUM-1	 SCORE-2 PING-3		 NAME-4	  LASTMSG-5    IP-6					PORT-7		QPORT-8

	using ItT = std::regex_iterator<std::string_view::const_iterator>;
	using MatchT = std::match_results<std::string_view::const_iterator>;

	for(auto it = ItT(response.begin(), response.end(), rx); it != ItT{}; ++it) {
		const MatchT match = *it;
		const std::string name = match[4].str();
		const int number = std::stoi(match[1]);
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

		player_it->score = std::stoi(match[2]);
		player_it->ping = std::stoi(match[3]);
		player_it->address = { match[6], std::stoi(match[7]) };
	}
}

void annotate_team_from_status_response(std::vector<Player>* players, std::string_view response) {
	// Currently doesn't annotate bot teams as bots are not reported in connectionless status response

	if (!response.starts_with("print\n")) {
		throw Exception("Unexpected status response: " + std::string(response));
	}

	std::string_view serverinfo = response.substr("print\n"sv.size());
	serverinfo = serverinfo.substr(0, serverinfo.find('\n'));

	const std::regex rx(R"((?:^|\\)p([byrpo])\\((\!\d+)+)(?:$|\\))");

	using ItT = std::regex_iterator<std::string_view::const_iterator>;
	using MatchT = std::match_results<std::string_view::const_iterator>;

	for (auto it = ItT(serverinfo.begin(), serverinfo.end(), rx); it != ItT{}; ++it) {
		const MatchT match = *it;

		const std::string team_string = match[1].str();
		const Team team = team_from_string(team_string);

		const std::string concatenated_numbers = match[2].str();
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

std::vector<Player> get_players(const SendArgs& args, std::string_view rcon_password) {
	// rcon sv players : longest player line I can come up with is 69 bytes-- " 123 (1234567)] * OP (9999), XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX (b123)"
	// rcon status : player lines are 68 chars long -> if there's more than 68 bytes left, we must be at the end of the message
	// non-rcon status: stops adding player lines if that would exceed 1384 bytes // SV_StatusString, if (statusLength + playerLength >= sizeof(status))
	PacketAwareSendArgs forward_args{
		.address = args.address,
		.timeout = args.timeout,
		.assume_additional_packet_if_packet_bigger_than = PacketAwareSendArgs::MAX_PACKET_SIZE - 100
	};

	SingleRemoteEndpointUdpSocket rcon_sv_players_socket(*args.address);
	async_send_connectionless(rcon_sv_players_socket, forward_args, make_rcon_message("sv players", rcon_password));

	SingleRemoteEndpointUdpSocket rcon_status_socket(*args.address);
	async_send_connectionless(rcon_status_socket, forward_args, make_rcon_message("status", rcon_password));

	SingleRemoteEndpointUdpSocket status_socket(*args.address);
	async_send_connectionless(status_socket, forward_args, "status");

	std::vector<Player> result = get_players_from_rcon_sv_players_response(async_receive_connectionless(rcon_sv_players_socket, forward_args));
	annotate_score_ping_address_from_rcon_status_response(&result, async_receive_connectionless(rcon_status_socket, forward_args));
	annotate_team_from_status_response(&result, async_receive_connectionless(status_socket, forward_args));

	return result;
}


};