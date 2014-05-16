/*
	Copyright (C) 2014 Richard Ebeling

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

#define MTU 65536 //Maximum packet size that might come back as answer to a rcon packet.
//Set that high so it even works in future (Current DSL / Ethernet MTU is <= 1500, TCP Limit is 65536)

#include <winsock2.h>
#include <vector>
#include <string>

struct PLAYER //basic information gained by "sv players" and RCON / non RCON status
{
	std::string sName;
	std::string sIp;
	int iPort;
	int iNumber;
	int iId;
	int iBuild;
	int iOp;
	int iPing;
	int iScore;
	char cColor;
};

/*struct SERVEREX //extended information including current status
{
	char sIp;
	char sHostname;
	char sRconPassword;
	char sTimeLeft;
	char sScores;
	char sAdmin;
	char sEmail;
	char sLocation;
	char sWebsite;
	char sMapname;
	char sVersion;
	int iPort;
	int iMaxclients;
	int iElim;
	int iFraglimit;
	int iNeedpass;
	int iSv_Login;
	int iTimelimit;
};*/

struct SERVER //Basic information needed to communicate with this server
{
	std::string sIp;
	std::string sHostname;
	std::string sRconPassword;
	int iPort;
};

void vResetPlayer(PLAYER * player);

int iSendMessageToServer(std::string sIpAddress, int iPort, std::string sMessage, std::string* sReturnBuffer,
						std::string sRconPassword = "", SOCKET hUdpSocket = 0, double dTimeout = 0.5);

//TODO (#1#): int iServerExStructFromAddress (const char *szIpAddress, int iPort, SERVER *pServerStruct, SOCKET hUdpSocket = 0, double dTimeout = 0.5);
int iServerStructFromAddress (std::string sIpAddress, int iPort, SERVER *pServerStruct, SOCKET hUdpSocket = 0, double dTimeout = 0.5);

int iPlayerStructVectorFromAddress (std::string sIpAddress, int iPort, std::string sRconPassword,
								std::vector <PLAYER> * playervector, SOCKET hUdpSocket = 0, double dTimeout = 0.5);

int iVarContentFromName (std::string sIpAddress, int iPort, std::string sRconPassword, std::string sVarName,
						std::string* sReturnBuffer, SOCKET hUdpSocket = 0, double dTimeout = 0.5);

int InitializeWinsock();
int ShutdownWinsock();
#endif // RCONFUNCTIONS_H_INCLUDED