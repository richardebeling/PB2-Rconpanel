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

namespace SUBITEMS
{
	static const int iNumber = 0;
	static const int iName = 1;
	static const int iBuild = 2;
	static const int iId = 3;
	static const int iOp = 4;
	static const int iIp = 5;
	static const int iScore = 6;
	static const int iPing = 7;
}

namespace DEFAULTSETTINGS
{
	static const     bool  bRunAutoReloadThread   = false;
	static const     bool  bRunBanThread          = false;
	static const     bool  bLimitConsoleLineCount = true;
	static const     float fTimeoutSecs           = 0.5f;
	static const     float fAllServersTimeoutSecs = 1.0f;
	static const     int   iBanCheckDelaySecs     = 60;
	static const     int   iMaxConsoleLineCount   = 500;
	static const     int   iMaxPingMsecs          = 0;
	static const     bool  bColorPings            = false;
	static const     bool  bColorPlayers          = true;
	static const	 bool  bDisableConsole		  = false;
	static const     int   iAutoReloadDelaySecs   = 60;
	static const std::string sServerlistAddress	  = "http://www.dplogin.com/serverlist.php";
}

struct SETTINGS
{
	bool  bRunAutoReloadThread;   // signals for threads to exit.
	bool  bRunBanThread;          //
	bool  bLimitConsoleLineCount; // Enable or disable the automatic line reduction
	float fTimeoutSecs;           // timeout used for servers you have rcon access  to
	float fAllServersTimeoutSecs; // timeout used for servers that you  don't have rcon access to (maybe higher ping?)
	int   iBanCheckDelaySecs;     // delay between checking the servers for banned players in seconds
	int   iMaxConsoleLineCount;   // maximum lines in the console edit
	int   iMaxPingMsecs;          // maximum ping in ms, 0 = unlimited
	bool  bColorPings;            // in the listview, the background color of the pings will vary from green to red
	bool  bColorPlayers;          // in the listview, players will get their teamcolor as background
	bool  bDisableConsole;		  // the lower part of the GUI (manual RCON communication) will not be shown.
	int   iAutoReloadDelaySecs;   // delay for auto-reloading;
	std::string sServerlistAddress;	  // address where the server list can be gotten in case it changes.
};