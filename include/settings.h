#ifndef __SETTINGS_H_INCLUDED
#define __SETTINGS_H_INCLUDED

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
	bool  bAutoKickCheckEnable = false;		// Whether automatick kicks are enabled
	int   iAutoKickCheckDelay = 10;			// delay between checking the servers for players to AutoKick
	int   iAutoKickCheckMaxPingMsecs = 0;	// maximum ping in ms, 0 = unlimited, to be used by AutoKick checks

	bool  bLimitConsoleLineCount = false;	// Enable or disable the automatic line reduction
	int   iMaxConsoleLineCount = 10000;		// maximum lines in the console edit

	int	  iAutoReloadDelaySecs = 0;			// delay for auto-reloading, 0 = disabled

	float fTimeoutSecs = .5;				// timeout used for servers you have rcon access  to
	// TODO: Unused, remove
	float fAllServersTimeoutSecs = 1;		// timeout used for servers that you  don't have rcon access to (maybe higher ping?)
	
	bool  bColorPings = false;				// in the listview, the background color of the pings will vary from green to red
	bool  bColorPlayers = true;				// in the listview, players will get their teamcolor as background
	bool  bDisableConsole = false;			// the lower part of the GUI (manual RCON communication) will not be shown.
	std::string sServerlistAddress = "http://dplogin.com/serverlist.php"; // address where the server list can be retrieved in case it changes.
};

#endif // __SETTINGS_H_INCLUDED