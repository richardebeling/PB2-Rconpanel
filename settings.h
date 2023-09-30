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

namespace Subitems {
static constexpr int NUMBER = 0;
static constexpr int NAME = 1;
static constexpr int BUILD = 2;
static constexpr int ID = 3;
static constexpr int OP = 4;
static constexpr int IP = 5;
static constexpr int SCORE = 6;
static constexpr int PING = 7;
};

struct Settings
{
	bool  bRunBanThread = false;			// signals for threads to exit
	bool  bLimitConsoleLineCount = false;	// Enable or disable the automatic line reduction
	float fTimeoutSecs = .5;				// timeout used for servers you have rcon access  to
	float fAllServersTimeoutSecs = 1;		// timeout used for servers that you  don't have rcon access to (maybe higher ping?)
	int   iBanCheckDelaySecs = 10;			// delay between checking the servers for banned players in seconds
	int   iMaxConsoleLineCount = 10000;		// maximum lines in the console edit
	int   iMaxPingMsecs = 0;				// maximum ping in ms, 0 = unlimited
	bool  bColorPings = false;				// in the listview, the background color of the pings will vary from green to red
	bool  bColorPlayers = true;				// in the listview, players will get their teamcolor as background
	bool  bDisableConsole = false;			// the lower part of the GUI (manual RCON communication) will not be shown.
	int iAutoReloadDelaySecs = 0;			// delay for auto-reloading, 0 = disabled
	std::string sServerlistAddress;			// address where the server list can be gotten in case it changes.
};