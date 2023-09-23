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

#ifndef RCONFUNCTIONS_H_INCLUDED
#define RCONFUNCTIONS_H_INCLUDED

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <vector>
#include <string>

// TODO: Namespace

constexpr static size_t MTU = 65536; //Maximum packet size that might come back as answer to a rcon packet (Ethernet MTU is <= 1500, TCP Limit is 65536)

struct Player //basic information gained by "sv players" and RCON / non RCON status
{
	std::string sName;
	std::string sIp;
	int iPort = 0;
	int iNumber = 0;
	int iId = 0;
	int iBuild = 0;
	int iOp = 0;
	int iPing = 0;
	int iScore = 0;
	char cColor = 'o';
};

struct Server
{
	std::string sIp;
	std::string sHostname;
	std::string sRconPassword;
	int iPort = 0;

	Server() = default;
	Server(std::string ip, int port);

	void retrieveAndSetHostname(SOCKET hUdpSocket = 0, double dTimeout = 0.5);
};

// TODO: Proper API
// * split functions for rcon / non rcon
// * overloads taking Server instance
// * handle optional udp socket -- make explicit optional / give overload without parameter?

int iSendMessageToServer(std::string sIpAddress, int iPort, std::string sMessage, std::string* sReturnBuffer,
						std::string sRconPassword = "", SOCKET hUdpSocket = 0, double dTimeout = 0.5);

int iPlayerStructVectorFromAddress (std::string sIpAddress, int iPort, std::string sRconPassword,
								std::vector <Player> * playervector, SOCKET hUdpSocket = 0, double dTimeout = 0.5);

int iVarContentFromName (std::string sIpAddress, int iPort, std::string sRconPassword, std::string sVarName,
						std::string* sReturnBuffer, SOCKET hUdpSocket = 0, double dTimeout = 0.5);

int InitializeWinsock();
int ShutdownWinsock();

#endif // RCONFUNCTIONS_H_INCLUDED