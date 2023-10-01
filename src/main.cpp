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

//TODO: Add: Modifiable messages that will be said as console before players are kicked.
//TODO: Add: Dialog that can be used to change servers settings and save current settings as configuration file for a server
//TODO: IDs and IPs Dialog: Show information about these numbers (maybe a button that links to dplogin / ip whois?)

//TODO: Save and restore window position?

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#define strcasecmp _stricmp
#endif


#include "main.h"
#include "color.h"
#include "async_repeated_timer.h"
#include "version.h"
#include "settings.h"
#include "resource.h"

#include <thread>
#include <future>

using namespace std::string_literals;
using namespace std::chrono_literals;

std::vector<pb2lib::Player> g_vPlayers;			// players on the current server, shown in the listview

std::vector<pb2lib::Server> g_vSavedServers;	// used to store servers in the combo box
std::vector<pb2lib::Server> g_vAllServers;		// used to fill server list in the "Manage servers" dialog
std::vector<AutoKickEntry> 	g_vAutoKickEntries;

AsyncRepeatedTimer g_AutoReloadTimer;
AsyncRepeatedTimer g_AutoKickTimer;

HANDLE g_hSendRconThread    = 0; // TODO

std::map <size_t, HANDLE> g_mLoadServersThreads;	//contains UID and ExitEvent for every reload thread

UINT WM_REFETCHPLAYERS;
UINT WM_SERVERCHANGED;
UINT WM_PLAYERSREADY;
UINT WM_SERVERCVARSREADY;

WindowHandles gWindows; // stores all window handles
Settings gSettings;     // stores all program settings

ULONG_PTR g_gdiplusStartupToken;
std::unique_ptr<Gdiplus::Bitmap> g_pMapshotBitmap;


//--------------------------------------------------------------------------------------------------
// Program Entry Point                                                                             |
//{-------------------------------------------------------------------------------------------------

#pragma warning (suppress : 28251)
int WINAPI WinMain (HINSTANCE hThisInstance, HINSTANCE hPrevInstance, PSTR lpszArgument, int nCmdShow)
{
	INITCOMMONCONTROLSEX icex = { 0 }; // needed for list view control
	icex.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);

	WM_REFETCHPLAYERS = RegisterWindowMessageOrCriticalError("RCONPANEL_REFETCHPLAYERS");
	WM_PLAYERSREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_PLAYERSREADY");
	WM_SERVERCHANGED = RegisterWindowMessageOrCriticalError("RCONPANEL_SERVERCHANGED");
	WM_SERVERCVARSREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_SERVERCVARSREADY");
	
	if (OleInitialize(NULL) != S_OK) {
		HandleCriticalError("OleInitialize returned non-ok status");
	}
	
	Gdiplus::GdiplusStartupInput gsi;
	Gdiplus::GdiplusStartup(&g_gdiplusStartupToken, &gsi, NULL);

	const char* classname = "Rconpanel";
	WNDCLASSEX wincl = { 0 };
	wincl.hInstance = hThisInstance;
	wincl.lpszClassName = classname;
	wincl.lpfnWndProc = WindowProcedure;
	wincl.style = CS_DBLCLKS;
	wincl.cbSize = sizeof (WNDCLASSEX);
	wincl.hIcon = LoadIcon (hThisInstance, MAKEINTRESOURCE(IDA_APP_ICON));
	wincl.hIconSm = LoadIcon (hThisInstance, MAKEINTRESOURCE(IDA_APP_ICON));
	wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
	wincl.lpszMenuName = NULL;
	wincl.cbClsExtra = 0;
	wincl.cbWndExtra = 0;
	wincl.hbrBackground = (HBRUSH) (COLOR_WINDOW);

	if (!RegisterClassEx (&wincl)) {
		HandleCriticalError("Could not register window class");
	}

	DWORD dwBaseUnits = GetDialogBaseUnits();
	CreateWindowEx (0, classname, "DP:PB2 Rconpanel",
					WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
					MulDiv(285, LOWORD(dwBaseUnits), 4), MulDiv(290, HIWORD(dwBaseUnits), 8),
					HWND_DESKTOP, LoadMenu (hThisInstance, MAKEINTRESOURCE(IDM)),
					hThisInstance, NULL);
	// All other windows will be created in OnMainWindowCreate

	ShowWindow(gWindows.hWinMain, nCmdShow);

	MSG messages;
	while (GetMessage(&messages, NULL, 0, 0))
	{
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}
	return 0;
}

//}-------------------------------------------------------------------------------------------------
// Main Window Functions                                                                           |
//{-------------------------------------------------------------------------------------------------

// TODO: Namespacing instead of function name prefixing

void MainWindowUpdateAutoKickState() noexcept {
	HMENU menu = GetMenu(gWindows.hWinMain);

	CheckMenuItem(menu, IDM_AUTOKICK_ENABLE, gSettings.bAutoKickCheckEnable ? MF_CHECKED : MF_UNCHECKED);
	g_AutoKickTimer.set_interval(gSettings.bAutoKickCheckEnable ? gSettings.iAutoKickCheckDelay : 0);

	CheckMenuItem(menu, IDM_AUTOKICK_SETPING, gSettings.iAutoKickCheckMaxPingMsecs != 0 ? MF_CHECKED : MF_UNCHECKED);
}

pb2lib::Server* MainWindowGetSelectedServerOrLoggedNull() noexcept {
	if (g_vSavedServers.size() == 0) {
		MainWindowWriteConsole("There are no servers in your server list.");
		return nullptr;
	}

	size_t selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR || selectedServerIndex >= g_vSavedServers.size()) {
		MainWindowWriteConsole("Error when trying to get the selected server");
		return nullptr;
	}

	return &g_vSavedServers.at(selectedServerIndex);
}

pb2lib::Player* MainWindowGetSelectedPlayerOrNull() noexcept {
	auto iSelectedItem = ListView_GetNextItem(gWindows.hListPlayers, -1, LVNI_SELECTED);
	if (iSelectedItem == -1) {
		return nullptr;
	}

	LVITEM item = { 0 };
	item.iItem = iSelectedItem;
	ListView_GetItem(gWindows.hListPlayers, &item);
	size_t stored_index = item.lParam;

	return &g_vPlayers.at(stored_index);
}

pb2lib::Player* MainWindowGetSelectedPlayerOrLoggedNull() noexcept {
	auto result = MainWindowGetSelectedPlayerOrNull();
	if (!result) {
		MainWindowWriteConsole("Please select a player first.");
	}
	return result;
}

// TODO: additional string argument describing the attempted action
template <typename FuncT>
auto MainWindowLogPb2LibExceptionsToConsole(FuncT&& functor) {
	try {
		functor();
	}
	catch (pb2lib::Exception& e) {
		MainWindowWriteConsole("An error occurred: "s + e.what());
	}
}

void ShowPlayerInfo(HWND hwnd)
{
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!player) {
		return;
	}

	SetWindowText(hwnd, "DP:PB2 Rconpanel - Retrieving Information...");
	std::string sBoxContent = "Information about player " + player->name;
	sBoxContent += "\r\n\r\n";
	sBoxContent += "DPLogin Profile:\r\n";

	if (player->id)
	{
		std::string sPlayersite = GetHttpResponse("http://www.dplogin.com/index.php?action=viewmember&playerid=" + std::to_string(player->id.value()));

		std::smatch matches;
		std::regex rx ("<tr><td><b class=\"faqtitle\">(.+?:)</b></td><td>(.+?)</td></tr>");
		//												1-VARNAME		2-CONTENT
				
		auto start = sPlayersite.cbegin();
		while (std::regex_search(start, sPlayersite.cend(), matches, rx)) {
			start = matches[0].second;
			
			sBoxContent += matches[1];
			sBoxContent += " ";
			
			std::string sContent = matches[2];
			sContent = std::regex_replace(sContent, std::regex(R"(<a href=\".+?\">)"), "");
			sContent = std::regex_replace(sContent, std::regex("</a>"), "");

			sContent = std::regex_replace(sContent, std::regex("&gt;"), ">");
			sContent = std::regex_replace(sContent, std::regex("&lt;"), "<");
			sContent = std::regex_replace(sContent, std::regex("&quot;"), "\"");
			
			sBoxContent += sContent + "\r\n";
		}
		sBoxContent += "\r\n";
	}

	if (player->address)
	{
		std::string sIpApiResponse = GetHttpResponse(
			"http://ip-api.com/line/"
			+ player->address.value().ip
			+ "?fields=continent,country,regionName,city,district,zip,isp,org,as,proxy,hosting");

		auto linesView = sIpApiResponse
			| std::ranges::views::split('\n')
			| std::ranges::views::transform([](auto&& rng) { return std::string_view(rng.begin(), rng.end()); });

		std::vector<std::string> lines(linesView.begin(), linesView.end());
		lines.resize(11); // cheap way to prevent oor-access
		
		sBoxContent += "IP: " + player->address.value().ip + " (data from ip-api.com)\r\n";
		sBoxContent += "ISP: " + lines[6] + "\r\n";
		sBoxContent += "Organization: " + lines[7] + "\r\n";
		sBoxContent += "AS: " + lines[8] + "\r\n";
		sBoxContent += "Is proxy: " + lines[9] + "\r\n";
		sBoxContent += "Is hosting: " + lines[10] + "\r\n";
		sBoxContent += "\r\n";
		sBoxContent += "Continent: " + lines[0] + "\r\n";
		sBoxContent += "Country: " + lines[1] + "\r\n";
		sBoxContent += "Region: " + lines[2] + "\r\n";
		sBoxContent += "City: " + lines[3] + "\r\n";
		sBoxContent += "District: " + lines[4] + "\r\n";
		sBoxContent += "Zip code: " + lines[5] + "\r\n";
	}

	SetWindowText(hwnd, "DP:PB2 Rconpanel");
	MessageBox(hwnd, sBoxContent.c_str(), "Information about player", MB_ICONINFORMATION | MB_OK);
}

void ShowAboutDialog(HWND hwnd)
{
	std::string sTitle = "About - DP:PB2 Rconpanel V" + std::to_string(AutoVersion::MAJOR)
						+ "." + std::to_string(AutoVersion::MINOR) + "." + std::to_string(AutoVersion::BUILD);

	MessageBox(hwnd,"Remote administration tool for Digital Paint: Paintball 2 servers."
					"The source code is released under GPLv3 here:\r\n"
					"https://github.com/richardebeling/PB2-Rconpanel\r\n"
					"If there are any questions, feel free to contact me (issue, email, discord, ...).",
					sTitle.c_str(),
					MB_OK | MB_ICONINFORMATION);
}

inline void SignalAllThreads(std::map<size_t, HANDLE> * map)
{
	for (auto& [key, event] : *map) {
		SetEvent(event);
	}
}

void MainWindowUpdatePlayersListview() noexcept
{
	ListView_DeleteAllItems(gWindows.hListPlayers);

	LVITEM LvItem = { 0 };
	for(unsigned int playerIndex = 0; playerIndex < g_vPlayers.size(); playerIndex++)
	{
		const pb2lib::Player& player = g_vPlayers[playerIndex];
		LvItem.mask = LVIF_TEXT | LVIF_PARAM;
		LvItem.iItem = playerIndex;
		LvItem.lParam = playerIndex;
		LvItem.iSubItem = Subitems::NUMBER;
		std::string itemText = std::to_string(player.number);
		LvItem.pszText = (LPSTR)itemText.c_str();
		ListView_InsertItem(gWindows.hListPlayers, &LvItem);

		for (int column = 1; column < 8; column++)
		{
			itemText = [&](){
				switch (column) {
					case Subitems::NAME: return player.name;
					case Subitems::BUILD: return std::to_string(player.build);
					case Subitems::ID: return player.id ? std::to_string(*player.id) : "-";
					case Subitems::OP: return player.op ? std::to_string(player.op) : "-";
					case Subitems::IP: return player.address ? player.address->ip : "";
					case Subitems::PING: return player.ping ? std::to_string(*player.ping) : "";
					case Subitems::SCORE: return player.score ? std::to_string(*player.score) : "";
				}
				assert(false);
				return ""s;
			}();

			LvItem.mask = LVIF_TEXT;
			LvItem.iSubItem = column;
			LvItem.pszText = (LPSTR)itemText.c_str();
			ListView_SetItem(gWindows.hListPlayers, &LvItem);
		}
	}
	ListView_SortItems(gWindows.hListPlayers, OnMainWindowListViewSort, 0);
	ListView_SetColumnWidth(gWindows.hListPlayers, 7, LVSCW_AUTOSIZE_USEHEADER);
}

void MainWindowWriteConsole(const std::string_view str) // prints text to gWindows.hEditConsole, adds timestamp and linebreak
{
	// may be called by multiple threads, shouldn't intermingle output
	static std::mutex mutex;
	std::lock_guard guard(mutex);

	// TODO: Sometimes flickers, loses focus of players listview. Can we do better?
	auto const time = std::chrono::time_point_cast<std::chrono::seconds>(
		std::chrono::current_zone()->to_local(std::chrono::system_clock::now())
	);
	std::string formatted = std::format("[{:%H:%M:%S}] {}", time, str); 

	if (formatted.ends_with('\n')) {
		formatted = formatted.substr(0, formatted.find_last_not_of('\n') + 1); // remove trailing newlines
	}
	formatted = std::regex_replace(formatted, std::regex{"\n"}, "\n---------> "); // indent text after line ending
	formatted = std::regex_replace(formatted, std::regex{"\n"}, "\r\n");

	// add linebreak (if its not the first line) and the the text to the end of gWindows.hEditConsole
	SendMessage(gWindows.hEditConsole, EM_SETSEL, -2, -2);
	DWORD start = 0, end = 0;
	SendMessage(gWindows.hEditConsole, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
	if (start != 0)
		SendMessage(gWindows.hEditConsole, EM_REPLACESEL, 0, (LPARAM)"\r\n");

	// Add new text
	SendMessage(gWindows.hEditConsole, EM_REPLACESEL, 0, (LPARAM)formatted.c_str());

	//remove first line until linecount is equal to gSettings.iMaxConsoleLineCount
	if (gSettings.bLimitConsoleLineCount)
		Edit_ReduceLines(gWindows.hEditConsole, gSettings.iMaxConsoleLineCount);

	//Scroll to the bottom of gWindows.hEditConsole so the user directly sees what has just been added
	Edit_ScrollToEnd(gWindows.hEditConsole);
}

//}-------------------------------------------------------------------------------------------------
// Callback Main Window                                                                            |
//{-------------------------------------------------------------------------------------------------

static int OnPlayerListCustomDraw (LPARAM lParam)
{
	LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW) lParam;

	switch (lplvcd->nmcd.dwDrawStage)
    {
	case CDDS_PREPAINT:
		return CDRF_NOTIFYITEMDRAW;
	
	case CDDS_ITEMPREPAINT:
		return CDRF_NOTIFYSUBITEMDRAW;
	
	case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		pb2lib::Player & player = g_vPlayers.at(lplvcd->nmcd.lItemlParam);
		switch(lplvcd->iSubItem)
		{
		case Subitems::NUMBER:
		case Subitems::NAME:
		case Subitems::BUILD:
		case Subitems::ID:
		case Subitems::OP:
		case Subitems::IP:
		case Subitems::SCORE:
			if (!gSettings.bColorPlayers) {
				lplvcd->clrTextBk = Color::WHITE;
			}
			
			lplvcd->clrTextBk = Color::from_team(player.team.value_or(pb2lib::Team::OBSERVER));
			return CDRF_NEWFONT;
		
		case Subitems::PING:
			lplvcd->clrTextBk = Color::WHITE;

			if (gSettings.bColorPings) {
				if (player.ping) { // ping == nullopt when bot or attribution failure
					lplvcd->clrTextBk = Color::from_ping(*player.ping);
				}
			}
			else if (gSettings.bColorPlayers) {
				lplvcd->clrTextBk = Color::from_team(player.team.value_or(pb2lib::Team::OBSERVER));
			}
			return CDRF_NEWFONT;
			
		default:
			lplvcd->clrTextBk = Color::WHITE;
			return CDRF_NEWFONT;
		}
	}
    return CDRF_DODEFAULT;
}

void PostMessageToAllWindows(UINT message) {

	WNDENUMPROC enum_callback = [](HWND hwnd, LPARAM lParam) -> BOOL {
		PostMessage(hwnd, (UINT)lParam, 0, 0);
		return TRUE;
	};

	EnumWindows(enum_callback, (LPARAM)message);
}

int CALLBACK OnMainWindowListViewSort(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) //returns order of 2 items
{
	pb2lib::Player& lhs = g_vPlayers[lParam1];
	pb2lib::Player& rhs = g_vPlayers[lParam2];
	switch(lParamSort)
	{
	case Subitems::NUMBER: return lhs.number - rhs.number;
	case Subitems::NAME: return strcasecmp(lhs.name.c_str(), rhs.name.c_str());
	case Subitems::BUILD: return lhs.build - rhs.build;
	case Subitems::ID: return lhs.id.value_or(0) - rhs.id.value_or(0);
	case Subitems::OP: return lhs.op - rhs.op;
	case Subitems::PING: return lhs.ping.value_or(0) - rhs.ping.value_or(0);
	case Subitems::SCORE: return lhs.score.value_or(0) - rhs.score.value_or(0);
	case Subitems::IP: {
			std::string left_ip = lhs.address ? lhs.address->ip : "";
			std::string right_ip = rhs.address ? rhs.address->ip : "";
			return strcasecmp(left_ip.c_str(), right_ip.c_str());
		}
	}
	return 0;
}

BOOL OnMainWindowCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
	g_AutoReloadTimer.set_trigger_action([hwnd]() { PostMessage(hwnd, WM_REFETCHPLAYERS, 0, 0); });
	g_AutoKickTimer.set_trigger_action(AutoKickTimerFunction);

	//{ Create Controls
	DWORD dwBaseUnits = GetDialogBaseUnits();

	gWindows.hWinMain = hwnd;

	HWND hStaticServer = CreateWindowEx(0, "STATIC", "Server: ",
						SS_SIMPLE | WS_CHILD | WS_VISIBLE,
						MulDiv(3  , LOWORD(dwBaseUnits), 4), // Units to Pixel
						MulDiv(4  , HIWORD(dwBaseUnits), 8),
						MulDiv(30 , LOWORD(dwBaseUnits), 4),
						MulDiv(8  , HIWORD(dwBaseUnits), 8),
						hwnd, NULL, NULL, NULL);

	//The following controls will be resized when the window is shown and HandleResize is called.
	gWindows.hComboServer = CreateWindowEx(WS_EX_CLIENTEDGE, "COMBOBOX", "",
						CBS_DROPDOWNLIST | CBS_SORT | WS_CHILD | WS_VISIBLE,
						0, 0, 0, CW_USEDEFAULT,	//automatically adapt to content
						hwnd, NULL, NULL, NULL);

	gWindows.hStaticServerInfo = CreateWindowEx(0, WC_STATIC, "",
						SS_LEFTNOWORDWRAP | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hListPlayers = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_RIGHTSCROLLBAR, WC_LISTVIEW, "",
						LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonJoin = CreateWindowEx(0, WC_BUTTON, "&Join", WS_CHILD | WS_VISIBLE , 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonReload = CreateWindowEx(0, WC_BUTTON, "&Reload", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonKick = CreateWindowEx(0, WC_BUTTON, "&Kick", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonAutoKick = CreateWindowEx(0, WC_BUTTON, "&AutoKick", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonBanIP = CreateWindowEx(0, WC_BUTTON, "&Ban IP", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonDPLoginProfile = CreateWindowEx(0, WC_BUTTON, "&DPLogin Profile", WS_CHILD | WS_VISIBLE,
						0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonWhois = CreateWindowEx(0, WC_BUTTON, "&Whois IP", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonForcejoin = CreateWindowEx(0, WC_BUTTON, "&Forcejoin", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hComboRcon = CreateWindowEx(WS_EX_CLIENTEDGE, WC_COMBOBOX, "",
						CBS_AUTOHSCROLL | CBS_SIMPLE | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonSend = CreateWindowEx(0, WC_BUTTON, "&Send Rcon", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hEditConsole = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, "",
						WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_READONLY | WS_CHILD | WS_VISIBLE,
						0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	//}

	HDC hdc = GetDC(NULL);
	LONG lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(NULL, hdc);

	static DeleteObjectRAIIWrapper<HFONT> font(CreateFont(lfHeight, 0, 0, 0, 0, FALSE, 0, 0, 0, 0, 0, 0, 0, "MS Shell Dlg"));
	static DeleteObjectRAIIWrapper<HFONT> consoleFont(CreateFont(lfHeight, 0, 0, 0, 0, FALSE, 0, 0, 0, 0, 0, 0, 0, "Courier New"));

	WPARAM fontWparam = (WPARAM)(HFONT)font;
	WPARAM fontConsoleWparam = (WPARAM)(HFONT)consoleFont;

	SendMessage(hStaticServer				  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hStaticServerInfo	  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hComboServer		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hListPlayers		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonJoin		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonKick		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonAutoKick	  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonBanIP		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonReload		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonDPLoginProfile, WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonWhois		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonForcejoin	  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hComboRcon			  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hButtonSend		  , WM_SETFONT, fontWparam, true);
	SendMessage(gWindows.hEditConsole		  , WM_SETFONT, fontConsoleWparam, true);
	
	SendMessage(gWindows.hEditConsole		  , EM_SETLIMITTEXT, WPARAM(0), LPARAM(0));

	LVCOLUMN lvc = { 0 };
	char szText[32] = { 0 }; //maximum: "Build\0"
	lvc.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
	for (int i = 0; i <= 7; i++)
	{
		lvc.iSubItem = i;
		lvc.pszText = szText;
		lvc.fmt = LVCFMT_RIGHT;
		switch (i)
		{
			case Subitems::NUMBER: strcpy(szText, "Num");   break;
			case Subitems::NAME:   strcpy(szText, "Name");  lvc.fmt = LVCFMT_LEFT; break;
			case Subitems::BUILD:  strcpy(szText, "Build"); break;
			case Subitems::ID:     strcpy(szText, "ID");    break;
			case Subitems::OP:     strcpy(szText, "OP");    break;
			case Subitems::IP:     strcpy(szText, "IP");    lvc.fmt = LVCFMT_LEFT; break;
			case Subitems::PING:   strcpy(szText, "Ping");  break;
			case Subitems::SCORE:  strcpy(szText, "Score"); break;
		}
		ListView_InsertColumn(gWindows.hListPlayers, i, &lvc);
	}
	SendMessage(gWindows.hListPlayers, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	MainWindowWriteConsole("DP:PB2 Rconpanel started, console initialized");
	MainWindowWriteConsole("Loading configuration file...");

	int retVal = LoadConfig();
	
	if (retVal == -1)
		MainWindowWriteConsole("No configuration file found, the program will save it's settings when you close it.");
	else if (retVal == -2)
		MainWindowWriteConsole("Error while reading AutoKick entries from config file.");
	else if (retVal == 1)
		MainWindowWriteConsole("Success.");
	else
		MainWindowWriteConsole("!!! Unexpected error when loading the configuration file: " + std::to_string(retVal) + " !!!");
	
	SendMessage(hwnd, WM_SERVERCHANGED, 0, 0);

	return true;
	//Will make the window procedure return 0 because the message cracker changes the return value:
	//#define HANDLE_WM_CREATE(hwnd,wParam,lParam,fn) (LRESULT)((fn)((hwnd),(LPCREATESTRUCT)(lParam)) ? 0 : -1)
}

void OnMainWindowForcejoin(void)
{
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!server || !player) {
		return;
	}

	auto iSelectedColor = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_FORCEJOIN), gWindows.hWinMain, (DLGPROC) ForcejoinDlgProc);
	if (iSelectedColor == -1) {
		return;
	}

	MainWindowLogPb2LibExceptionsToConsole([&]() {
		auto updated_players = pb2lib::get_players_from_rcon_sv_players(server->address, server->rcon_password, gSettings.fTimeoutSecs);
		auto matching_updated_player_it = std::ranges::find_if(updated_players, [&](const pb2lib::Player& updated_player) {
			return updated_player.number == player->number && updated_player.name == player->name; });

		if (matching_updated_player_it == updated_players.end()) {
			MainWindowWriteConsole("It seems like the player disconnected. They were not forcejoined.");
			return;
		}

		const std::string message = "sv forcejoin " + std::to_string(player->number) + " " + (char)iSelectedColor;
		pb2lib::send_rcon(server->address, server->rcon_password, message, gSettings.fTimeoutSecs);
		MainWindowWriteConsole("Player was force-joined successfully.");
	});
}

void OnMainWindowSendRcon(void)
{
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	int buffer_size = GetWindowTextLength(gWindows.hComboRcon) + 1;
	std::vector<char> command_buffer(buffer_size);
	GetWindowText(gWindows.hComboRcon, command_buffer.data(), buffer_size);

	MainWindowLogPb2LibExceptionsToConsole([&]() {
		std::string answer = pb2lib::send_rcon(server->address, server->rcon_password, command_buffer.data(), gSettings.fTimeoutSecs);
		MainWindowWriteConsole("Got answer from Server:\r\n" + answer);
	});
}

void OnMainWindowJoinServer(void)
{
	const auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	const std::optional<std::string> pb2_path = GetPb2InstallPath();
	if (!pb2_path) {
		MainWindowWriteConsole("Could not find the path of your DP:PB2 install directory in the registry.");
		return;
	}

	const std::string args = "+connect " + server->address.ip + ":" + std::to_string(server->address.port);
	const std::string pb2_executable = pb2_path.value() + "\\paintball2.exe";

	auto ret = (INT_PTR) ShellExecute(0, "open", pb2_executable.c_str(), args.c_str(), 0, 1); //start it
	if (ret <= 32) {
		MainWindowWriteConsole("Error while starting:\r\n" + pb2_executable + "\r\nShellExecute returned: " + std::to_string(ret));
	}
}

void OnMainWindowOpenWhois(void)
{
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!player) {
		return;
	}

	if (!player->address) {
		MainWindowWriteConsole("The player's IP was not loaded correctly. Please reload.");
		return;
	}

	std::string sUrl = "https://www.utrace.me/?query=" + player->address->ip;
	ShellExecute(0, "open", sUrl.c_str(), 0, 0, 1);
}

void OnMainWindowOpenDPLogin(void)
{
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!player) {
		return;
	}

	std::string url = "http://dplogin.com/index.php?action=displaymembers&search=" + player->name;
	if (player->id) {
		url = "http://dplogin.com/index.php?action=viewmember&playerid=" + std::to_string(*player->id);
	}

	ShellExecute(0, "open", url.c_str(), 0, 0, 1);
}

void OnMainWindowKickPlayer(void)
{
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!player || !server) {
		return;
	}

	MainWindowLogPb2LibExceptionsToConsole([&]() {
		auto updated_players = pb2lib::get_players_from_rcon_sv_players(server->address, server->rcon_password, gSettings.fTimeoutSecs);
		auto matching_updated_player_it = std::ranges::find_if(updated_players, [&](const pb2lib::Player& updated_player) {
			return updated_player.number == player->number && updated_player.name == player->name; });

		if (matching_updated_player_it == updated_players.end()) {
			MainWindowWriteConsole("It seems like the player disconnected. They were not kicked.");
			return;
		}

		const std::string message = "kick " + std::to_string(player->number);
		pb2lib::send_rcon(server->address, server->rcon_password, message, gSettings.fTimeoutSecs);
		MainWindowWriteConsole("Player was kicked successfully.");
	});
}

void OnMainWindowBanIP(void)
{
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!server || !player) {
		return;
	}

	// TODO: Maybe instead just craft the command and act as if user pressed the send_rcon button?

	if (!player->address) {
		MainWindowWriteConsole("The player's IP was not loaded correctly. Please reload.");
		return;
	}

	std::string command = "sv addip " + player->address->ip;

	MainWindowLogPb2LibExceptionsToConsole([&]() {
		pb2lib::send_rcon(server->address, server->rcon_password, command, gSettings.fTimeoutSecs);
		MainWindowWriteConsole("The IP " + player->address->ip + " was added to the server's ban list.");
	});
}

void OnMainWindowAutoKick(void)
{
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!player) {
		return;
	}

	if (player->id) {
		g_vAutoKickEntries.emplace_back(AutoKickEntry::Type::ID, std::to_string(*player->id));
		MainWindowWriteConsole("AutoKick entry added for ID " + std::to_string(*player->id));
	}
	else {
		g_vAutoKickEntries.emplace_back(AutoKickEntry::Type::NAME, player->name);
		MainWindowWriteConsole("AutoKick entry added for name " + player->name);
	}
}

void OnMainWindowDestroy(HWND hwnd)
{
	SaveConfig();

	HANDLE rHandles[1] = {g_hSendRconThread};
	WaitForMultipleObjects(1, rHandles, TRUE, 10000);
	
	// TODO -- remove these? Threads should cooperate.
	if (g_hSendRconThread != INVALID_HANDLE_VALUE)
	{
		TerminateThread(g_hSendRconThread, 0);
		CloseHandle(g_hSendRconThread);
	}

	OleUninitialize();
	
	g_pMapshotBitmap.reset();
	Gdiplus::GdiplusShutdown(g_gdiplusStartupToken);
	
	PostQuitMessage(0);
}

void OnMainWindowCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (codeNotify)
	{
		case BN_CLICKED:
		{
			if (hwndCtl == gWindows.hButtonSend)
			{
				if (g_hSendRconThread == INVALID_HANDLE_VALUE || WaitForSingleObject(g_hSendRconThread, 0) != WAIT_TIMEOUT)
				{
					CloseHandle(g_hSendRconThread);
					g_hSendRconThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) OnMainWindowSendRcon, NULL, 0, NULL);
				}
				else
					MainWindowWriteConsole("Please wait for the server to answer to your last request");
			}
			if (hwndCtl == gWindows.hButtonReload) PostMessage(hwnd, WM_REFETCHPLAYERS, 0, 0);
			if (hwndCtl == gWindows.hButtonKick) OnMainWindowKickPlayer();
			if (hwndCtl == gWindows.hButtonAutoKick) OnMainWindowAutoKick();
			if (hwndCtl == gWindows.hButtonBanIP) OnMainWindowBanIP();
			if (hwndCtl == gWindows.hButtonDPLoginProfile) OnMainWindowOpenDPLogin();
			if (hwndCtl == gWindows.hButtonWhois) OnMainWindowOpenWhois();
			if (hwndCtl == gWindows.hButtonForcejoin) OnMainWindowForcejoin();
			if (hwndCtl == gWindows.hButtonJoin) OnMainWindowJoinServer();
			break;
		}
	
		case CBN_SELENDOK:
		{
			if (hwndCtl == gWindows.hComboRcon)
			{
				if (g_hSendRconThread == INVALID_HANDLE_VALUE || WaitForSingleObject(g_hSendRconThread, 0) != WAIT_TIMEOUT)
				{
					CloseHandle(g_hSendRconThread);
					g_hSendRconThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) OnMainWindowSendRcon, NULL, 0, NULL);
				}
				else
					MainWindowWriteConsole ("Please wait for the server to answer to your last request");
			}
			else if (hwndCtl == gWindows.hComboServer) {
				PostMessageToAllWindows(WM_SERVERCHANGED);
			}
			break;
		}
	}
			
	
	switch (id)
	{
		case IDM_FILE_EXIT:
			SendMessage(hwnd, WM_CLOSE, 0, 0);
			break;
		case IDM_FILE_SETTINGS:
			if (!gWindows.hDlgSettings)
				gWindows.hDlgSettings = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PROGRAMSETTINGS), hwnd, (DLGPROC) ProgramSettingsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgSettings);
			break;
		case IDM_FILE_REMOVECONFIG:
			{
				int iResult = MessageBoxA(gWindows.hWinMain, "This will delete every information "
										"stored in this program, including server IPs, ports and "
										"passwords as well as AutoKick entries.\n"
										"Are you sure you want to continue?",
										"Are you sure?",
										MB_ICONQUESTION | MB_YESNO);
				if (iResult == IDYES) DeleteConfig();
				break;
			}
		case IDM_SERVER_MANAGE:
			DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGESERVERS), hwnd, (DLGPROC) ManageServersDlgProc);
			break;
		case IDM_SERVER_ROTATION:
			//Must be forced to only be open one time because it uses two global variables which does not work when the dialog is open two times.
			//g_pMapshotBitmap and g_pMapshotBitmapResized
			if (!gWindows.hDlgManageRotation)
				gWindows.hDlgManageRotation = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGEROTATION), hwnd, (DLGPROC) ManageRotationDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgManageRotation);			
			break;
		case IDM_SERVER_BANNEDIPS:
			if (!gWindows.hDlgManageIps)
				gWindows.hDlgManageIps = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGEIPS), hwnd, (DLGPROC)ManageIPsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgManageIps);
			break;
		case IDM_AUTOKICK_ENABLE: {
			gSettings.bAutoKickCheckEnable = GetMenuState(GetMenu(gWindows.hWinMain), IDM_AUTOKICK_ENABLE, MF_BYCOMMAND) != SW_SHOWNA;
			MainWindowUpdateAutoKickState();
			break;
		}
		case IDM_AUTOKICK_SETPING:
			DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETPING), hwnd, (DLGPROC) SetPingDlgProc);
			break;
		case IDM_AUTOKICK_MANAGEIDS:
			if (!gWindows.hDlgManageIds)
				gWindows.hDlgManageIds = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGEIDS), hwnd, (DLGPROC) ManageIDsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgManageIds);
			break;
		case IDM_HELP_DPLOGIN:
			ShellExecute(NULL, "open", "http://www.DPLogin.com", NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_HELP_RCONCOMMANDS:
			if (!gWindows.hDlgRconCommands)
				gWindows.hDlgRconCommands = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_RCONCOMMANDS), hwnd, (DLGPROC) RCONCommandsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgRconCommands);
			break;
		case IDM_HELP_SERVERBROWSER:
			StartServerbrowser(); break;
		case IDM_HELP_ABOUT:
			ShowAboutDialog(hwnd); break;
	}

	FORWARD_WM_COMMAND(hwnd, id, hwndCtl, codeNotify, DefWindowProc);
}

int OnMainWindowNotify(HWND hwnd, int id, NMHDR* nmh)
{
    if (nmh->hwndFrom == gWindows.hListPlayers) {
		auto* player = MainWindowGetSelectedPlayerOrNull();
		switch (nmh->code) {			
			case NM_DBLCLK:
			{
				ShowPlayerInfo(hwnd);
				break;
			}
			
			case LVN_COLUMNCLICK:
			{
				NMLISTVIEW* pNmListview = (NMLISTVIEW*)nmh;
				ListView_SortItems(gWindows.hListPlayers, OnMainWindowListViewSort, pNmListview->iSubItem);
				FORWARD_WM_NOTIFY(hwnd, id, nmh, DefWindowProc);
				break;
			}
			
			case NM_RCLICK:
			{
				if (player && player->address) {
					SetClipboardContent(player->address->ip);
					MainWindowWriteConsole("IP was copied to clipboard.");
				}			
				break;
			}
		
			case NM_CUSTOMDRAW:
			{
				return OnPlayerListCustomDraw((LPARAM) nmh);
			}
		}
	}
	
	FORWARD_WM_NOTIFY(hwnd, id, nmh, DefWindowProc);
	return 0;
}

void OnMainWindowSize(HWND hwnd, UINT state, int cx, int cy)
{
	DWORD dwBaseUnits = GetDialogBaseUnits();
    int iMW = LOWORD(dwBaseUnits) / 4; //Multiplier width for base units -> pixels
	int iMH = HIWORD(dwBaseUnits) / 8; //Multiplier height for base units -> pixels

	// TODO: Fix clipping with DPI scaling enabled

    // TODO: Add: calculate it all from a few, for humans readable areas (Server, Player, Console) so editing is easier.
    //RECT rcServer =  {3*iMW, 3*iMH                   , cx - 3*iMW, 23*iMH};
    //RECT rcPlayers = {3*iMW, rcServer.bottom + 2*iMH , cx - 3*iMW, ((cy > 244*iMH) ? cy/2-20*iMH : 102*iMH) + 25*iMH};
    //RECT rcConsole = {3*iMW, rcPlayers.bottom + 2*iMH, cx - 3*iMW, cy - 3*iMH};
    //printf ("Server : left: %d; top: %d; right: %d; bottom: %d\n", rcServer.left, rcServer.top, rcServer.right, rcServer.bottom);
    //printf ("Player : left: %d; top: %d; right: %d; bottom: %d\n", rcPlayers.left, rcPlayers.top, rcPlayers.right, rcPlayers.bottom);
    //printf ("Console: left: %d; top: %d; right: %d; bottom: %d\n", rcConsole.left, rcConsole.top, rcConsole.right, rcConsole.bottom);

	MoveWindow(gWindows.hComboServer	 , 24*iMW, 3  *iMH, cx - 71*iMW, 8*iMH, FALSE);
	MoveWindow(gWindows.hStaticServerInfo, 24*iMW, 15 *iMH, cx - 71*iMW, 8*iMH, FALSE);
	
	if (gSettings.bDisableConsole)
	{
		ShowWindow(gWindows.hComboRcon,   SW_HIDE);
		ShowWindow(gWindows.hButtonSend,  SW_HIDE);
		ShowWindow(gWindows.hEditConsole, SW_HIDE);
		
		MoveWindow(gWindows.hListPlayers, 3*iMW, 25*iMH, cx - 50*iMW, cy - 28*iMH, FALSE);
	}
	else
	{
		if (cy > 244*iMH) //if window is big enough
		{
			MoveWindow(gWindows.hListPlayers, 3 *iMW	 , 25 *iMH	  , cx - 50*iMW, cy/2-20*iMH, FALSE); //resize listview and console
			MoveWindow(gWindows.hComboRcon  , 3 *iMW	 , cy/2+10*iMH, cx - 50*iMW, 10*iMH	    , FALSE);
			MoveWindow(gWindows.hButtonSend , cx - 45*iMW, cy/2+9*iMH , 43*iMW	   , 12*iMH		, FALSE);
			MoveWindow(gWindows.hEditConsole, 3 *iMW	 , cy/2+23*iMH, cx - 6 *iMW, cy/2-26*iMH, FALSE);
		}
		else
		{
			MoveWindow(gWindows.hListPlayers, 3 *iMW, 25 *iMH     , cx - 50*iMW, 102*iMH   , FALSE); //only resize console, keep listview's min size
			MoveWindow(gWindows.hComboRcon  , 3 *iMW, 132*iMH     , cx - 50*iMW, 10*iMH	   , FALSE);
			MoveWindow(gWindows.hButtonSend , cx - 45*iMW, 131*iMH,      43*iMW, 12*iMH	   , FALSE);
			MoveWindow(gWindows.hEditConsole, 3 *iMW, 145*iMH     , cx - 6 *iMW, cy-148*iMH, FALSE);
		}
		ShowWindow(gWindows.hComboRcon,   SW_SHOW);
		ShowWindow(gWindows.hButtonSend,  SW_SHOW);
		ShowWindow(gWindows.hEditConsole, SW_SHOW);
	}

	MoveWindow(gWindows.hButtonAutoKick		 , cx - 45*iMW, 54 *iMH, 43*iMW, 12*iMH, FALSE); //Move all buttons to left / right
	MoveWindow(gWindows.hButtonBanIP		 , cx - 45*iMW, 67 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonDPLoginProfile, cx - 45*iMW, 83 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonForcejoin	 , cx - 45*iMW, 115*iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonJoin		     , cx - 45*iMW, 2  *iMH + 1, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonKick		     , cx - 45*iMW, 41 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonReload		 , cx - 45*iMW, 25 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonWhois		 , cx - 45*iMW, 96 *iMH, 43*iMW, 12*iMH, FALSE);

#pragma warning(push)
#pragma warning(disable:26451)  // We're not computing large numbers here
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::NUMBER, 17*iMW);                   //num
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::NAME,   cx - 218*iMW);             //name
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::BUILD,  18*iMW);                   //build
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::ID,     27*iMW);                   //ID
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::OP,     15*iMW);                   //OP
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::IP,     47*iMW);                   //IP
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::PING,   17*iMW);                   //Ping
	ListView_SetColumnWidth(gWindows.hListPlayers, Subitems::SCORE,  20*iMW);                   //Score
	ListView_SetColumnWidth(gWindows.hListPlayers, 7,                 LVSCW_AUTOSIZE_USEHEADER);
#pragma warning(pop)

	RedrawWindow(gWindows.hWinMain, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
	
	FORWARD_WM_SIZE(hwnd, state, cx, cy, DefWindowProc);
}

void OnMainWindowGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo)
{
	DWORD dwBaseUnits = GetDialogBaseUnits();
	if (gSettings.bDisableConsole)
	{
		lpMinMaxInfo->ptMinTrackSize.x = MulDiv(230, LOWORD(dwBaseUnits), 4);
		lpMinMaxInfo->ptMinTrackSize.y = MulDiv(159, HIWORD(dwBaseUnits), 8);	
	}
	else
	{
		lpMinMaxInfo->ptMinTrackSize.x = MulDiv(230, LOWORD(dwBaseUnits), 4);
		lpMinMaxInfo->ptMinTrackSize.y = MulDiv(203, HIWORD(dwBaseUnits), 8);	
	}
	FORWARD_WM_GETMINMAXINFO(hwnd, lpMinMaxInfo, DefWindowProc);
}

HBRUSH OnMainWindowCtlColorStatic(HWND hwnd, HDC hdc, HWND hwndChild, int type)
{
	static DeleteObjectRAIIWrapper<HBRUSH> consoleBackgroundBrush(CreateSolidBrush(RGB(255, 255, 255)));

	if (hwndChild == gWindows.hEditConsole) //paint the background of the console white
	{
		SetTextColor(hdc, RGB(0, 0, 0) );
		SetBkColor  (hdc, RGB(255, 255, 255) );
		return consoleBackgroundBrush;
	}
	return FORWARD_WM_CTLCOLORSTATIC(hwnd, hdc, hwndChild, DefWindowProc);
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		HANDLE_MSG(hwnd, WM_CREATE,         OnMainWindowCreate);
		HANDLE_MSG(hwnd, WM_DESTROY,        OnMainWindowDestroy);
		HANDLE_MSG(hwnd, WM_COMMAND,        OnMainWindowCommand);
		HANDLE_MSG(hwnd, WM_NOTIFY,         OnMainWindowNotify);
		HANDLE_MSG(hwnd, WM_SIZE,           OnMainWindowSize);
		HANDLE_MSG(hwnd, WM_GETMINMAXINFO,  OnMainWindowGetMinMaxInfo);
		HANDLE_MSG(hwnd, WM_CTLCOLORSTATIC, OnMainWindowCtlColorStatic);
	}

	static std::jthread fetch_players_thread;
	static std::future<std::vector<pb2lib::Player>> fetched_players_future;

	static std::jthread fetch_servercvars_thread;
	static std::future<std::vector<std::string>> fetched_servercvars_future;

	// TODO: Move into callback function -- make variables globals?
	if (message == WM_REFETCHPLAYERS || message == WM_SERVERCHANGED) {
		g_AutoReloadTimer.reset_current_timeout();

		fetch_players_thread.request_stop();
		if (fetch_players_thread.joinable()) {
			fetch_players_thread.detach();
		}

		fetch_servercvars_thread.request_stop();
		if (fetch_servercvars_thread.joinable()) {
			fetch_servercvars_thread.detach();
		}

		auto* server = MainWindowGetSelectedServerOrLoggedNull();
		if (!server) {
			return NULL;
		}

		auto fetch_players_thread_function = [](std::promise<std::vector<pb2lib::Player>> promise, pb2lib::Server server, HWND window, double timeout) {
			try {
				promise.set_value(pb2lib::get_players(server.address, server.rcon_password, timeout));
			} catch (pb2lib::Exception&) {
				promise.set_exception(std::current_exception());
			}
			PostMessage(window, WM_PLAYERSREADY, 0, 0);
		};

		std::promise<std::vector<pb2lib::Player>> players_promise;
		fetched_players_future = players_promise.get_future();
		fetch_players_thread = std::jthread(fetch_players_thread_function, std::move(players_promise), *server, hwnd, gSettings.fTimeoutSecs);

		auto fetch_cvars_thread_function = [](std::promise<std::vector<std::string>> promise, pb2lib::Server server, HWND window, double timeout) {
			const std::vector<std::string> status_vars = { "mapname", "password", "elim", "timelimit", "maxclients" };
			try {
				promise.set_value(pb2lib::get_cvars(server.address, server.rcon_password, status_vars, gSettings.fTimeoutSecs));
			}
			catch (pb2lib::Exception&) {
				promise.set_exception(std::current_exception());
			}
			PostMessage(window, WM_SERVERCVARSREADY, 0, 0);
		};

		std::promise<std::vector<std::string>> cvars_promise;
		fetched_servercvars_future = cvars_promise.get_future();
		fetch_players_thread = std::jthread(fetch_cvars_thread_function, std::move(cvars_promise), *server, hwnd, gSettings.fTimeoutSecs);

		return NULL;
	}

	// TODO: Move into callback function
	if (message == WM_PLAYERSREADY) {
		// Message might be from an outdated / older thread -> only process if the current future is ready
		if (fetched_players_future.valid() && fetched_players_future.wait_for(0s) != std::future_status::timeout) {
			MainWindowLogPb2LibExceptionsToConsole([&]() {
				g_vPlayers = fetched_players_future.get();
				MainWindowUpdatePlayersListview();
				MainWindowWriteConsole("The player list was reloaded.");
			});
		}
		return NULL;
	}

	if (message == WM_SERVERCVARSREADY) {
		if (fetched_servercvars_future.valid() && fetched_servercvars_future.wait_for(0s) != std::future_status::timeout) {
			std::vector<std::string> values;

			std::string display_text = "Error";

			// TODO: Cvar names and value access are now separated, they could start mismatching. Do something about that?
			// Maybe some ServerCvars struct that is returned in the promise, the thread handles all strings and assigns compile-time names?

			MainWindowLogPb2LibExceptionsToConsole([&]() {
				values = fetched_servercvars_future.get();
				display_text = "map: " + values[0];
				display_text += " | pw: " + (values[1].size() > 0 ? values[1] : "none");
				display_text += " | elim: " + values[2];
				display_text += " | timelimit: " + values[3];
				display_text += " | maxclients: " + values[4];
			});

			SetWindowText(gWindows.hStaticServerInfo, display_text.c_str());
			DeleteObjectRAIIWrapper<HRGN> region(CreateRectRgn(0, 0, 0, 0));
			GetWindowRgn(gWindows.hStaticServerInfo, region);
			RedrawWindow(gWindows.hWinMain, NULL, region, RDW_ERASE | RDW_INVALIDATE);
		}
		return NULL;
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}

//}-------------------------------------------------------------------------------------------------
// Callback Set Ping Dialog                                                                        |
//{-------------------------------------------------------------------------------------------------

BOOL OnSetPingInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	std::string sMaxPing = std::to_string(gSettings.iAutoKickCheckMaxPingMsecs);
	SetDlgItemText(hwnd, IDC_SP_EDIT, sMaxPing.c_str());
	return TRUE;
}

void OnSetPingClose(HWND hwnd)
{
	EndDialog (hwnd, 0);
}

void OnSetPingCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDC_SP_BUTTONOK:
		{
			int iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_SP_EDIT)) + 1;
			std::vector<char> maxPingBuffer(iBufferSize);
			GetDlgItemText(hwnd, IDC_SP_EDIT, maxPingBuffer.data(), iBufferSize);
			gSettings.iAutoKickCheckMaxPingMsecs = atoi(maxPingBuffer.data());
			MainWindowUpdateAutoKickState();

			EndDialog(hwnd, 1);
			return;
		}
		
	case IDC_SP_BUTTONCANCEL:
		{
			EndDialog(hwnd, 0);
			return;
		}
	}
}

LRESULT CALLBACK SetPingDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
    	HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnSetPingInitDialog);
    	HANDLE_MSG(hWndDlg, WM_CLOSE,      OnSetPingClose);
    	HANDLE_MSG(hWndDlg, WM_COMMAND,    OnSetPingCommand);
    }
    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Program Settings Dialog                                                                |
//{-------------------------------------------------------------------------------------------------

BOOL OnProgramSettingsInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam)
{	
	char szBuffer [32]; //maximum: "10.00 s\0"
	std::string sBuffer;
	
	SendDlgItemMessage(hwnd, IDC_PS_TRACKOWNSERVERS, TBM_SETRANGE, FALSE, MAKELPARAM(2, 200));
	SendDlgItemMessage(hwnd, IDC_PS_TRACKOTHERSERVERS, TBM_SETRANGE, FALSE, MAKELPARAM(2, 200));
	SendDlgItemMessage(hwnd, IDC_PS_TRACKOWNSERVERS, TBM_SETTICFREQ, 1000, 0);
	SendDlgItemMessage(hwnd, IDC_PS_TRACKOTHERSERVERS, TBM_SETTICFREQ, 100, 0);
	
	SendDlgItemMessage(hwnd, IDC_PS_TRACKOWNSERVERS, TBM_SETPOS, TRUE, (LPARAM) (gSettings.fTimeoutSecs) * 20);
	sprintf (szBuffer, "%.2f s", gSettings.fTimeoutSecs);
	SetDlgItemText(hwnd, IDC_PS_STATICOWNSERVERS, szBuffer);
	
	SendDlgItemMessage(hwnd, IDC_PS_TRACKOTHERSERVERS, TBM_SETPOS, TRUE, (LPARAM) (gSettings.fAllServersTimeoutSecs) * 20);
	sprintf (szBuffer, "%.2f s", gSettings.fAllServersTimeoutSecs);
	SetDlgItemText(hwnd, IDC_PS_STATICOTHERSERVERS, szBuffer);
	
	sBuffer = std::to_string(gSettings.iAutoKickCheckDelay);
	SetDlgItemText(hwnd, IDC_PS_EDITAUTOKICKINTERVAL, sBuffer.c_str());
	
	sBuffer = std::to_string(gSettings.iAutoReloadDelaySecs);
	SetDlgItemText(hwnd, IDC_PS_EDITAUTORELOAD, sBuffer.c_str());

	sBuffer = std::to_string(gSettings.iMaxConsoleLineCount);
	SetDlgItemText(hwnd, IDC_PS_EDITLINECOUNT, sBuffer.c_str());
	if (gSettings.bLimitConsoleLineCount)
		CheckDlgButton(hwnd, IDC_PS_CHECKLINECOUNT, BST_CHECKED);
	else
		EnableWindow(GetDlgItem(hwnd, IDC_PS_EDITLINECOUNT), FALSE);
	
	if (gSettings.bColorPlayers)
		CheckDlgButton(hwnd, IDC_PS_CHECKCOLORPLAYERS, BST_CHECKED);
	
	if (gSettings.bColorPings)
		CheckDlgButton(hwnd, IDC_PS_CHECKCOLORPINGS, BST_CHECKED);
	
	if (gSettings.bDisableConsole)
		CheckDlgButton(hwnd, IDC_PS_CHECKDISABLECONSOLE, BST_CHECKED);
	
	return TRUE;
}

void OnProgramSettingsHScroll(HWND hwnd, HWND hwndCtl, UINT, int)
{
	if (hwndCtl == GetDlgItem(hwnd, IDC_PS_TRACKOWNSERVERS))
	{
		auto pos = SendDlgItemMessage(hwnd, IDC_PS_TRACKOWNSERVERS, TBM_GETPOS, 0, 0);
		char szStaticText [32]; //"10.00 s\0"
		snprintf (szStaticText, sizeof(szStaticText), "%.2f s", (float)pos/(float)20);
		SetDlgItemText(hwnd, IDC_PS_STATICOWNSERVERS, szStaticText);
	}
	else if (hwndCtl == GetDlgItem(hwnd, IDC_PS_TRACKOTHERSERVERS))
	{
		auto pos = SendDlgItemMessage(hwnd, IDC_PS_TRACKOTHERSERVERS, TBM_GETPOS, 0, 0);
		char szStaticText [32]; //"10.000 s\0"
		snprintf (szStaticText, sizeof(szStaticText), "%.2f s", (float)pos/(float)20);
		SetDlgItemText(hwnd, IDC_PS_STATICOTHERSERVERS, szStaticText);
	}
}

void OnProgramSettingsClose(HWND hwnd)
{
	gWindows.hDlgSettings = NULL;
	EndDialog(hwnd, 0);
}

void OnProgramSettingsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{				
	case IDC_PS_CHECKLINECOUNT:
		{
			if (codeNotify == BN_CLICKED)
			{
				if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKLINECOUNT) == BST_CHECKED)
					EnableWindow(GetDlgItem(hwnd, IDC_PS_EDITLINECOUNT), true);
				else
					EnableWindow(GetDlgItem(hwnd, IDC_PS_EDITLINECOUNT), false);
			}					
			break;
		}
		
	case IDC_PS_BUTTONOK:
		{			
			auto pos = SendDlgItemMessage(hwnd, IDC_PS_TRACKOWNSERVERS, TBM_GETPOS, 0, 0);
			gSettings.fTimeoutSecs = (float)pos/(float)20;
			
			pos = SendDlgItemMessage(hwnd, IDC_PS_TRACKOTHERSERVERS, TBM_GETPOS, 0, 0);
			gSettings.fAllServersTimeoutSecs = (float)pos/(float)20;
			
			auto iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_PS_EDITAUTOKICKINTERVAL)) + 1;
			iBufferSize = max(iBufferSize, GetWindowTextLength(GetDlgItem(hwnd, IDC_PS_EDITAUTORELOAD)) + 1);
			iBufferSize = max(iBufferSize, GetWindowTextLength(GetDlgItem(hwnd, IDC_PS_EDITLINECOUNT)) + 1);
			std::vector<char> buffer(iBufferSize);
			
			GetDlgItemText(hwnd, IDC_PS_EDITAUTOKICKINTERVAL, buffer.data(), static_cast<int>(buffer.size()));
			gSettings.iAutoKickCheckDelay = atoi(buffer.data());
			MainWindowUpdateAutoKickState();
			
			GetDlgItemText(hwnd, IDC_PS_EDITAUTORELOAD, buffer.data(), static_cast<int>(buffer.size()));
			gSettings.iAutoReloadDelaySecs = atoi (buffer.data());
			g_AutoReloadTimer.set_interval(gSettings.iAutoReloadDelaySecs);
			
			// TODO: Also use 0 = unlimited semantics?
			GetDlgItemText(hwnd, IDC_PS_EDITLINECOUNT, buffer.data(), static_cast<int>(buffer.size()));
			gSettings.iMaxConsoleLineCount = atoi (buffer.data());
			if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKLINECOUNT) == BST_CHECKED)
			{
				gSettings.bLimitConsoleLineCount = 1;
				Edit_ReduceLines(gWindows.hEditConsole, gSettings.iMaxConsoleLineCount);
				Edit_ScrollToEnd(gWindows.hEditConsole);
			}
			else
			{
				gSettings.bLimitConsoleLineCount = 0;
			}
			
			if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKCOLORPLAYERS) == BST_CHECKED)
				gSettings.bColorPlayers = true;
			else
				gSettings.bColorPlayers = false;
				
			if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKCOLORPINGS) == BST_CHECKED)
				gSettings.bColorPings = true;
			else
				gSettings.bColorPings = false;
			
			if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKDISABLECONSOLE) == BST_CHECKED)
				gSettings.bDisableConsole = true;
			else
				gSettings.bDisableConsole = false;
			
			RECT rc;
			GetClientRect(gWindows.hWinMain, &rc);
			OnMainWindowSize(gWindows.hWinMain, SIZE_RESTORED, rc.right, rc.bottom); // Redraw
					
			SendMessage(hwnd, WM_CLOSE, 0, 0); //Make sure to clear the handle so a new one is opened next time
			return;
		}
		
	case IDC_PS_BUTTONCANCEL:
		{
			SendMessage(hwnd, WM_CLOSE, 0, 0); //Make sure to clear the handle so a new one is opened next time
			return;
		}
	}
}

LRESULT CALLBACK ProgramSettingsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
	HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnProgramSettingsInitDialog);
	HANDLE_MSG(hWndDlg, WM_CLOSE,      OnProgramSettingsClose);
	HANDLE_MSG(hWndDlg, WM_COMMAND,    OnProgramSettingsCommand);
	HANDLE_MSG(hWndDlg, WM_HSCROLL,     OnProgramSettingsHScroll);
    }
    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Manage Rotation Dialog                                                                 |
//{-------------------------------------------------------------------------------------------------

void LoadRotationToListbox(HWND hListBox)
{
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}
	
	MainWindowLogPb2LibExceptionsToConsole([&]() {
		std::string response = pb2lib::send_rcon(server->address, server->rcon_password, "sv maplist", gSettings.fTimeoutSecs);

		SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
		std::string::const_iterator start = response.begin();
		std::string::const_iterator end = response.end();
		std::regex rx(R"(^\d+ (.*?)$)");
		std::smatch matches;
		while (std::regex_search(start, end, matches, rx))
		{
			start = matches[0].second;
			std::string map = matches[1];
			ListBox_AddString(hListBox, map.c_str());
		}
	});
}

BOOL OnManageRotationInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam)
{
	OnManageRotationReloadContent(hwnd);
	return TRUE;
}


void OnManageRotationReloadContent(HWND hwnd)
{
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	// TODO: Might log two times -- probably ok?
	LoadRotationToListbox(GetDlgItem(hwnd, IDC_MROT_LIST));

	MainWindowLogPb2LibExceptionsToConsole([&]() {
		std::string answer = pb2lib::get_cvar(server->address, server->rcon_password, "rot_file", gSettings.fTimeoutSecs);
		SetDlgItemText(hwnd, IDC_MROT_EDITFILE, answer.c_str());
	});

	std::optional<std::string> pb2InstallPath = GetPb2InstallPath();
	if (!pb2InstallPath)
		return;

	std::string sMapshot = pb2InstallPath.value() + "\\pball\\pics\\mapshots\\-no-preview-.jpg";
	std::wstring sWideMapshot(sMapshot.begin(), sMapshot.end());

	g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());
}


void OnManageRotationPaint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(GetDlgItem(hwnd, IDC_MROT_MAPSHOT), &ps);
	const RECT ui_rect = ps.rcPaint;
	FillRect(hdc, &ui_rect, (HBRUSH) (COLOR_WINDOW));

	const int ui_width = ui_rect.right - ui_rect.left;
	const int ui_height = ui_rect.bottom - ui_rect.top;

	if (g_pMapshotBitmap) {
		const int image_width = g_pMapshotBitmap->GetWidth();
		const int image_height = g_pMapshotBitmap->GetHeight();
		const double image_ratio = 1.0 * image_width / image_height;

		int draw_width = ui_width;
		int draw_height = static_cast<int>((1.0 / image_ratio) * draw_width);
		if (draw_height > ui_height) {
			draw_height = ui_height;
			draw_width = static_cast<int>(image_ratio * draw_height);
		}

		const int offset_x = (ui_width - draw_width) / 2;
		const int offset_y = (ui_height - draw_height) / 2;

		Gdiplus::Graphics graphics(hdc);
		graphics.DrawImage(g_pMapshotBitmap.get(), offset_x, offset_y, draw_width, draw_height);
	}

	EndPaint(GetDlgItem(hwnd, IDC_MROT_MAPSHOT), &ps);
	DeleteDC(hdc);
}


void OnManageRotationClose(HWND hwnd)
{	
	gWindows.hDlgManageRotation = NULL;
	
	EndDialog(hwnd, 0);
}

void OnManageRotationCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	auto executeSubcommandOnSelectedServer = [&](std::string subcommand) {
		auto* server = MainWindowGetSelectedServerOrLoggedNull();
		if (!server) {
			return std::string();
		}

		std::string command = "sv rotation " + subcommand;
		std::string answer;

		MainWindowLogPb2LibExceptionsToConsole([&]() {
			answer = pb2lib::send_rcon(server->address, server->rcon_password, command, gSettings.fTimeoutSecs);
		});

		LoadRotationToListbox(GetDlgItem(hwnd, IDC_MROT_LIST));
		return answer;
	};

	auto executeMapSubcommandOnSelectedServer = [&](std::string mapSubcommand) {
		auto iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_MROT_EDITMAP)) + 1;
		std::vector<char> buffer(iBufferSize);
		GetDlgItemText(hwnd, IDC_MROT_EDITMAP, buffer.data(), iBufferSize);

		std::string command = mapSubcommand + " " + buffer.data();
		return executeSubcommandOnSelectedServer(command);
	};

	switch (id)
	{
		case IDC_MROT_BUTTONADD:
		{
			executeMapSubcommandOnSelectedServer("add");
			break;
		}
	
		case IDC_MROT_BUTTONREMOVE:
		{
			executeMapSubcommandOnSelectedServer("remove");
			break;
		}
	
		case IDC_MROT_BUTTONCLEAR:
		{
			executeSubcommandOnSelectedServer("clear");
			break;
		}
	
		case IDC_MROT_BUTTONWRITE:
		{
			auto sAnswer = executeSubcommandOnSelectedServer("write");

			if (sAnswer.find("Saved maplist to") != std::string::npos)
				MessageBox(hwnd, "The maplist was saved successfully", "Success", MB_OK | MB_ICONINFORMATION);
			else
			{
				std::string sContent = "An error occured. The server answered: " + sAnswer;
				MessageBox(hwnd, sContent.c_str(), "Error", MB_OK | MB_ICONERROR);
			}
			break;
		}
	
		case IDC_MROT_BUTTONREAD:
		{
			executeSubcommandOnSelectedServer("load");
			break;
		}
	
		case IDC_MROT_BUTTONOK:
		{
			SendMessage(hwnd, WM_CLOSE, 0, 0);
			break;
		}
		
		case IDC_MROT_LIST:
		{
			if (codeNotify == LBN_SELCHANGE)
			{
				auto iCurSel = SendMessage(GetDlgItem(hwnd, IDC_MROT_LIST), LB_GETCURSEL, 0, 0);
				if (iCurSel == LB_ERR) return;

				auto iBufferSize = SendMessage(GetDlgItem(hwnd, IDC_MROT_LIST), LB_GETTEXTLEN, iCurSel, 0) + 1;
				std::vector<char> mapnameBuffer(iBufferSize);
				SendMessage(GetDlgItem(hwnd, IDC_MROT_LIST), LB_GETTEXT, iCurSel, (LPARAM) mapnameBuffer.data());
				SetDlgItemText(hwnd, IDC_MROT_EDITMAP, mapnameBuffer.data());
			}
			break;
		}
	
		case IDC_MROT_EDITMAP:
		{
			if (codeNotify == EN_CHANGE)
			{
				std::optional<std::string> pb2InstallPath = GetPb2InstallPath();
				if (!pb2InstallPath)
					return;

				std::string sMapshot = pb2InstallPath.value() + R"(\pball\pics\mapshots\)";
				int iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_MROT_EDITMAP)) + 1;
				std::vector<char> mapnameBuffer(iBufferSize);
				GetDlgItemText(hwnd, IDC_MROT_EDITMAP, mapnameBuffer.data(), iBufferSize);
				sMapshot += mapnameBuffer.data();
				sMapshot += ".jpg";

				DWORD dwAttributes = GetFileAttributes(sMapshot.c_str());
				if (dwAttributes == INVALID_FILE_ATTRIBUTES || (dwAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					sMapshot = pb2InstallPath.value() + R"(\pball\pics\mapshots\-no-preview-.jpg)";
				}

				RECT rcMapshotImageRect;
				GetClientRect(GetDlgItem(hwnd, IDC_MROT_MAPSHOT), &rcMapshotImageRect);

				std::wstring sWideMapshot(sMapshot.begin(), sMapshot.end());
				g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());

				RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_INVALIDATE);
			}
		}
	}
}

LRESULT CALLBACK ManageRotationDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
    	HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnManageRotationInitDialog);
    	HANDLE_MSG(hWndDlg, WM_CLOSE,      OnManageRotationClose);
    	HANDLE_MSG(hWndDlg, WM_COMMAND,    OnManageRotationCommand);
    	HANDLE_MSG(hWndDlg, WM_PAINT,      OnManageRotationPaint);
    }
    
    if (Msg == WM_SERVERCHANGED)
	{
		OnManageRotationReloadContent(hWndDlg);
		return TRUE;
	}

    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback RCON Commands Dialog                                                                   |
//{-------------------------------------------------------------------------------------------------

BOOL OnRCONCommandsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	HWND textElement = GetDlgItem(hwnd, IDC_RCONCOMMANDS_INFOTEXT);
	SetWindowText(textElement,
		"sv (Prefix) - lets you use most of the in-game commands, including admin commands\r\n"
		"Examples:\r\n\r\n"
		"sv addip\r\n"
		"sv expert X\r\n"
		"sv listip\r\n"
		"sv listuserip\r\n"
		"sv maplist\r\n"
		"sv newmap\r\n"
		"sv players\r\n"
		"sv removeip\r\n"
		"sv rotation add\r\n"
		"sv rotation delete\r\n"
		"sv rotation load\r\n"
		"sv rotation save\r\n"
		"sv tban\r\n"
		"sv writeip\r\n\r\n"
		"VARNAME VALUE - sets the specified variable to the value\r\n"
		"status - gives you an overview of all players and the current map\r\n"
		"kick NUMBER - kicks a player by his number\r\n"
		"map NAME - instantly restarts the server and loads a map.\r\n"
		"say TEXT - says the text as server\r\n"
		"quit - closes the server\r\n"
		"exit - closes the server\r\n"
		"exec PATH - executes a config file\r\n"
		"set VARNAME CONTENT TYPE - sets the content of a variable. The type is optional\r\n"
		"unset VARNAME - unsets a variable"
	);
	return TRUE;
}

void OnRCONCommandsClose(HWND hwnd)
{
	gWindows.hDlgRconCommands = NULL;
	EndDialog(hwnd, 1);
}

LRESULT CALLBACK RCONCommandsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnRCONCommandsInitDialog);
		HANDLE_MSG(hWndDlg, WM_CLOSE, OnRCONCommandsClose);
    }
    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Manage Servers Dialog                                                                  |
//{-------------------------------------------------------------------------------------------------

void LoadServersToListbox(LPVOID lpArgumentStruct) //Only called as thread, has to delete its argument
{
	size_t uid = static_cast<LoadServersArgs *>(lpArgumentStruct)->uid;
	HWND hwndListbox = static_cast<LoadServersArgs *>(lpArgumentStruct)->hwnd;
	delete static_cast<LoadServersArgs *>(lpArgumentStruct);
	
	bool bExit = false;
	
	MainWindowWriteConsole("Loading servers, this may take a short time...");
	
	std::string sServerlist = GetHttpResponse(gSettings.sServerlistAddress);

	std::smatch matches;
	std::regex rx (R"((\d+\.\d+\.\d+\.\d+):(\d{2,5}))");
	std::string::const_iterator start = sServerlist.begin();
	std::string::const_iterator end   = sServerlist.end();

	while (std::regex_search(start, end, matches, rx)) //add all servers that answer to the listbox
	{
		start = matches[0].second;

		pb2lib::Server server;
		server.address.ip = matches[1];
		server.address.port = std::stoi(matches[2]);
		// TODO: Asynchronous
		auto hostname = pb2lib::get_hostname_or_nullopt(server.address, gSettings.fAllServersTimeoutSecs);
		if (hostname) {
			server.hostname = hostname.value();
			g_vAllServers.push_back(server);
			auto index = SendMessage(hwndListbox, LB_ADDSTRING, 0, (LPARAM)server.hostname.c_str());
			SendMessage(hwndListbox, LB_SETITEMDATA, index, g_vAllServers.size() - 1);
		}
		
		try
		{
			if (WaitForSingleObject(g_mLoadServersThreads.at(uid), 0) == WAIT_OBJECT_0)
			{
				bExit = true;
				break;
			}
		}
		catch (const std::out_of_range&)
		{
			// todo -- this should work differently. A thread should get all hostnames, then the UI should be updated (synchronously)
			return;
		}
	}

	CloseHandle(g_mLoadServersThreads.at(uid)); //Delete the Event
	g_mLoadServersThreads.erase(g_mLoadServersThreads.find(uid));
	if (!bExit) MainWindowWriteConsole("Done.");
}

void OnManageServersClose(HWND hwnd)
{
	EndDialog(hwnd, 1);
}

void OnManageServersDestroy(HWND hwnd)
{
	SendMessage(gWindows.hComboServer, CB_RESETCONTENT, 0, 0);
	for (unsigned int i = 0; i < g_vSavedServers.size(); i++)
	{
		auto index = SendMessage(gWindows.hComboServer, CB_ADDSTRING, 0, (LPARAM) g_vSavedServers[i].hostname.c_str());
		SendMessage(gWindows.hComboServer, CB_SETITEMDATA, index, i);
	}
	SendMessage(gWindows.hComboServer, CB_SETCURSEL, 0, 0);
	
	SignalAllThreads(&g_mLoadServersThreads);
	MainWindowWriteConsole("Aborting.");  // TODO: Not always correct
	
	g_vAllServers.clear();
}

BOOL OnManageServersInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	// TODO: Maybe initially list servers as their IPs, replace by hostname once retrieved
	for(unsigned int i = 0; i < g_vSavedServers.size(); i++) {
		auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING, 0, (LPARAM)g_vSavedServers[i].hostname.c_str());
		SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETITEMDATA, index, i);
	}
	
	LoadServersArgs * lpArgumentsStruct = new LoadServersArgs;
	
	lpArgumentsStruct->uid = GetFirstUnusedMapKey(g_mLoadServersThreads);
	lpArgumentsStruct->hwnd = GetDlgItem(hwnd, IDC_DM_LISTLEFT);
	
	g_mLoadServersThreads.emplace(lpArgumentsStruct->uid, CreateEvent(NULL, TRUE, FALSE, NULL));
	
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) &LoadServersToListbox, lpArgumentsStruct, 0, NULL);
	if (hThread == NULL) {
		MessageBox(NULL, "Failed to create thread to load servers", "Error", MB_OK | MB_ICONERROR);
	}
	else {
		CloseHandle(hThread);
	}
	
	return TRUE;
}

void OnManageServersCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch(id)
	{
		case IDC_DM_BUTTONOK:
		{
			//saving will be done after WM_DESTROY
			EndDialog(hwnd, 0);
			return ;
		}
		case IDC_DM_BUTTONADD:
		{
			// TODO: Disallow adding server with empty password

			int iBufferSize = 16; //"255.255.255.255\0
			int iRconPWLength = GetWindowTextLength(GetDlgItem(hwnd, IDC_DM_EDITPW)) + 1;
			iBufferSize = (iRconPWLength > iBufferSize) ? iRconPWLength : iBufferSize;
			std::vector<char> buffer(iBufferSize);

			pb2lib::Server server;

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_GETTEXT, iBufferSize, (LPARAM) buffer.data());
			server.address.port = atoi(buffer.data());

			DWORD dwIP = 0;
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_GETADDRESS, 0, (LPARAM) &dwIP);
			server.address.ip = std::to_string(FIRST_IPADDRESS(dwIP)) + "." +
							 std::to_string(SECOND_IPADDRESS(dwIP)) + "." +
							 std::to_string(THIRD_IPADDRESS(dwIP)) + "." +
							 std::to_string(FOURTH_IPADDRESS(dwIP));

			// TODO: Deduplicate with initialization from config file.

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPW), WM_GETTEXT, iBufferSize, (LPARAM) buffer.data());
			server.rcon_password = buffer.data();
			server.hostname = pb2lib::get_hostname_or_nullopt(server.address, gSettings.fTimeoutSecs).value_or(unreachable_hostname);

			g_vSavedServers.push_back(server);

			auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING, 0, (LPARAM) server.hostname.c_str());
			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETITEMDATA, index, g_vSavedServers.size() - 1);
			return;
		}
		case IDC_DM_BUTTONREMOVE:
		{
			// TODO: Use helpers
			//Get position of selected server in g_vSavedServers
			auto iCurSel = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETCURSEL, 0, 0);
			if (iCurSel == LB_ERR) return;
			auto iServerIndex = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETITEMDATA, iCurSel, 0);
			if (iServerIndex == LB_ERR) return;

			// TODO: Removing the selected server causes "Error why trying to get selected server" spam by the auto-reload-thread
			// Dialog should modify a copy, gServer should be changed on "OK"/"Save", together with updating the combobox.
			g_vSavedServers.erase(g_vSavedServers.begin() + iServerIndex);


			// TODO: Remove instead of clear + rebuild -- or, at least, factor this out
			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_RESETCONTENT, 0, 0); //Reload right listbox
			for(unsigned int i = 0; i < g_vSavedServers.size(); i++) {
				auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING,
										 0, (LPARAM)g_vSavedServers[i].hostname.c_str());
				SendMessage (GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETITEMDATA, index, i);
			}


			return;
		}
		case IDC_DM_BUTTONSAVE:
		{
			int iBufferSize = 16; //"255.255.255.255\0"
			iBufferSize = max(iBufferSize, GetWindowTextLength(GetDlgItem(hwnd, IDC_DM_EDITPW)) + 1);
			std::vector<char> buffer(iBufferSize);

			auto iCurSel = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETCURSEL, 0, 0);
			if (iCurSel == LB_ERR)
			{
				return;
			}
			auto iRet = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETITEMDATA, iCurSel, 0);
			if (iRet == LB_ERR)
			{
				return;
			}

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_GETTEXT, iBufferSize, (LPARAM) buffer.data());
			g_vSavedServers[iRet].address.port = atoi(buffer.data());

			DWORD dwIP = 0;
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_GETADDRESS, 0, (LPARAM) &dwIP);
			snprintf(buffer.data(), iBufferSize, "%lu.%lu.%lu.%lu", FIRST_IPADDRESS(dwIP), SECOND_IPADDRESS(dwIP),
					THIRD_IPADDRESS(dwIP), FOURTH_IPADDRESS(dwIP));
			g_vSavedServers[iRet].address.ip = buffer.data();

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPW), WM_GETTEXT, iBufferSize, (LPARAM)buffer.data());
			g_vSavedServers[iRet].rcon_password = buffer.data();

			g_vSavedServers[iRet].hostname = pb2lib::get_hostname_or_nullopt(g_vSavedServers[iRet].address, gSettings.fAllServersTimeoutSecs).value_or(unreachable_hostname);

			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_RESETCONTENT, 0, 0); //reload right listbox
			for(size_t i = 0; i < g_vSavedServers.size(); i++)
			{
				auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING, 0, (LPARAM)g_vSavedServers[i].hostname.c_str());
				SendMessage (GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETITEMDATA, index, i);
			}
			
			return;
		}
	}
	if (codeNotify == LBN_SELCHANGE)
	{
		if (id == IDC_DM_LISTLEFT)
		{
			char szIpBuffer[16] = {'\0'};
			auto selectedServerIndex = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTLEFT), LB_GETITEMDATA,
						SendMessage(GetDlgItem(hwnd, IDC_DM_LISTLEFT), LB_GETCURSEL, 0, 0),
						0);
			if (selectedServerIndex == LB_ERR) return;

			strcpy (szIpBuffer, g_vAllServers[selectedServerIndex].address.ip.c_str());

			BYTE b0, b1, b2, b3;
			SplitIpAddressToBytes(szIpBuffer, &b0, &b1, &b2, &b3);

#pragma warning (suppress : 26451)
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_SETTEXT,
						0, (LPARAM) (std::to_string(g_vAllServers[selectedServerIndex].address.port)).c_str());

			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETCURSEL,  -1, 0);
			EnableWindow(GetDlgItem(hwnd, IDC_DM_BUTTONREMOVE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_DM_BUTTONSAVE), FALSE);
			return;
		}
		else if (id == IDC_DM_LISTRIGHT)
		{
			auto selectedSavedServerIndex = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETITEMDATA,
						SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETCURSEL, 0, 0),
						0);
			if (selectedSavedServerIndex == LB_ERR) return;

			char szIpBuffer[16] = {'\0'};
			strcpy (szIpBuffer, g_vSavedServers[selectedSavedServerIndex].address.ip.c_str());
			BYTE b0, b1, b2, b3;
			SplitIpAddressToBytes(szIpBuffer, &b0, &b1, &b2, &b3);

#pragma warning (suppress : 26451)
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_SETTEXT,
						0, (LPARAM) (std::to_string(g_vSavedServers[selectedSavedServerIndex].address.port)).c_str());
			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPW), WM_SETTEXT,
						0, (LPARAM) g_vSavedServers[selectedSavedServerIndex].rcon_password.c_str());
			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTLEFT), LB_SETCURSEL,  -1, 0);
			EnableWindow(GetDlgItem(hwnd, IDC_DM_BUTTONREMOVE), TRUE);
			EnableWindow(GetDlgItem(hwnd, IDC_DM_BUTTONSAVE), TRUE);
		}
	}
}

LRESULT CALLBACK ManageServersDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
		HANDLE_MSG(hWndDlg, WM_CLOSE,      OnManageServersClose);
		HANDLE_MSG(hWndDlg, WM_DESTROY,    OnManageServersDestroy);
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnManageServersInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnManageServersCommand);
	}
	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Forcejoin Dialog                                                                       |
//{-------------------------------------------------------------------------------------------------

BOOL OnForcejoinInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[r] Red");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[b] Blue");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[p] Purple");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[y] Yellow");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[a] Automatic");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[o] Observer");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_SETCURSEL, 0, 0);
	return TRUE;
}

void OnForcejoinCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDC_FJ_OK: { //return the char that can be used to join (r, b, p, y, a or o)
		auto hwndColorList = GetDlgItem(hwnd, IDC_FJ_COLORLIST);
		auto selectedColorIndex = SendMessage(hwndColorList, LB_GETCURSEL, 0, 0);
		auto selectedColorLength = SendMessage(hwndColorList, LB_GETTEXTLEN, selectedColorIndex, 0);

		std::vector<char> buffer(selectedColorLength + 1);
		SendMessage(hwndColorList, LB_GETTEXT, selectedColorIndex, (LPARAM)buffer.data());

		if (strlen(buffer.data()) > 1)
			EndDialog(hwnd, (LPARAM)buffer[1]);
		else
			EndDialog(hwnd, -1);
		return;
	}

	case IDC_FJ_CANCEL: {
		EndDialog(hwnd, -1);
		return;
	}
	}
}

void OnForcejoinClose(HWND hwnd)
{
	EndDialog(hwnd, -1);
}

void OnForcejoinKeyDown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
	char szColor[14] = {'\0'}; //[a] Automatic\0

	switch(vk)
	{
		case 0x41: //a - auto
			sprintf(szColor, "[a] Automatic"); break;
		case 0x42: //b - blue
			sprintf(szColor, "[b] Blue"); break;
		case 0x50: //p - purple
			sprintf(szColor, "[p] Purple"); break;
		case 0x52: //r - red
			sprintf(szColor, "[r] Red"); break;
		case 0x4F: //o - observer
			sprintf(szColor, "[o] Observer"); break;
		case 0x59: //y - yellow
			sprintf(szColor, "[y] Yellow"); break;
		case VK_ESCAPE:
		{
			EndDialog(hwnd, -1);
			return;
		}
	}
	
	if (strlen(szColor) > 0)
		SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_SELECTSTRING, -1, (LPARAM) szColor);
}

LRESULT CALLBACK ForcejoinDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnForcejoinInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnForcejoinCommand);
		HANDLE_MSG(hWndDlg, WM_CLOSE,      OnForcejoinClose);
		HANDLE_MSG(hWndDlg, WM_KEYDOWN,    OnForcejoinKeyDown);
	}
	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Manage IDs Dialog                                                                      |
//{-------------------------------------------------------------------------------------------------

BOOL OnManageIDsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	for (size_t i = 0; i<g_vAutoKickEntries.size(); i++) {
		auto index = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_ADDSTRING, 0, (LPARAM) g_vAutoKickEntries[i].sText.c_str());
		SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_SETITEMDATA, index, i);
	}
	SendMessage(GetDlgItem(hwnd, IDC_MIDS_RADIOID), BM_SETCHECK, BST_CHECKED, 1);
	return TRUE;
}

void OnManageIDsClose(HWND hwnd)
{
	gWindows.hDlgManageIds = NULL;
	EndDialog(hwnd, 1);
}

void OnManageIDsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	auto refillListbox = [&]() {
		SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_RESETCONTENT, 0, 0);
		for (size_t i = 0; i < g_vAutoKickEntries.size(); i++) {
			auto index = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_ADDSTRING, 0, (LPARAM)g_vAutoKickEntries[i].sText.c_str());
			SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_SETITEMDATA, index, i);
		}
	};

	switch(id)
	{
		case IDC_MIDS_BUTTONADD:
		{
			AutoKickEntry entry;
			std::vector<char> buffer(static_cast<size_t>(GetWindowTextLength(GetDlgItem(hwnd, IDC_MIDS_EDIT))) + 1);

			SendMessage (GetDlgItem(hwnd, IDC_MIDS_EDIT), WM_GETTEXT, buffer.size(), (LPARAM) buffer.data()); //set text
			entry.sText = buffer.data();

			if (IsDlgButtonChecked(hwnd, IDC_MIDS_RADIOID)) //Set ID / NAME flag
			{
				std::vector<char> compareBuffer(buffer.size());
				sprintf(compareBuffer.data(), "%d", atoi(buffer.data())); //check if it's a valid ID
				
				if (strcmp (buffer.data(), compareBuffer.data()) != 0) {
					MessageBox(gWindows.hWinMain, "The ID you have entered is not valid.", "Error: Invalid ID", MB_OK | MB_ICONERROR);
					return ;
				}
				entry.tType = AutoKickEntry::Type::ID;
			}
			else {
				assert(IsDlgButtonChecked(hwnd, IDC_MIDS_RADIONAME));
				entry.tType = AutoKickEntry::Type::NAME;
			}

			g_vAutoKickEntries.push_back(entry);

			refillListbox();
			return;
		}
		
		case IDC_MIDS_BUTTONOK:
		{
			SendMessage(hwnd, WM_CLOSE, 0, 0); //Make sure to clear the handle so a new one is opened next time
			return;
		}

		case IDC_MIDS_BUTTONREMOVE:
		{
			auto selectedPlayerIndex = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_GETITEMDATA,
						SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_GETCURSEL, 0, 0),
						0);
			if (selectedPlayerIndex == LB_ERR) return;

			g_vAutoKickEntries.erase(g_vAutoKickEntries.begin() + selectedPlayerIndex); //delete the entry

			refillListbox();
			return;
		}
		case IDC_MIDS_BUTTONSAVE:
		{
			auto selectedPlayerIndex = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_GETITEMDATA,
						SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_GETCURSEL, 0, 0),
						0);
			if (selectedPlayerIndex == LB_ERR) return;

			int iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_MIDS_EDIT)) + 1;
			std::vector<char> buffer(iBufferSize);
			SendMessage (GetDlgItem(hwnd, IDC_MIDS_EDIT), WM_GETTEXT, iBufferSize, (LPARAM) buffer.data());
			g_vAutoKickEntries[selectedPlayerIndex].sText = buffer.data();

			if (IsDlgButtonChecked(hwnd, IDC_MIDS_RADIOID))
			{
				std::vector<char> comparisonBuffer(buffer.size());
				sprintf(comparisonBuffer.data(), "%d", atoi(buffer.data())); //check if it's a valid ID
				if (strcmp (buffer.data(), comparisonBuffer.data()) != 0) {
					MessageBoxA(gWindows.hWinMain, "The ID you have entered is not valid.", "Error: Invalid ID", MB_OK | MB_ICONERROR);
					return;
				}
				g_vAutoKickEntries[selectedPlayerIndex].tType = AutoKickEntry::Type::ID;
			}
			else {
				assert(IsDlgButtonChecked(hwnd, IDC_MIDS_RADIONAME));
				g_vAutoKickEntries[selectedPlayerIndex].tType = AutoKickEntry::Type::NAME;
			}

			refillListbox();
			return;
		}
		
		case IDC_MIDS_LIST:
		{
			if (codeNotify == LBN_SELCHANGE)
			{
				auto selectedPlayerIndex = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_GETITEMDATA,
						SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_GETCURSEL, 0, 0),
						0);
				if (selectedPlayerIndex == LB_ERR) return;
				SendMessage(GetDlgItem(hwnd, IDC_MIDS_EDIT), WM_SETTEXT,  0, (LPARAM) g_vAutoKickEntries[selectedPlayerIndex].sText.c_str());
				
				if (g_vAutoKickEntries[selectedPlayerIndex].tType == AutoKickEntry::Type::ID)
				{
					SendMessage(GetDlgItem(hwnd, IDC_MIDS_RADIOID), BM_SETCHECK, BST_CHECKED, 1);
					SendMessage(GetDlgItem(hwnd, IDC_MIDS_RADIONAME), BM_SETCHECK, BST_UNCHECKED, 1);
				}
				else
				{
					SendMessage(GetDlgItem(hwnd, IDC_MIDS_RADIONAME), BM_SETCHECK, BST_CHECKED, 1);
					SendMessage(GetDlgItem(hwnd, IDC_MIDS_RADIOID), BM_SETCHECK, BST_UNCHECKED, 1);
				}
			}
		}
	}
}

LRESULT CALLBACK ManageIDsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnManageIDsInitDialog);
		HANDLE_MSG(hWndDlg, WM_CLOSE,      OnManageIDsClose);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnManageIDsCommand);
	}
	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Manage IPs Dialog                                                                      |
//{-------------------------------------------------------------------------------------------------

void LoadBannedIPsToListbox(HWND hListBox)
{
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	MainWindowLogPb2LibExceptionsToConsole([&]() {
		std::string answer = pb2lib::send_rcon(server->address, server->rcon_password, "sv listip", gSettings.fTimeoutSecs);

		ListBox_ResetContent(hListBox);
		std::string::const_iterator start = answer.begin();
		std::string::const_iterator end = answer.end();
		std::regex rx(R"(\s*\d+\.\s*\d+\.\s*\d+\.\s*\d+)");
		std::smatch matches;
		while (std::regex_search(start, end, matches, rx))
		{
			start = matches[0].second;
			std::string ip = matches[0];
			ListBox_AddString(hListBox, ip.c_str());
		}
	});
}

BOOL OnManageIPsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_MIPS_LIST));
	return TRUE;
}

void OnManageIPsClose(HWND hwnd)
{
	gWindows.hDlgManageIps = NULL;
	EndDialog(hwnd, 1);
}

void OnManageIPsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	auto helper_run_rcon_command_with_current_ip = [&](std::string command) {
		auto* server = MainWindowGetSelectedServerOrLoggedNull();
		if (!server) {
			return;
		}

		DWORD ip = 0;
		SendMessage(GetDlgItem(hwnd, IDC_MIPS_IPCONTROL), IPM_GETADDRESS, 0, (LPARAM)&ip);

		command += std::to_string(FIRST_IPADDRESS(ip)) + "." + std::to_string(SECOND_IPADDRESS(ip))
			+ "." + std::to_string(THIRD_IPADDRESS(ip)) + "." + std::to_string(FOURTH_IPADDRESS(ip));

		MainWindowLogPb2LibExceptionsToConsole([&]() {
			pb2lib::send_rcon(server->address, server->rcon_password, command, gSettings.fTimeoutSecs);
		});

		LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_MIPS_LIST));
	};

	switch(id)
	{
		case IDC_MIPS_BUTTONADD:
			return helper_run_rcon_command_with_current_ip("sv addip ");
	
		case IDC_MIPS_BUTTONREMOVE:
			return helper_run_rcon_command_with_current_ip("sv removeip ");
	
		case IDC_MIPS_BUTTONOK:
			SendMessage(hwnd, WM_CLOSE, 0, 0);
			return;
		
		case IDC_MIPS_LIST:
		{
			if (codeNotify == LBN_SELCHANGE)
			{
				char szBuffer[16] = { 0 };
				BYTE b0, b1, b2, b3;
				SendMessage(GetDlgItem(hwnd, IDC_MIPS_LIST), LB_GETTEXT,
							SendMessage(GetDlgItem(hwnd, IDC_MIPS_LIST), LB_GETCURSEL, 0, 0),
							(LPARAM) szBuffer);
				SplitIpAddressToBytes(szBuffer, &b0, &b1, &b2, &b3);
#pragma warning (suppress : 26451)
				SendMessage(GetDlgItem(hwnd, IDC_MIPS_IPCONTROL), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			}
		}
	}
}

void OnManageIPsReloadContent(HWND hwnd)
{
	LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_MIPS_LIST));
}

LRESULT CALLBACK ManageIPsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnManageIPsInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnManageIPsCommand);
		HANDLE_MSG(hWndDlg, WM_CLOSE,      OnManageIPsClose);
	}

	if (Msg == WM_SERVERCHANGED) {
		OnManageIPsReloadContent(hWndDlg);
		return TRUE;
	}
	
	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Other functions                                                                                 |
//{-------------------------------------------------------------------------------------------------

[[noreturn]] void HandleCriticalError(const std::string& message) noexcept {
	MessageBox(NULL, message.c_str(), "Critical Error - Terminating", MB_OK | MB_ICONERROR);
	exit(-1);
}

UINT RegisterWindowMessageOrCriticalError(const std::string& message_name) noexcept {
	auto result = RegisterWindowMessage(message_name.c_str());
	if (result == NULL) {
		HandleCriticalError("Registering window message failed. GetLastError() = " + std::to_string(GetLastError()));
	}
	return result;
}

std::string GetHttpResponse(const std::string& url)
{
	const char* user_agent = ""; // TODO?
	HINTERNET hInternet = InternetOpen(user_agent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	HINTERNET hFile = InternetOpenUrl(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);

	std::string response;
	std::vector<char> buffer(1024*1024);
	while(true)
	{
		DWORD bytes_read;
		BOOL ret_val = InternetReadFile(hFile, buffer.data(), static_cast<int>(buffer.size()), &bytes_read);
		if (!ret_val || bytes_read == 0 )
			break;

		buffer[bytes_read] = '\0';
		response += buffer.data();
	}
	InternetCloseHandle(hFile);
	InternetCloseHandle(hInternet);

	return response;
}

void SetClipboardContent(const std::string& content) {
	// TODO: Does this leak?

	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, content.size() + 1);
	if (hMem == NULL) {
		return;
	}

	LPVOID pLocked = GlobalLock(hMem);
	if (pLocked == NULL) {
		return;
	}

	memcpy(pLocked, content.c_str(), content.size() + 1);
	GlobalUnlock(hMem);
	OpenClipboard(gWindows.hWinMain);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);
	CloseClipboard();
}

void Edit_ReduceLines(HWND hEdit, int iLines)
{
	if (iLines <= 0)
		return;
	
	while (SendMessage(hEdit, EM_GETLINECOUNT, 0, 0) > iLines)
	{
		SendMessage(hEdit, EM_SETSEL, 0, 1 + SendMessage(hEdit, EM_LINELENGTH, 0, 0));
		SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM) "");
	}
}

void Edit_ScrollToEnd(HWND hEdit)
{
	auto text_length = Edit_GetTextLength(hEdit);
	Edit_SetSel(hEdit, text_length, text_length);
	Edit_ScrollCaret(hEdit);
}

void SplitIpAddressToBytes(char * szIp, BYTE * pb0, BYTE * pb1, BYTE * pb2, BYTE * pb3)
{
	char * split;
	split = strtok(szIp, ".");
	*pb0 = atoi(split);
	split = strtok(NULL, ".");
	*pb1 = atoi(split);
	split = strtok(NULL, ".");
	*pb2 = atoi(split);
	split = strtok(NULL, "\0");
	*pb3 = atoi(split);
}

std::optional<std::string> GetPb2InstallPath()
{
	for (auto root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
		HKEY key;
		if (RegOpenKeyEx(root, "SOFTWARE\\Digital Paint\\Paintball2", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
		{
			std::string buffer(MAX_PATH, '\0');
			DWORD buffer_size = static_cast<DWORD>(buffer.size());

			auto retVal = RegQueryValueEx(key, "INSTDIR", NULL, NULL, (LPBYTE)buffer.data(), &buffer_size);
			RegCloseKey(key);

			if (retVal == ERROR_SUCCESS && buffer_size >= 1)
			{
				buffer.resize(buffer_size - 1);
				return buffer;
			}
		}
	}

	return std::nullopt;
}

// TODO: Unused
void ListView_SetImage(HWND hListview, std::string_view sImagePath)
{
	LVBKIMAGE LvBkImg = { 0 };

	std::vector<char> buffer(MAX_PATH);
	LvBkImg.pszImage = buffer.data();
	LvBkImg.cchImageMax = static_cast<int>(buffer.size());

	ListView_GetBkImage(hListview, &LvBkImg);
	
	if (sImagePath != LvBkImg.pszImage)
	{
		LvBkImg.ulFlags = LVBKIF_STYLE_NORMAL | LVBKIF_SOURCE_URL;
		LvBkImg.pszImage = (LPSTR)sImagePath.data();
		LvBkImg.cchImageMax = static_cast<int>(sImagePath.length());
		ListView_SetBkImage(hListview, &LvBkImg);
	}
}

void StartServerbrowser(void)
{
	std::optional<std::string> pb2InstallPath = GetPb2InstallPath();
	if (!pb2InstallPath) {
		MainWindowWriteConsole("Could not find the path of your DP:PB2 install directory in the registry.");
		return;
	}

	std::string serverbrowserPath = pb2InstallPath.value() + "\\serverbrowser.exe";
	auto iRet = (INT_PTR) ShellExecute(0, "open", serverbrowserPath.c_str(), "", pb2InstallPath.value().c_str(), 1);
	if (iRet <= 32)
	{
		MainWindowWriteConsole("Error while starting:\r\n" + serverbrowserPath + "\r\nShellExecute returned: " + std::to_string(iRet));
	}
}

void AutoKickTimerFunction() noexcept {
	// todo: mutex -- currently race condition
	std::vector <pb2lib::Server> servers = g_vSavedServers;
	std::vector <AutoKickEntry> autokick_entries = g_vAutoKickEntries;

    for (const auto& server : servers)
	{
		std::vector <pb2lib::Player> players = pb2lib::get_players(server.address, server.rcon_password, gSettings.fTimeoutSecs);
			
        for (const auto& player : players)
		{
			if (gSettings.iAutoKickCheckMaxPingMsecs != 0 && player.ping > gSettings.iAutoKickCheckMaxPingMsecs)
			{
				MainWindowLogPb2LibExceptionsToConsole([&]() {
					std::string command = "kick " + std::to_string(player.number);
					auto response = pb2lib::send_rcon(server.address, server.rcon_password, command, gSettings.fTimeoutSecs);
					MainWindowWriteConsole("Player " + player.name + " on server " + server.hostname + " had a too high ping and was kicked.");
				});
				continue;
			}

			auto autokick_it = std::ranges::find_if(autokick_entries, [&](const AutoKickEntry& entry) {
				return (entry.tType == AutoKickEntry::Type::NAME && strcasecmp(player.name.c_str(), entry.sText.c_str()) == 0)
					|| (entry.tType == AutoKickEntry::Type::ID && player.id == std::stoi(entry.sText));
			});

			if (autokick_it == autokick_entries.end()) {
				continue;
			}

			MainWindowLogPb2LibExceptionsToConsole([&]() {
				std::string command = "kick " + std::to_string(player.number);
				auto response = pb2lib::send_rcon(server.address, server.rcon_password, command, gSettings.fTimeoutSecs);
				MainWindowWriteConsole("Found and kicked player " + player.name + " on server " + server.hostname);
			});
		}
	}
	MainWindowWriteConsole("AutoKick checked all servers.");
}

std::string ConfigLocation() {
	char buffer[MAX_PATH] = { '\0' }; //get path of config file
	GetModuleFileName(GetModuleHandle(NULL), buffer, MAX_PATH);
	buffer[strlen(buffer) - 3] = '\0';
	strcat(buffer, "ini");
	return buffer;
}

void DeleteConfig() // deletes the config file, prints result to console, sets g_bDontWriteConfig to 1
{
	auto path = ConfigLocation();
	if (DeleteFile(path.c_str()))
	{
		MainWindowWriteConsole("The configuration file was deleted successfully.");
	}
	else
	{
		MainWindowWriteConsole("The configuration file could not be removed.");
		MainWindowWriteConsole("You need to delete it yourself:");
		MainWindowWriteConsole(path);
	}
}

int LoadConfig() // loads the servers and settings from the config file
{
	char szReadBuffer[4096];
	auto path = ConfigLocation();

	Settings defaults;

	GetPrivateProfileString("general", "timeout", std::to_string(defaults.fTimeoutSecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.fTimeoutSecs = (float) atof(szReadBuffer);
	
	GetPrivateProfileString("general", "timeoutForNonRconServers", std::to_string(defaults.fAllServersTimeoutSecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.fAllServersTimeoutSecs = (float) atof(szReadBuffer);
	
	GetPrivateProfileString("general", "maxConsoleLineCount", std::to_string(defaults.iMaxConsoleLineCount).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.iMaxConsoleLineCount = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "limitConsoleLineCount", std::to_string(defaults.iMaxConsoleLineCount).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bLimitConsoleLineCount = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "colorPlayers", std::to_string(defaults.bColorPlayers).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bColorPlayers = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "colorPings", std::to_string(defaults.bColorPings).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bColorPings = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "disableConsole", std::to_string(defaults.bDisableConsole).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bDisableConsole = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "autoReloadDelay", std::to_string(defaults.iAutoReloadDelaySecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.iAutoReloadDelaySecs = atoi(szReadBuffer);
	g_AutoReloadTimer.set_interval(gSettings.iAutoReloadDelaySecs);
	
	GetPrivateProfileString("general", "serverlistAddress", defaults.sServerlistAddress.c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.sServerlistAddress = szReadBuffer;
	
	GetPrivateProfileString("bans", "runBanThread", std::to_string(defaults.bAutoKickCheckEnable).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bAutoKickCheckEnable = atoi(szReadBuffer);
	
	GetPrivateProfileString("bans", "delay", std::to_string(defaults.iAutoKickCheckDelay).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.iAutoKickCheckDelay = atoi(szReadBuffer);
	MainWindowUpdateAutoKickState();

	char szCount[10];
	GetPrivateProfileString("server", "count", "-1", szCount, sizeof(szCount), path.c_str());
	if (strcmp (szCount, "-1") == 0 && GetLastError() == 0x2) return -1; //File not Found
	for (int i = 0; i < atoi(szCount); i++) //load servers
	{
		char szKeyBuffer[512] = { 0 };
		char szPortBuffer[6] = { 0 };
		sprintf(szKeyBuffer, "%d", i);
		GetPrivateProfileString("ip", szKeyBuffer, "0.0.0.0", szReadBuffer, sizeof(szReadBuffer), path.c_str());
		GetPrivateProfileString("port", szKeyBuffer, "00000", szPortBuffer, 6, path.c_str());
		pb2lib::Server server;
		server.address.ip = szReadBuffer;
		server.address.port = atoi(szPortBuffer);

		// TODO: Run asynchronously -- immediately add the servers to the combobox, but asynchronously add their hostnames once retrieved.
		server.hostname = pb2lib::get_hostname_or_nullopt(server.address, gSettings.fTimeoutSecs).value_or(unreachable_hostname);

		GetPrivateProfileString("pw", szKeyBuffer, "", szReadBuffer, sizeof(szReadBuffer), path.c_str());
		server.rcon_password = szReadBuffer;

		g_vSavedServers.push_back(server);
		auto index = SendMessage(gWindows.hComboServer, CB_ADDSTRING, 0, (LPARAM)server.hostname.c_str());
		SendMessage (gWindows.hComboServer, CB_SETITEMDATA, index, g_vSavedServers.size() - 1);
	}
	SendMessage(gWindows.hComboServer, CB_SETCURSEL, 0, 0);

	GetPrivateProfileString("bans", "count", "0", szCount, sizeof(szCount), path.c_str());
	for (int i = 0; i < atoi(szCount); i++)
	{
		char szKeyBuffer[512];
		sprintf(szKeyBuffer, "%d", i);
		GetPrivateProfileString("bans", szKeyBuffer, "", szReadBuffer, sizeof(szReadBuffer), path.c_str());
		if (strcmp(szReadBuffer, "") == 0) {
			return -2;
		}

		AutoKickEntry ban;
		ban.sText = szReadBuffer;

		sprintf(szKeyBuffer, "%dtype", i);
		GetPrivateProfileString("bans", szKeyBuffer, "-1", szReadBuffer, 6, path.c_str());
		if (strcmp(szReadBuffer, "-1") == 0) {
			return -2;
		}
		ban.tType = (AutoKickEntry::Type)atoi(szReadBuffer);
		g_vAutoKickEntries.push_back(ban);
	}
	return 1;
}

void SaveConfig() // Saves all servers and settings in the config file
{
	// TODO: Store and reload max ping for auto-kick?
	// TODO: Update ini paths with renamed program terminology (e.g. ban vs autokick)

	MainWindowWriteConsole("Saving configuration file...");
	auto path = ConfigLocation();

	//clear old config so servers & bans that are not used anymore are not occupying any disk space:
	WritePrivateProfileString("ip", NULL, NULL, path.c_str());
	WritePrivateProfileString("pw", NULL, NULL, path.c_str());
	WritePrivateProfileString("port", NULL, NULL, path.c_str());
	WritePrivateProfileString("bans", NULL, NULL, path.c_str());
	
	std::string sWriteBuffer = std::to_string(g_vSavedServers.size());
	if (!WritePrivateProfileString("server", "count", sWriteBuffer.c_str(), path.c_str()))
		return;

	for (unsigned int i = 0; i< g_vSavedServers.size(); i++) //write servers to it
	{
		std::string sKeyBuffer = std::to_string(i);
		std::string sPortBuffer = std::to_string(g_vSavedServers[i].address.port);
		WritePrivateProfileString("ip", sKeyBuffer.c_str(), g_vSavedServers[i].address.ip.c_str(), path.c_str());
		WritePrivateProfileString("pw", sKeyBuffer.c_str(), g_vSavedServers[i].rcon_password.c_str(), path.c_str());
		WritePrivateProfileString("port", sKeyBuffer.c_str(), sPortBuffer.c_str(), path.c_str());
	}

	sWriteBuffer = std::to_string(gSettings.fTimeoutSecs);
	WritePrivateProfileString("general", "timeout", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.fAllServersTimeoutSecs);
	WritePrivateProfileString("general", "timeoutForNonRconServers", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.iMaxConsoleLineCount);
	WritePrivateProfileString("general", "maxConsoleLineCount", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bLimitConsoleLineCount);
	WritePrivateProfileString("general", "limitConsoleLineCount", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.iAutoReloadDelaySecs);
	WritePrivateProfileString("general", "autoReloadDelay", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bColorPlayers);
	WritePrivateProfileString("general", "colorPlayers", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bColorPings);
	WritePrivateProfileString("general", "colorPings", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bDisableConsole);
	WritePrivateProfileString("general", "disableConsole", sWriteBuffer.c_str(), path.c_str());
	
	WritePrivateProfileString("general", "serverlistAddress", gSettings.sServerlistAddress.c_str(), path.c_str());
	
	sWriteBuffer = std::to_string(gSettings.bAutoKickCheckEnable);
	WritePrivateProfileString("bans", "runBanThread", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.iAutoKickCheckDelay);
	WritePrivateProfileString("bans", "delay", sWriteBuffer.c_str(), path.c_str());

	std::string sKeyBuffer = std::to_string(g_vAutoKickEntries.size());
	WritePrivateProfileString("bans", "count", sKeyBuffer.c_str(), path.c_str());

	for (unsigned int i = 0; i < g_vAutoKickEntries.size(); i++)
	{
		sKeyBuffer = std::to_string(i);
		WritePrivateProfileString("bans", sKeyBuffer.c_str(), g_vAutoKickEntries[i].sText.c_str(), path.c_str());

		sKeyBuffer = std::to_string(i) + "type";
		sWriteBuffer = std::to_string(static_cast<int>(g_vAutoKickEntries[i].tType));
		WritePrivateProfileString("bans", sKeyBuffer.c_str(), sWriteBuffer.c_str(), path.c_str());
	}
}

size_t GetFirstUnusedMapKey (const std::map<size_t, HANDLE>& m)
{	
	for (size_t key = 0; key <= m.size() + 1; key++)
	{
		if (!m.contains(key))
			return key;
	}
	assert(false);
	return -1;
}