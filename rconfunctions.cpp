/*
	Copyright (C) 2015 Richard Ebeling

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

#include "rconfunctions.h"
#include <regex>
#include <ranges>

int InitializeWinsock()
{
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2,0),&wsa);
}

int ShutdownWinsock()
{
	return WSACleanup();
}

// TODO: Return optional(string)
int iVarContentFromName (std::string sIpAddress, int iPort, std::string sRconPassword, std::string sVarName,
						std::string* sReturnBuffer, SOCKET hUdpSocket, double dTimeout)
{
	std::string sAnswer;
	iSendMessageToServer(sIpAddress, iPort, sVarName, &sAnswer, sRconPassword, hUdpSocket, dTimeout);
	std::smatch MatchResults;
	std::regex rx ("\".*\" is \"(.*)\"");
	if (std::regex_search(sAnswer, MatchResults, rx))
	{
		sReturnBuffer->assign(MatchResults[1]);
		return 1;
	}
	else
		return 0;
}

Server::Server(std::string ip, int port) : sIp(std::move(ip)), iPort(port) {}

void Server::retrieveAndSetHostname(SOCKET hUdpSocket, double dTimeout) {
	std::string sAnswer;
	iSendMessageToServer(sIp, iPort, "status", &sAnswer, "", hUdpSocket, dTimeout);
	std::smatch MatchResults;
	std::regex rx("\\\\hostname\\\\(.*?)\\\\");
	int found = std::regex_search(sAnswer, MatchResults, rx);

	std::string sHostname = "COULD NOT GET HOSTNAME";
	if (found)
		sHostname = MatchResults[1];
}

int iSendMessageToServer(std::string sIpAddress, int iPort, std::string sMessage, std::string* sReturnBuffer,
						std::string sRconPassword, SOCKET hUdpSocket, double dTimeout)
{
	bool bOwnSocket = false;
	if (hUdpSocket == 0 || hUdpSocket == INVALID_SOCKET)
	{
		bOwnSocket = true;
		hUdpSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (hUdpSocket == INVALID_SOCKET)
		{
			closesocket(hUdpSocket);
			return -1;
		}
	}

	std::string sPacketContent;
	if(sRconPassword == "") //if szRconPassword == "" ==> if no rcon is being sent
	{
		sPacketContent.assign("\xFF\xFF\xFF\xFF");
		sPacketContent.append(sMessage);
	}
	else //if a rcon command is being sent
	{
		sPacketContent.assign("\xFF\xFF\xFF\xFFrcon ");
		sPacketContent.append(sRconPassword);
		sPacketContent.append(" ");
		sPacketContent.append(sMessage);
	}

	struct sockaddr_in stAddress = { 0 };
	stAddress.sin_family = AF_INET;
	InetPton(AF_INET, sIpAddress.c_str(), &stAddress.sin_addr.s_addr);
	stAddress.sin_port = htons(iPort);

	int iSizeOfAddressStructure = sizeof(stAddress);

	fd_set fdset = { 0 }; //remove any answers that are still depending for that socket while we don't want them
	timeval tv = { 0 };
	FD_ZERO(&fdset);
	FD_SET(hUdpSocket, &fdset);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	while (select(FD_SETSIZE, &fdset, 0, 0, &tv))
	{
		recvfrom(hUdpSocket, NULL, 0, 0, NULL, 0);
	}

	if (SOCKET_ERROR == sendto(hUdpSocket, sPacketContent.c_str(), static_cast<int>(sPacketContent.size() + 1), 0, (sockaddr*)  &stAddress, iSizeOfAddressStructure))
	{
		if (bOwnSocket)
		{
			closesocket(hUdpSocket);
		}
		return -2;
	}

	sReturnBuffer->assign("print\n");
	int iTotalReceivedBytes = 6;

	bool bReceivedSomething = false;

	std::vector<char> buffer(MTU);

	while (true)
	{
		FD_ZERO(&fdset);
		FD_SET(hUdpSocket, &fdset);
		tv.tv_sec = static_cast<long>(dTimeout);
		tv.tv_usec = static_cast<long>((fmod(dTimeout, 1))*1000000);

		int selectResult = select(FD_SETSIZE, &fdset, 0, 0, &tv);
		if (selectResult > 0)
		{
			bReceivedSomething = true;
			int receivedBytes = recvfrom(hUdpSocket, buffer.data(), static_cast<int>(buffer.size()), 0, (sockaddr*) &stAddress, &iSizeOfAddressStructure);

			buffer[receivedBytes] = '\0';

			if (strncmp(buffer.data(), "\xFF\xFF\xFF\xFFprint\n", 10) == 0)
			{
				memmove(buffer.data(), buffer.data()+10, strlen(buffer.data()));
				receivedBytes -= 10;
			}
			else
				return -5;

			sReturnBuffer->append(buffer.data());

			iTotalReceivedBytes += receivedBytes;
			if (receivedBytes < 1200)  // todo: possibly wrong heuristic
				break;
		}
		else if (selectResult == 0)
			break;
		else if (selectResult < 0)
			return -3;
	}

	if (bOwnSocket)
		closesocket(hUdpSocket);

	if (!bReceivedSomething)
	{
		return -4;
	}

	return iTotalReceivedBytes;
}

int iPlayerStructVectorFromAddress (std::string sIpAddress, int iPort, std::string sRconPassword,
								std::vector <Player> * playervector, SOCKET hUdpSocket , double dTimeout)
{
	if (sRconPassword.compare("") == 0)
		return -10;

	playervector->clear();
	std::string sAnswer;
	
	//{ send and process answer to "sv players" (gives number, name, build, ID and OP)
	int iRetVal = iSendMessageToServer(sIpAddress, iPort, "sv players\0", &sAnswer, sRconPassword, hUdpSocket, dTimeout);
	if (iRetVal <= 0)
		return iRetVal;

	std::smatch MatchResults;
	std::regex rx;
	std::vector <std::string> vLines;
	
	auto lines = sAnswer
		| std::ranges::views::split(' ')
		| std::ranges::views::transform([](auto&& subrange) { return std::string(&*subrange.begin(), std::ranges::distance(subrange)); });

	for (const auto& line : lines)
	{		
		rx.assign("(\\d+) \\((\\d+)\\)] \\* OP (\\d+), (.*?) \\(b(\\d+)\\)");
		if (std::regex_search(line, MatchResults, rx)) //Admin, logged in
		{
			Player tempplayer;
			tempplayer.iNumber = atoi( std::string(MatchResults[1]).c_str() );
			tempplayer.iId     = atoi( std::string(MatchResults[2]).c_str() );
			tempplayer.iOp     = atoi( std::string(MatchResults[3]).c_str() );
			tempplayer.iBuild  = atoi( std::string(MatchResults[5]).c_str() );
			tempplayer.sName   = std::string(MatchResults[4]);
			
			playervector->push_back(tempplayer);
			continue;
		}
		
		rx.assign("(\\d+) \\((\\d+)\\)] \\* (.*?) \\(b(\\d+)\\)");
		if (std::regex_search(line, MatchResults, rx)) //Player, logged in
		{
			std::string sName (MatchResults[3]);

			Player tempplayer;
			tempplayer.iNumber = atoi( std::string(MatchResults[1]).c_str() );
			tempplayer.iId	   = atoi( std::string(MatchResults[2]).c_str() );
			tempplayer.iBuild  = atoi( std::string(MatchResults[4]).c_str() );
			tempplayer.sName   = sName;
			playervector->push_back(tempplayer);
			continue;
		}

		rx.assign("(\\d+) ] \\* OP (\\d+), (.*?) \\(b(\\d+)\\)");
		if (std::regex_search(line, MatchResults, rx)) //Admin, not logged in
		{
			Player tempplayer;
			tempplayer.iNumber = atoi( std::string (MatchResults[1]).c_str() );
			tempplayer.iOp     = atoi( std::string (MatchResults[2]).c_str() );
			tempplayer.iBuild  = atoi( std::string (MatchResults[4]).c_str() );
			tempplayer.sName   = std::string (MatchResults[3]);
			
			playervector->push_back(tempplayer);
			continue;
		}

		rx.assign("(\\d+) ] \\* (.*?) \\(b(\\d+)\\)");
		if (std::regex_search(line, MatchResults, rx)) //Player, not logged in
		{			
			std::string sName (MatchResults[2]);

			Player tempplayer;
			tempplayer.iNumber = atoi( std::string (MatchResults[1]).c_str() );
			tempplayer.iBuild  = atoi( std::string (MatchResults[3]).c_str() );
			tempplayer.sName   = sName;
			
			playervector->push_back(tempplayer);
			continue;
		}

		rx.assign("(\\d+) \\(bot\\)] \\* (.*?) \\(b0\\)");
		if (std::regex_search(line, MatchResults, rx)) //Bot
		{
			Player tempplayer;
			tempplayer.iNumber = atoi( std::string(MatchResults[1]).c_str() );
			tempplayer.sName   = std::string(MatchResults[2]);
			
			playervector->push_back(tempplayer);
			continue;
		}
	}

	//}

	//{ send and process answer to "status" as RCON packet (gives IP, score and ping)
	iRetVal = iSendMessageToServer(sIpAddress, iPort, "status", &sAnswer, sRconPassword, hUdpSocket, dTimeout);
	if (iRetVal <= 0)
		return iRetVal;
	
	rx.assign("(\\d+)\\s*(\\d+)\\s*(\\d+)\\s{0,1}(.+?)\\s*(\\d+|CNCT)\\s*(\\d+\\.\\d+\\.\\d+\\.\\d+):(\\d{1,5})\\s*(\\d{2,5})");
	//			NUM-1	SCORE-2		PING-3	NAME-4		LASTMSG-5	  IP-6							PORT-7			QPORT-8
	std::string::const_iterator start = sAnswer.begin();
	std::string::const_iterator end   = sAnswer.end();
	std::string sName;
	
	while (std::regex_search(start, end, MatchResults, rx))
	{
		start = MatchResults[0].second;
		sName.assign(MatchResults[4]);

		for (unsigned int i = 0; i < playervector->size(); i++)
		{
			try
			{
				//name is cut off after 15 chars in rcon status command
				//Numbers of nonames and newbies are not added to the name in the answer to "status"
				//names only consisting of spaces)
				if ( ( (playervector->at(i).sName.compare(0, 16, sName)   == 0)
					|| (playervector->at(i).sName.compare(0, 6, "noname") == 0 && sName.compare(0, 6, "noname") == 0)
					|| (playervector->at(i).sName.compare(0, 6, "newbie") == 0 && sName.compare(0, 6, "newbie") == 0)
					|| (playervector->at(i).sName.find_first_not_of(' ') == std::string::npos && sName.find_first_not_of(' ') == std::string::npos))
					&& (playervector->at(i).iNumber == atoi( std::string(MatchResults[1]).c_str() ) ) )
				{
					playervector->at(i).iScore = atoi( std::string(MatchResults[2]).c_str() );
					playervector->at(i).iPing  = atoi( std::string(MatchResults[3]).c_str() );
					playervector->at(i).iPort  = atoi( std::string(MatchResults[7]).c_str() );
					playervector->at(i).sIp    = std::string (MatchResults[6]);
					break;
				}
			}
			// todo -- race condition, mutex
			catch (const std::out_of_range&)
			{
				break;
			}
		}
	}
	
	//}
	
	//{ send and process answer to "status" as non-RCON packet (gives color)
	iRetVal = iSendMessageToServer(sIpAddress, iPort, "status", &sAnswer, "", hUdpSocket, dTimeout);
	if (iRetVal <= 0)
		return iRetVal;
	
	rx.assign("p([byrpo])\\\\((\\!\\d+)+)");
	start = sAnswer.begin();
	end   = sAnswer.end();
	
	std::string sColor;
	std::string sNumbers;

	while (std::regex_search(start, end, MatchResults, rx))
	{
		start = MatchResults[0].second;
		
		sColor.assign(MatchResults[1]);
		sNumbers.assign(MatchResults[2]);
		
		char* strtok_context = NULL;
		char* szFound = strtok_s(sNumbers.data(), "!", &strtok_context);
		while (szFound != NULL)
		{
			int iNum = atoi(szFound);
			try
			{
				playervector->at(iNum).cColor = *(sColor.c_str());
			}
			// todo -- race condition, mutex
			catch (const std::out_of_range&)
			{
				//Maybe another player connected until now and there is no playervector->at(iNum), so if this occurs just ignore it.
				break;
			}
			szFound = strtok_s(NULL, "!", &strtok_context);
		}
	}
	//}
	return 1;
}