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

#include "rconfunctions.h"
#include <boost\regex.hpp>

int InitializeWinsock()
{
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2,0),&wsa);
}

int ShutdownWinsock()
{
	return WSACleanup();
}

void vResetPlayer(PLAYER * player)
{
	player->sName.assign("");
	player->sIp.assign("");
	player->iPort = 0;
	player->iNumber = 0;
	player->iId = 0;
	player->iBuild = 0;
	player->iOp = 0;
	player->iPing = 0;
	player->iScore = 0;
	player->cColor = 0;
}

int iVarContentFromName (std::string sIpAddress, int iPort, std::string sRconPassword, std::string sVarName,
						std::string* sReturnBuffer, SOCKET hUdpSocket, double dTimeout)
{
	std::string sAnswer;
	iSendMessageToServer(sIpAddress, iPort, sVarName, &sAnswer, sRconPassword, hUdpSocket, dTimeout);
	boost::smatch MatchResults;
	boost::regex rx ("\".*\" is \"(.*)\"");
	if (boost::regex_search(sAnswer, MatchResults, rx))
	{
		sReturnBuffer->assign(MatchResults[1]);
		return 1;
	}
	else
		return 0;
}

int iServerStructFromAddress (std::string sIpAddress, int iPort, SERVER *pServerStruct, SOCKET hUdpSocket, double dTimeout)
{
	std::string sAnswer;
	iSendMessageToServer(sIpAddress, iPort, "status", &sAnswer, "", hUdpSocket, dTimeout);
	boost::smatch MatchResults;
	boost::regex rx ("\\\\hostname\\\\(.*?)\\\\");
	int found = boost::regex_search(sAnswer, MatchResults, rx);

	std::string sVar = "COULD NOT GET HOSTNAME";
	if (found)
		sVar = MatchResults[1];

	pServerStruct->sHostname.assign(sVar);
	pServerStruct->sIp.assign(sIpAddress);
	pServerStruct->iPort = iPort;

	if (found) return 1;
	return 0;
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
	if(sRconPassword.compare("") == 0) //if szRconPassword == "" ==> if no rcon is being sent
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

	struct sockaddr_in stAddress;
	stAddress.sin_family = AF_INET;
	stAddress.sin_addr.s_addr = inet_addr(sIpAddress.c_str());
	stAddress.sin_port = htons(iPort);

	int iSizeOfAddressStructure = sizeof(stAddress);

	fd_set fdset; //remove any answers that are still depending for that socket while we don't want them
	timeval tv;
	FD_ZERO(&fdset);
	FD_SET(hUdpSocket, &fdset);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	while (select(FD_SETSIZE, &fdset, 0, 0, &tv))
	{
		recvfrom(hUdpSocket, NULL, 0, 0, NULL, 0);
	}

	if (sendto(hUdpSocket, sPacketContent.c_str(), sPacketContent.size() + 1, 0, (sockaddr*)  &stAddress, iSizeOfAddressStructure) == SOCKET_ERROR)
	{
		if (bOwnSocket)
		{
			closesocket(hUdpSocket);
		}
		//std::cout << WSAGetLastError() << std::endl;
		return -2;
	}

	int iReceivedBytes = 6;
	sReturnBuffer->assign("print\n");
	bool bReceivedSomething = false;

	char szTempBuffer[MTU] = {'\0'};

	while (true)
	{
		FD_ZERO(&fdset);
		FD_SET(hUdpSocket, &fdset);
		tv.tv_sec = dTimeout;
		tv.tv_usec = (fmod(dTimeout, 1))*1000000;

		int iRetVal = select(FD_SETSIZE, &fdset, 0, 0, &tv);
		if (iRetVal > 0)
		{
			bReceivedSomething = true;
			iRetVal = recvfrom(hUdpSocket, szTempBuffer, MTU, 0, (sockaddr*) &stAddress, &iSizeOfAddressStructure);

			szTempBuffer[iRetVal] = '\0';

			if (strncmp(szTempBuffer, "\xFF\xFF\xFF\xFFprint\n", 10) == 0)
			{
				memmove(szTempBuffer, szTempBuffer+10, strlen(szTempBuffer));
				iRetVal -= 10;
			}
			else
				return -5;

			sReturnBuffer->append(szTempBuffer);

			iReceivedBytes += iRetVal;
			if (iRetVal < 1200)
				break;
		}
		else if (iRetVal == 0)
			break;
		else if (iRetVal < 0)
			return -3;
	}

	if (!bReceivedSomething)
	{
		if (bOwnSocket)
		{
			closesocket(hUdpSocket);
		}
		return -4;
	}

	if (bOwnSocket)
		closesocket(hUdpSocket);

	return iReceivedBytes;
}

int iPlayerStructVectorFromAddress (std::string sIpAddress, int iPort, std::string sRconPassword,
								std::vector <PLAYER> * playervector, SOCKET hUdpSocket , double dTimeout)
{
	if (sRconPassword.compare("") == 0)
		return -10;

	playervector->clear();
	std::string sAnswer;
	
	//{ send and process answer to "sv players" (gives number, name, build, ID and OP)
	int iRetVal = iSendMessageToServer(sIpAddress, iPort, "sv players\0", &sAnswer, sRconPassword, hUdpSocket, dTimeout);
	if (iRetVal <= 0)
		return iRetVal;

	boost::smatch MatchResults;
	boost::regex rx;
	std::vector <std::string> vLines;
	
	std::size_t iPosition = 0;
    std::size_t iPrevious = 0;
    
    iPosition = sAnswer.find("\n");
    while (iPosition != std::string::npos)
    {
        vLines.push_back(sAnswer.substr(iPrevious, iPosition - iPrevious));
        iPrevious = iPosition + 1;
        iPosition = sAnswer.find("\n", iPrevious);
    }
    vLines.push_back(sAnswer.substr(iPrevious));
	
	PLAYER tempplayer;

	for (unsigned int i = 0; i < vLines.size(); i++)
	{		
		rx.assign("(\\d+) \\((\\d+)\\)] \\* OP (\\d+), (.*?) \\(b(\\d+)\\)");
		if (boost::regex_search(vLines.at(i), MatchResults, rx)) //Admin, logged in
		{
			vResetPlayer(&tempplayer);
			
			tempplayer.iNumber = atoi( std::string(MatchResults[1]).c_str() );
			tempplayer.iId     = atoi( std::string(MatchResults[2]).c_str() );
			tempplayer.iOp     = atoi( std::string(MatchResults[3]).c_str() );
			tempplayer.iBuild  = atoi( std::string(MatchResults[5]).c_str() );
			tempplayer.sName   = std::string(MatchResults[4]);
			
			playervector->push_back(tempplayer);
			continue;
		}
		
		rx.assign("(\\d+) \\((\\d+)\\)] \\* (.*?) \\(b(\\d+)\\)");
		if (boost::regex_search(vLines.at(i), MatchResults, rx)) //Player, logged in
		{
			vResetPlayer(&tempplayer);
	
			std::string sName (MatchResults[3]);
			
			tempplayer.iNumber = atoi( std::string(MatchResults[1]).c_str() );
			tempplayer.iId	   = atoi( std::string(MatchResults[2]).c_str() );
			tempplayer.iBuild  = atoi( std::string(MatchResults[4]).c_str() );
			tempplayer.sName   = sName;
			playervector->push_back(tempplayer);
			continue;
		}

		rx.assign("(\\d+) ] \\* OP (\\d+), (.*?) \\(b(\\d+)\\)");
		if (boost::regex_search(vLines.at(i), MatchResults, rx)) //Admin, not logged in
		{
			vResetPlayer(&tempplayer);
			
			tempplayer.iNumber = atoi( std::string (MatchResults[1]).c_str() );
			tempplayer.iOp     = atoi( std::string (MatchResults[2]).c_str() );
			tempplayer.iBuild  = atoi( std::string (MatchResults[4]).c_str() );
			tempplayer.sName   = std::string (MatchResults[3]);
			
			playervector->push_back(tempplayer);
			continue;
		}

		rx.assign("(\\d+) ] \\* (.*?) \\(b(\\d+)\\)");
		if (boost::regex_search(vLines.at(i), MatchResults, rx)) //Player, not logged in
		{
			vResetPlayer(&tempplayer);		
			
			std::string sName (MatchResults[2]);
			
			tempplayer.iNumber = atoi( std::string (MatchResults[1]).c_str() );
			tempplayer.iBuild  = atoi( std::string (MatchResults[3]).c_str() );
			tempplayer.sName   = sName;
			
			playervector->push_back(tempplayer);
			continue;
		}

		rx.assign("(\\d+) \\(bot\\)] \\* (.*?) \\(b0\\)");
		if (boost::regex_search(vLines.at(i), MatchResults, rx)) //Bot
		{
			vResetPlayer(&tempplayer);
			
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
	
	while (boost::regex_search(start, end, MatchResults, rx))
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
			catch (const std::out_of_range& oor)
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

	while (boost::regex_search(start, end, MatchResults, rx))
	{
		char * szFound;
		char * szNumbers;
		
		start = MatchResults[0].second;
		
		sColor.assign(MatchResults[1]);
		sNumbers.assign(MatchResults[2]);
		
		szNumbers = new char[sNumbers.length()+1];
		strcpy(szNumbers, sNumbers.c_str());
		
		szFound = strtok(szNumbers, "!");		
		while (szFound != NULL)
		{
			int iNum = atoi(szFound);
			try
			{
				playervector->at(iNum).cColor = *(sColor.c_str());
			}
			catch (const std::out_of_range& oor)
			{
				//May be that another player connected until now and there is no playervector->at(iNum), so if this occurs just ignore it.
				break;
			}
			szFound = strtok(NULL, "!");	
		}
		
		delete[] szNumbers;
	}
	//}
	return 1;
}