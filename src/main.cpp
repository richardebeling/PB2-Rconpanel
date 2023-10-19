//TODO: Add: Modifiable messages that will be said as console before players are kicked.
//TODO: Add: Dialog that can be used to change servers settings and save current settings as configuration file for a server
//TODO: IDs and IPs Dialog: Show information about these numbers (maybe a button that links to dplogin / ip whois?)

//TODO: Save and restore window position?

// TODO: Make everything in the main window accessible via keyboard through menus?

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
#include <charconv>
#include <array>
#include <filesystem>

using namespace std::string_literals;
using namespace std::chrono_literals;

std::mutex g_ThreadGlobalReadMutex; // Locked by helper threads accessing the following globals. Also locked by the main thread when writing to them

std::vector<std::unique_ptr<Server>> g_ServersWithRcon;
std::vector<std::unique_ptr<Server>> g_Serverlist;
std::vector<std::unique_ptr<AutoKickEntry>> g_vAutoKickEntries;

// end of variables protected by g_ThreadGlobalReadMutex. All following variables may only be accessed by the main thread.

std::future<ServerCvars> g_FetchServerCvarsFuture;
std::future<std::vector<pb2lib::Player>> g_FetchPlayersFuture;

std::vector<std::future<std::string>> g_RconResponses;
std::vector<pb2lib::Player> g_vPlayers;

std::future<std::vector<std::unique_ptr<Server>>> g_ServerlistFuture;

AsyncRepeatedTimer g_AutoReloadTimer;
AsyncRepeatedTimer g_AutoKickTimer;

pb2lib::AsyncHostnameResolver g_HostnameResolver;

// read-only after program initialization. Can be read by threads.
UINT WM_REFETCHPLAYERS, WM_SERVERCHANGED, WM_PLAYERSREADY, WM_SERVERCVARSREADY, WM_RCONRESPONSEREADY, WM_HOSTNAMEREADY, WM_SERVERLISTREADY, WM_AUTOKICKENTRYADDED;

DeleteObjectRAIIWrapper<HFONT> g_MainFont, g_MonospaceFont;

WindowHandles gWindows;
Settings gSettings;

ULONG_PTR g_gdiplusStartupToken;
std::unique_ptr<Gdiplus::Bitmap> g_pMapshotBitmap;


Server::operator std::string() const {
	std::string display_hostname = "No response";
	if (hostname.valid() && hostname.wait_for(0s) != std::future_status::timeout) {
		MainWindowLogExceptionsToConsole([&]() {
			display_hostname = hostname.get();
		}, "getting hostname");
	}

	return display_hostname + " [" + static_cast<std::string>(address) + "]";
}

AutoKickEntry AutoKickEntry::from_type_and_value(std::string_view type, std::string_view value) {
	AutoKickEntry result;
	// "0" and "1" are backwards compatibility for < v1.4.0
	if (type == "id" || type == "0") {
		AutoKickEntry::IdT parsed;
		std::from_chars(value.data(), value.data() + value.size(), parsed);
		result.value = parsed;
	}
	else if (type == "name" || type == "1") {
		result.value = AutoKickEntry::NameT(value);
	}
	else {
		assert(false);
	}
	return result;
}



bool AutoKickEntry::matches(AutoKickEntry::IdT id) const {
	return std::visit(Overload(
		[&](const IdT& stored_id) { return stored_id == id; },
		[](const NameT&) { return false; }
	), value);
}

bool AutoKickEntry::matches(AutoKickEntry::NameT name) const {
	return std::visit(Overload(
		[](const IdT&) { return false; },
		[&](const NameT& stored_name) { return strcasecmp(stored_name.c_str(), name.c_str()) == 0; }
	), value);
}

std::string AutoKickEntry::type_string() const {
	return std::visit(Overload(
		[](const IdT& id) { return "id"; },
		[](const NameT& name) { return "name"; }
	), value);
}

std::string AutoKickEntry::value_string() const {
	return std::visit(Overload(
		[](const IdT& id) { return std::to_string(id); },
		[](const NameT& name) { return name; }
	), value);
}

AutoKickEntry::operator std::string() const {
	return std::visit(Overload(
		[](const IdT& id) { return "ID: " + std::to_string(id); },
		[](const NameT& name) { return "Name: " + name; }
	), value);
}


ServerCvars ServerCvars::from_server(const Server& server, std::chrono::milliseconds timeout) {
	const std::vector<std::string> status_vars = { "mapname", "password", "elim", "timelimit", "maxclients" };
	auto values = pb2lib::get_cvars(server.address, server.rcon_password, status_vars, timeout);

	return ServerCvars{
		.mapname = values[0],
		.password = values[1],
		.elim = atoi(values[2].c_str()),
		.timelimit = atoi(values[3].c_str()),
		.maxclients = atoi(values[4].c_str()),
	};
}


//--------------------------------------------------------------------------------------------------
// Program Entry Point                                                                             |
//{-------------------------------------------------------------------------------------------------

#ifdef _DEBUG
int main() {
	WinMain(GetModuleHandle(NULL), NULL, (PSTR)"", SW_NORMAL);
}
#endif

#pragma warning (suppress : 28251)
int WINAPI WinMain (HINSTANCE hThisInstance, HINSTANCE hPrevInstance, PSTR lpszArgument, int nCmdShow)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	INITCOMMONCONTROLSEX icex = { 0 }; // needed for list view control
	icex.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);

	WM_REFETCHPLAYERS = RegisterWindowMessageOrCriticalError("RCONPANEL_REFETCHPLAYERS");
	WM_PLAYERSREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_PLAYERSREADY");
	WM_SERVERCHANGED = RegisterWindowMessageOrCriticalError("RCONPANEL_SERVERCHANGED");
	WM_SERVERCVARSREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_SERVERCVARSREADY");
	WM_RCONRESPONSEREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_RCONRESPONSEREADY");
	WM_HOSTNAMEREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_HOSTNAMEREADY");
	WM_SERVERLISTREADY = RegisterWindowMessageOrCriticalError("RCONPANEL_SERVERLISTREADY");
	WM_AUTOKICKENTRYADDED = RegisterWindowMessageOrCriticalError("RCONPANEL_AUTOKICKENTRYADDED");
	
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

	// TODO: Get initial window size from dialog size also?
	DWORD dwBaseUnits = GetDialogBaseUnits();
	CreateWindowEx (0, classname, "DP:PB2 Rconpanel",
					WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
					MulDiv(285, LOWORD(dwBaseUnits), 4), MulDiv(290, HIWORD(dwBaseUnits), 8),
					HWND_DESKTOP, LoadMenu (hThisInstance, MAKEINTRESOURCE(IDM)),
					hThisInstance, NULL);
	// All UI elements are created in OnMainWindowCreate

	ShowWindow(gWindows.hWinMain, nCmdShow);

	MSG message;
	while (GetMessage(&message, NULL, 0, 0)) {
		// Tried playing with IsDialogMessage for the main window here to allow for nice keyboard navigation
		// * It takes the SELENDOK of the rcon input and tries to submit using the default push button.
		//     Correctly maintaining the default push button (none or the "Send" button) seems complicated.
		//     DM_SETDEFID on focus/unfocus of the rcon input didn't work.
		// * Pressing the alt-key will show the mnemonics / keyboard shortcuts for all buttons, but they will never hide again
		//	   Manually hiding them correctly doesn't seem to be easy (tried WM_CHANGEUISTATE, but they were only hidden after a successive draw?)
		//     Especially annoying: Alt-tab will trigger them
		// * Mnemonics will only work if some child window is selected, which isn't the case after startup or after alt-tabbing out and back in.
		// See https://devblogs.microsoft.com/oldnewthing/20031021-00/?p=42083
		// TODO: Maybe check out how other native WinAPI guis handle this.

		HWND hwndFocus = GetFocus();
		bool isKeyMessage = (message.message == WM_KEYDOWN || message.message == WM_KEYUP);
		if (isKeyMessage && HasClass(hwndFocus, "ListBox") && HasStyle(hwndFocus, LBS_WANTKEYBOARDINPUT)) {
			// ListBoxes with LBS_WANTKEYBOARDINPUT should handle their inputs even in dialogs.
		} else {
			if (IsDialogMessage(gWindows.hDlgAutoKickEntries, &message)) continue;
			if (IsDialogMessage(gWindows.hDlgBannedIps, &message)) continue;
			if (IsDialogMessage(gWindows.hDlgRconCommands, &message)) continue;
			if (IsDialogMessage(gWindows.hDlgRotation, &message)) continue;
			if (IsDialogMessage(gWindows.hDlgServers, &message)) continue;
			if (IsDialogMessage(gWindows.hDlgSettings, &message)) continue;
		}

		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	return 0;
}

//}-------------------------------------------------------------------------------------------------
// Main Window Functions                                                                           |
//{-------------------------------------------------------------------------------------------------

// TODO: Namespacing instead of function name prefixing

void MainWindowLogExceptionsToConsole(std::function<void()> func, std::string_view action_description) {
	try {
		func();
	}
	catch (pb2lib::Exception& e) {
		MainWindowWriteConsole("Error (" + std::string(action_description) + "): " + e.what());
	}
	catch (RconpanelException& e) {
		MainWindowWriteConsole("Error (" + std::string(action_description) + "): " + e.what());
	}
}

void MainWindowSendRcon(const std::string& command) noexcept {
	const Server* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	const pb2lib::Address& address = server->address;
	const std::string rcon_password = server->rcon_password;
	const auto timeout = gSettings.timeout;
	const HWND hwnd = gWindows.hWinMain;

	std::promise<std::string> promise;
	g_RconResponses.push_back(promise.get_future());

	std::thread thread([address, rcon_password, command, timeout, hwnd](std::promise<std::string> promise) {
		try {
			promise.set_value(pb2lib::send_rcon(address, rcon_password, command, timeout));
		}
		catch (pb2lib::Exception&) {
			promise.set_exception(std::current_exception());
		}
		PostMessage(hwnd, WM_RCONRESPONSEREADY, 0, 0);
		}, std::move(promise));
	thread.detach();
	MainWindowWriteConsole("rcon " + command);
}

void MainWindowAddOrUpdateOwnedServer(const Server* stable_server_ptr) noexcept {
	const Server& server = *stable_server_ptr;
	const std::string display_string = static_cast<std::string>(server);

	const auto selected_index = ComboBox_GetCurSel(gWindows.hComboServer);
	int found_index = ComboBox_CustomFindItemData(gWindows.hComboServer, stable_server_ptr);

	if (found_index >= 0) {
		std::vector<char> buffer(1ull + ComboBox_GetLBTextLen(gWindows.hComboServer, found_index));
		ComboBox_GetLBText(gWindows.hComboServer, found_index, buffer.data());
		if (std::string_view(buffer.data(), buffer.size() - 1) == display_string) {
			return;
		}
		ComboBox_DeleteString(gWindows.hComboServer, found_index);
	}

	const auto created_index = ComboBox_AddString(gWindows.hComboServer, display_string.c_str());
	ComboBox_SetItemData(gWindows.hComboServer, created_index, stable_server_ptr);

	if (selected_index == -1) {
		ComboBox_SetCurSel(gWindows.hComboServer, 0);
	}
	else if (selected_index == found_index) {
		ComboBox_SetCurSel(gWindows.hComboServer, created_index);
	}
}

void MainWindowRemoveOwnedServer(const Server* stored_server_ptr) noexcept {
	const auto found_index = ComboBox_CustomFindItemData(gWindows.hComboServer, stored_server_ptr);
	const auto selected_index = ComboBox_GetCurSel(gWindows.hComboServer);
	ComboBox_DeleteString(gWindows.hComboServer, found_index);

	if (found_index == selected_index) {
		const auto new_index = min(ComboBox_GetCount(gWindows.hComboServer) - 1, selected_index);
		ComboBox_SetCurSel(gWindows.hComboServer, new_index);
	}
}

void MainWindowRefetchHostnames() noexcept {
	HWND hwnd = gWindows.hWinMain;
	UINT message = WM_HOSTNAMEREADY;

	for (const auto& server_ptr : g_ServersWithRcon) {
		Server* raw_server_ptr = server_ptr.get();
		server_ptr->hostname = g_HostnameResolver.resolve(server_ptr->address, [hwnd, message, raw_server_ptr](const std::string& resolved_hostname) {
			PostMessage(hwnd, message, 0, (LPARAM)raw_server_ptr);
		});
	}
}

void MainWindowUpdateAutoKickState() noexcept {
	HMENU menu = GetMenu(gWindows.hWinMain);

	CheckMenuItem(menu, IDM_AUTOKICK_ENABLE, gSettings.bAutoKickCheckEnable ? MF_CHECKED : MF_UNCHECKED);
	g_AutoKickTimer.set_interval(gSettings.bAutoKickCheckEnable ? gSettings.iAutoKickCheckDelay : 0);

	CheckMenuItem(menu, IDM_AUTOKICK_SETPING, gSettings.iAutoKickCheckMaxPingMsecs != 0 ? MF_CHECKED : MF_UNCHECKED);
}

Server* MainWindowGetSelectedServerOrLoggedNull() noexcept {
	if (g_ServersWithRcon.size() == 0) {
		MainWindowWriteConsole("There are no servers in your server list.");
		return nullptr;
	}

	auto selected_index = ComboBox_GetCurSel(gWindows.hComboServer);
	auto selectedServerPtr = ComboBox_GetItemData(gWindows.hComboServer, selected_index);
	if (selectedServerPtr == CB_ERR) {
		MainWindowWriteConsole("Error when trying to get the selected server");
		return nullptr;
	}

	return reinterpret_cast<Server*>(selectedServerPtr);
}

pb2lib::Player* MainWindowGetSelectedPlayerOrNull() noexcept {
	auto iSelectedItem = ListView_GetNextItem(gWindows.hListPlayers, -1, LVNI_SELECTED);
	if (iSelectedItem == -1) {
		return nullptr;
	}

	LVITEM item = { 0 };
	item.iItem = iSelectedItem;
	item.mask = LVIF_PARAM;
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
		const std::string player_site = GetHttpResponse("http://www.dplogin.com/index.php?action=viewmember&playerid=" + std::to_string(player->id.value()));
		const std::regex rx ("<tr><td><b class=\"faqtitle\">(.+?:)</b></td><td>(.+?)</td></tr>");
		//													1-VARNAME		2-CONTENT
				
		const auto begin = std::sregex_iterator(player_site.begin(), player_site.end(), rx);
		const auto end = std::sregex_iterator{};
		for (auto it = begin; it != end; ++it) {
			const std::smatch match = *it;
			
			sBoxContent += match[1];
			sBoxContent += " ";
			
			std::string sContent = match[2];
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
	std::string sTitle = "About - DP:PB2 Rconpanel v" + std::to_string(Version::MAJOR)
						+ "." + std::to_string(Version::MINOR) + "." + std::to_string(Version::BUILD);

	MessageBox(hwnd,"Remote administration tool for Digital Paint: Paintball 2 servers. "
					"The source code is released under GPLv3 here:\r\n"
					"https://github.com/richardebeling/PB2-Rconpanel\r\n"
					"If there are any questions, feel free to contact me (issue, email, discord, ...).",
					sTitle.c_str(),
					MB_OK | MB_ICONINFORMATION);
}

void MainWindowRefetchServerInfo() noexcept {
	g_AutoReloadTimer.reset_current_timeout();

	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	auto fetch_players_thread_function = [](std::promise<std::vector<pb2lib::Player>> promise, Server server, HWND window, std::chrono::milliseconds timeout) {
		try {
			promise.set_value(pb2lib::get_players(server.address, server.rcon_password, timeout));
		}
		catch (pb2lib::Exception&) {
			promise.set_exception(std::current_exception());
		}
		PostMessage(window, WM_PLAYERSREADY, 0, 0);
	};

	std::promise<std::vector<pb2lib::Player>> players_promise;
	g_FetchPlayersFuture = players_promise.get_future();
	std::jthread fetch_players_thread(fetch_players_thread_function, std::move(players_promise), *server, gWindows.hWinMain, gSettings.timeout);
	fetch_players_thread.detach();

	auto fetch_cvars_thread_function = [](std::promise<ServerCvars> promise, Server server, HWND window, std::chrono::milliseconds timeout) {
		try {
			promise.set_value(ServerCvars::from_server(server, timeout));
		}
		catch (pb2lib::Exception&) {
			promise.set_exception(std::current_exception());
		}
		PostMessage(window, WM_SERVERCVARSREADY, 0, 0);
	};

	std::promise<ServerCvars> cvars_promise;
	g_FetchServerCvarsFuture = cvars_promise.get_future();
	std::jthread fetch_cvars_thread(fetch_cvars_thread_function, std::move(cvars_promise), *server, gWindows.hWinMain, gSettings.timeout);
	fetch_cvars_thread.detach();
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
	Edit_SetSel(gWindows.hEditConsole, -2, -2);
	DWORD start = 0, end = 0;
	SendMessage(gWindows.hEditConsole, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
	if (start != 0)
		Edit_ReplaceSel(gWindows.hEditConsole, "\r\n");

	// Add new text
	Edit_ReplaceSel(gWindows.hEditConsole, formatted.c_str());

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
	g_AutoReloadTimer.set_trigger_action([hwnd](){ PostMessage(hwnd, WM_REFETCHPLAYERS, 0, 0); });
	g_AutoKickTimer.set_trigger_action([](){ MainWindowLogExceptionsToConsole(AutoKickTimerFunction, "performing auto-kick checks"); });

	gWindows.hWinMain = hwnd;

	//The following controls will be resized when the window is shown and HandleResize is called.
	gWindows.hStaticServer = CreateWindowEx(0, "STATIC", "Server: ", SS_SIMPLE | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);

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

	int dpi = GetDpiForWindow(gWindows.hWinMain);
	RECT window_rect;
	GetWindowRect(gWindows.hWinMain, &window_rect);
	SendMessage(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&window_rect);

	Edit_LimitText(gWindows.hEditConsole, 0);

	LVCOLUMN lvc = { 0 };
	std::string buffer;
	lvc.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
	for (int i = 0; i <= 7; i++)
	{
		lvc.iSubItem = i;
		lvc.fmt = LVCFMT_RIGHT;
		switch (i)
		{
			case Subitems::NUMBER: buffer = "Num";   break;
			case Subitems::NAME:   buffer = "Name";  lvc.fmt = LVCFMT_LEFT; break;
			case Subitems::BUILD:  buffer = "Build"; break;
			case Subitems::ID:     buffer = "ID";    break;
			case Subitems::OP:     buffer = "OP";    break;
			case Subitems::IP:     buffer = "IP";    lvc.fmt = LVCFMT_LEFT; break;
			case Subitems::PING:   buffer = "Ping";  break;
			case Subitems::SCORE:  buffer = "Score"; break;
		}
		lvc.pszText = const_cast<char*>(buffer.c_str());
		ListView_InsertColumn(gWindows.hListPlayers, i, &lvc);
	}
	ListView_SetExtendedListViewStyle(gWindows.hListPlayers, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	MainWindowLogExceptionsToConsole([]() {
		LoadConfig();
		MainWindowWriteConsole("Configuration loaded.");
	}, "loading configuration");
	
	MainWindowRefetchHostnames();

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

	MainWindowLogExceptionsToConsole([&]() {
		auto updated_players = pb2lib::get_players_from_rcon_sv_players(server->address, server->rcon_password, gSettings.timeout);
		auto matching_updated_player_it = std::ranges::find_if(updated_players, [&](const pb2lib::Player& updated_player) {
			return updated_player.number == player->number && updated_player.name == player->name; });

		if (matching_updated_player_it == updated_players.end()) {
			MainWindowWriteConsole("It seems like the player disconnected. They were not forcejoined.");
			return;
		}

		MainWindowSendRcon("sv forcejoin " + std::to_string(player->number) + " " + (char)iSelectedColor);
	}, "force-joining");
}

void OnMainWindowSendRcon(void)
{
	std::string command(1ull + ComboBox_GetTextLength(gWindows.hComboRcon), '\0');
	ComboBox_GetText(gWindows.hComboRcon, command.data(), static_cast<int>(command.size()));
	MainWindowSendRcon(command);
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

	const std::string args = "+connect " + std::string(server->address);
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

	MainWindowLogExceptionsToConsole([&]() {
		auto updated_players = pb2lib::get_players_from_rcon_sv_players(server->address, server->rcon_password, gSettings.timeout);
		auto matching_updated_player_it = std::ranges::find_if(updated_players, [&](const pb2lib::Player& updated_player) {
			return updated_player.number == player->number && updated_player.name == player->name; });

		if (matching_updated_player_it == updated_players.end()) {
			MainWindowWriteConsole("It seems like the player disconnected. They were not kicked.");
			return;
		}

		MainWindowSendRcon("kick " + std::to_string(player->number));
	}, "kicking");
}

void OnMainWindowBanIP(void)
{
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!player) {
		return;
	}

	if (!player->address) {
		MainWindowWriteConsole("The player's IP was not loaded correctly. Please reload.");
		return;
	}

	MainWindowSendRcon("sv addip " + player->address->ip);
}

void OnMainWindowAutoKick(void) {
	auto* player = MainWindowGetSelectedPlayerOrLoggedNull();
	if (!player) {
		return;
	}

	AutoKickEntry entry;
	if (player->id) {
		entry.value = *player->id;
		MainWindowWriteConsole("AutoKick entry added for ID " + std::to_string(*player->id));
	}
	else {
		entry.value = player->name;
		MainWindowWriteConsole("AutoKick entry added for name " + player->name);
	}

	std::lock_guard guard(g_ThreadGlobalReadMutex);
	g_vAutoKickEntries.push_back(std::make_unique<AutoKickEntry>(entry));

	SendMessage(gWindows.hDlgAutoKickEntries, WM_AUTOKICKENTRYADDED, 0, 0);
}

void OnMainWindowPlayersReady() noexcept {
	// Message might be from an outdated / older thread -> only process if the current future is ready
	if (g_FetchPlayersFuture.valid() && g_FetchPlayersFuture.wait_for(0s) != std::future_status::timeout) {
		MainWindowLogExceptionsToConsole([&]() {
			g_vPlayers = g_FetchPlayersFuture.get();
			MainWindowUpdatePlayersListview();
			MainWindowWriteConsole("The player list was reloaded.");
		}, "loading players");
	}
}

void OnMainWindowServerCvarsReady() noexcept {
	if (g_FetchServerCvarsFuture.valid() && g_FetchServerCvarsFuture.wait_for(0s) != std::future_status::timeout) {
		std::string display_text = "Error";

		MainWindowLogExceptionsToConsole([&]() {
			auto values = g_FetchServerCvarsFuture.get();
			display_text = "map: " + values.mapname;
			display_text += " | pw: " + (values.password.size() > 0 ? values.password : "none");
			display_text += " | elim: " + std::to_string(values.elim);
			display_text += " | timelimit: " + std::to_string(values.timelimit);
			display_text += " | maxclients: " + std::to_string(values.maxclients);
		}, "getting cvars");

		SetWindowText(gWindows.hStaticServerInfo, display_text.c_str());
		DeleteObjectRAIIWrapper<HRGN> region(CreateRectRgn(0, 0, 0, 0));
		GetWindowRgn(gWindows.hStaticServerInfo, region);
		RedrawWindow(gWindows.hWinMain, NULL, region, RDW_ERASE | RDW_INVALIDATE);
	}
}

void OnMainWindowRconResponseReady() noexcept {
	for (auto& future : g_RconResponses) {
		if (future.wait_for(0s) == std::future_status::timeout) {
			continue;
		}
		MainWindowLogExceptionsToConsole([&]() {
			std::string response = future.get();
			MainWindowWriteConsole(response);
		}, "getting rcon response");
	}
	std::erase_if(g_RconResponses, [](const auto& future) { return !future.valid(); });
}

void OnMainWindowHostnameReady(Server* server_instance) noexcept {
	std::lock_guard guard(g_ThreadGlobalReadMutex);
	for (const auto& server_ptr : g_ServersWithRcon) {
		if (server_ptr.get() == server_instance) {
			MainWindowAddOrUpdateOwnedServer(server_ptr.get());
		}
	}
}

void OnMainWindowDestroy(HWND hwnd)
{
	SaveConfig();

	OleUninitialize();
	
	g_pMapshotBitmap.reset();
	Gdiplus::GdiplusShutdown(g_gdiplusStartupToken);
	
	PostQuitMessage(0);
}

void OnMainWindowCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (codeNotify) {
		case BN_CLICKED: {
			if (hwndCtl == gWindows.hButtonSend) OnMainWindowSendRcon();
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
	
		case CBN_SELENDOK: {
			if (hwndCtl == gWindows.hComboRcon) OnMainWindowSendRcon();
			if (hwndCtl == gWindows.hComboServer) {
				PostMessage(hwnd, WM_SERVERCHANGED, 0, 0);
				PostMessage(gWindows.hDlgRotation, WM_SERVERCHANGED, 0, 0);
				PostMessage(gWindows.hDlgBannedIps, WM_SERVERCHANGED, 0, 0);
			}
			break;
		}

		case CBN_SETFOCUS: {
			if (hwndCtl == gWindows.hComboRcon) {
				Button_SetStyle(gWindows.hButtonSend, BS_DEFPUSHBUTTON, true);
			}
			break;
		}
		case CBN_KILLFOCUS: {
			if (hwndCtl == gWindows.hComboRcon) {
				Button_SetStyle(gWindows.hButtonSend, BS_PUSHBUTTON, true);
			}
			break;
		}
	}	
	
	switch (id) {
		case IDM_FILE_EXIT:
			SendMessage(hwnd, WM_CLOSE, 0, 0);
			break;
		case IDM_FILE_SETTINGS:
			if (!gWindows.hDlgSettings)
				gWindows.hDlgSettings = CreateDialog(NULL, MAKEINTRESOURCE(IDD_SETTINGS), hwnd, (DLGPROC) SettingsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgSettings);
			break;
		case IDM_SERVER_EDITSERVERS:
			if (!gWindows.hDlgServers)
				gWindows.hDlgServers = CreateDialog(NULL, MAKEINTRESOURCE(IDD_SERVERS), hwnd, ServersDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgServers);
			break;
		case IDM_SERVER_ROTATION:
			if (!gWindows.hDlgRotation)
				gWindows.hDlgRotation = CreateDialog(NULL, MAKEINTRESOURCE(IDD_ROTATION), hwnd, RotationDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgRotation);			
			break;
		case IDM_SERVER_BANNEDIPS:
			if (!gWindows.hDlgBannedIps)
				gWindows.hDlgBannedIps = CreateDialog(NULL, MAKEINTRESOURCE(IDD_BANNEDIPS), hwnd, BannedIPsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgBannedIps);
			break;
		case IDM_AUTOKICK_ENABLE:
			gSettings.bAutoKickCheckEnable = GetMenuState(GetMenu(gWindows.hWinMain), IDM_AUTOKICK_ENABLE, MF_BYCOMMAND) != SW_SHOWNA;
			MainWindowUpdateAutoKickState();
			break;
		case IDM_AUTOKICK_SETPING:
			DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETPING), hwnd, SetMaxPingDlgProc);
			break;
		case IDM_AUTOKICK_EDITENTRIES:
			if (!gWindows.hDlgAutoKickEntries)
				gWindows.hDlgAutoKickEntries = CreateDialog(NULL, MAKEINTRESOURCE(IDD_AUTOKICK_ENTRIES), hwnd, AutoKickEntriesDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgAutoKickEntries);
			break;
		case IDM_HELP_DPLOGIN:
			ShellExecute(NULL, "open", "http://www.dplogin.com", NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_HELP_RCONCOMMANDS:
			if (!gWindows.hDlgRconCommands)
				gWindows.hDlgRconCommands = CreateDialog(NULL, MAKEINTRESOURCE(IDD_RCONCOMMANDS), hwnd, RCONCommandsDlgProc);
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
				auto* player = MainWindowGetSelectedPlayerOrNull();
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

	// TODO: Make DPI aware and mark as DPI aware?
	// TODO: Fix clipping with DPI scaling enabled

    // TODO: Add: calculate it all from a few, for humans readable areas (Server, Player, Console) so editing is easier.
    //RECT rcServer =  {3*iMW, 3*iMH                   , cx - 3*iMW, 23*iMH};
    //RECT rcPlayers = {3*iMW, rcServer.bottom + 2*iMH , cx - 3*iMW, ((cy > 244*iMH) ? cy/2-20*iMH : 102*iMH) + 25*iMH};
    //RECT rcConsole = {3*iMW, rcPlayers.bottom + 2*iMH, cx - 3*iMW, cy - 3*iMH};
    //printf ("Server : left: %d; top: %d; right: %d; bottom: %d\n", rcServer.left, rcServer.top, rcServer.right, rcServer.bottom);
    //printf ("Player : left: %d; top: %d; right: %d; bottom: %d\n", rcPlayers.left, rcPlayers.top, rcPlayers.right, rcPlayers.bottom);
    //printf ("Console: left: %d; top: %d; right: %d; bottom: %d\n", rcConsole.left, rcConsole.top, rcConsole.right, rcConsole.bottom);

	MoveWindow(gWindows.hStaticServer, 3 * iMW, 4 * iMH, 30 * iMW, 8 * iMH, false);
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

HBRUSH OnMainWindowCtlColorStatic(HWND hwnd, HDC hdc, HWND hwndChild, int type_string)
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

void OnMainWindowDpiChanged(HWND hwnd, int newDpiX, int newDpiY, RECT* suggestedPos) {
	SetWindowPos(hwnd, NULL,
		suggestedPos->left,
		suggestedPos->top,
		suggestedPos->right - suggestedPos->left,
		suggestedPos->bottom - suggestedPos->top,
		SWP_NOZORDER | SWP_NOACTIVATE);

	constexpr int fontSize = 9;

	HDC hdc = GetDC(hwnd);
	LONG lfHeight = -MulDiv(fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(NULL, hdc);
	g_MainFont = CreateFont(lfHeight, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0, "Segoe UI");
	g_MonospaceFont = CreateFont(lfHeight, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0, "Consolas");

	EnumChildWindows(hwnd, EnumWindowsSetFontCallback, (LPARAM)(HFONT)g_MainFont);
	SendMessage(gWindows.hEditConsole, WM_SETFONT, (WPARAM)(HFONT)g_MonospaceFont, true);


	if (gWindows.hDlgDummy) {
		EndDialog(gWindows.hDlgDummy, 0);
	}
	gWindows.hDlgDummy = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DUMMY), hwnd, NULL);

	/*
	RECT rect = { 0 }, rect2 = { 0 };
	GetWindowRect(GetDlgItem(gWindows.hDlgDummy, IDC_DUMMY_BUTTON), &rect);
	gDimensions.button_height = rect.bottom - rect.top;
	gDimensions.button_width = rect.right - right.left;

	GetWindowRect(GetDlgItem(gWindows.hDlgDummy, IDC_DUMMY_BUTTON_PADDINGLARGE), &rect2);
	gDimensions.padding_large = rect2.top - rect.top - gDimensions.button_height;

	GetWindowRect(GetDlgItem(gWindows.hDlgDummy, IDC_DUMMY_BUTTON_PADDINGLARGE), &rect2);
	gDimensions.padding_large = rect2.top - rect.top - gDimensions.button_height;


	// TODO: store globally, use in WM_SIZE handler
	/*
	* Static height
	* Static offset (button top to static top)
	* Padding large
	* Padding small
	* Padding tiny
	*/

	SendMessage(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(0ull + suggestedPos->right - suggestedPos->left, 0ull + suggestedPos->bottom - suggestedPos->top));
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
		HANDLE_MSG(hwnd, WM_CREATE, OnMainWindowCreate);
		HANDLE_MSG(hwnd, WM_DESTROY, OnMainWindowDestroy);
		HANDLE_MSG(hwnd, WM_COMMAND, OnMainWindowCommand);
		HANDLE_MSG(hwnd, WM_NOTIFY, OnMainWindowNotify);
		HANDLE_MSG(hwnd, WM_SIZE, OnMainWindowSize);
		HANDLE_MSG(hwnd, WM_GETMINMAXINFO, OnMainWindowGetMinMaxInfo);
		HANDLE_MSG(hwnd, WM_CTLCOLORSTATIC, OnMainWindowCtlColorStatic);

		case WM_DPICHANGED: {
			// Can't get windows to send this to us, even with PerMonitorV2 set up. Currently only used for initial scaling at program startup.
			OnMainWindowDpiChanged(hwnd, LOWORD(wParam), HIWORD(wParam), (RECT*)lParam);
			return 0;
		}
	}

	if (message == WM_REFETCHPLAYERS) { MainWindowRefetchServerInfo(); return 0; };
	if (message == WM_SERVERCHANGED) { MainWindowRefetchServerInfo(); return 0; };
	if (message == WM_PLAYERSREADY) { OnMainWindowPlayersReady(); return 0; };
	if (message == WM_SERVERCVARSREADY) { OnMainWindowServerCvarsReady(); return 0; };
	if (message == WM_RCONRESPONSEREADY) { OnMainWindowRconResponseReady(); return 0; };
	if (message == WM_HOSTNAMEREADY) { OnMainWindowHostnameReady((Server*)lParam); return 0; };

	return DefWindowProc(hwnd, message, wParam, lParam);
}

//}-------------------------------------------------------------------------------------------------
// Callback Set Ping Dialog                                                                        |
//{-------------------------------------------------------------------------------------------------

BOOL OnSetMaxPingDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	std::string sMaxPing = std::to_string(gSettings.iAutoKickCheckMaxPingMsecs);
	SetDlgItemText(hwnd, IDC_SP_EDIT, sMaxPing.c_str());
	return TRUE;
}

void OnSetMaxPingDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	switch (id) {
		case IDC_SP_BUTTONOK: {
			gSettings.iAutoKickCheckMaxPingMsecs = GetDlgItemInt(hwnd, IDC_SP_EDIT, NULL, FALSE);
			MainWindowUpdateAutoKickState();
			EndDialog(hwnd, 0);
			return;
		}
		
		case IDCANCEL: {
			EndDialog(hwnd, 0);
			return;
		}
	}
}

LRESULT CALLBACK SetMaxPingDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
    	HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnSetMaxPingDlgInitDialog);
    	HANDLE_MSG(hWndDlg, WM_COMMAND,    OnSetMaxPingDlgCommand);
    }
    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Program Settings Dialog                                                                |
//{-------------------------------------------------------------------------------------------------

BOOL OnSettingsDlgInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam) {	
	SetDlgItemText(hwnd, IDC_SETTINGS_EDITTIMEOUTOWNSERVERS, std::to_string(gSettings.timeout.count()).c_str());
	SetDlgItemText(hwnd, IDC_SETTINGS_EDITAUTOKICKINTERVAL, std::to_string(gSettings.iAutoKickCheckDelay).c_str());
	SetDlgItemText(hwnd, IDC_SETTINGS_EDITAUTORELOAD, std::to_string(gSettings.iAutoReloadDelaySecs).c_str());
	SetDlgItemText(hwnd, IDC_SETTINGS_EDITLINECOUNT, std::to_string(gSettings.iMaxConsoleLineCount).c_str());

	if (gSettings.bLimitConsoleLineCount)
		CheckDlgButton(hwnd, IDC_SETTINGS_CHECKLINECOUNT, BST_CHECKED);
	else
		EnableWindow(GetDlgItem(hwnd, IDC_SETTINGS_EDITLINECOUNT), FALSE);
	
	if (gSettings.bColorPlayers)
		CheckDlgButton(hwnd, IDC_SETTINGS_CHECKCOLORPLAYERS, BST_CHECKED);
	
	if (gSettings.bColorPings)
		CheckDlgButton(hwnd, IDC_SETTINGS_CHECKCOLORPINGS, BST_CHECKED);
	
	if (gSettings.bDisableConsole)
		CheckDlgButton(hwnd, IDC_SETTINGS_CHECKDISABLECONSOLE, BST_CHECKED);
	
	return TRUE;
}

void OnSettingsDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id) {				
	case IDC_SETTINGS_CHECKLINECOUNT: {
		if (codeNotify == BN_CLICKED) {
			EnableWindow(GetDlgItem(hwnd, IDC_SETTINGS_EDITLINECOUNT),
				IsDlgButtonChecked(hwnd, IDC_SETTINGS_CHECKLINECOUNT) == BST_CHECKED
			);
		}					
		return;
	}
		
	case IDC_SETTINGS_BUTTONOK: {
		gSettings.timeout = 1ms * GetDlgItemInt(hwnd, IDC_SETTINGS_EDITTIMEOUTOWNSERVERS, NULL, FALSE);

		gSettings.iAutoKickCheckDelay = GetDlgItemInt(hwnd, IDC_SETTINGS_EDITAUTOKICKINTERVAL, NULL, FALSE);
		MainWindowUpdateAutoKickState();

		gSettings.iAutoReloadDelaySecs = GetDlgItemInt(hwnd, IDC_SETTINGS_EDITAUTORELOAD, NULL, FALSE);
		g_AutoReloadTimer.set_interval(gSettings.iAutoReloadDelaySecs);
			
		// TODO: Also use 0 = unlimited semantics?
		gSettings.iMaxConsoleLineCount = GetDlgItemInt(hwnd, IDC_SETTINGS_EDITLINECOUNT, NULL, FALSE);
		gSettings.bLimitConsoleLineCount = 0;
		if (IsDlgButtonChecked(hwnd, IDC_SETTINGS_CHECKLINECOUNT) == BST_CHECKED) {
			gSettings.bLimitConsoleLineCount = 1;
			Edit_ReduceLines(gWindows.hEditConsole, gSettings.iMaxConsoleLineCount);
			Edit_ScrollToEnd(gWindows.hEditConsole);
		}
		
		gSettings.bColorPlayers = IsDlgButtonChecked(hwnd, IDC_SETTINGS_CHECKCOLORPLAYERS) == BST_CHECKED;
		gSettings.bColorPings = IsDlgButtonChecked(hwnd, IDC_SETTINGS_CHECKCOLORPINGS) == BST_CHECKED;
		gSettings.bDisableConsole = IsDlgButtonChecked(hwnd, IDC_SETTINGS_CHECKDISABLECONSOLE) == BST_CHECKED;
			
		RECT rc;
		GetClientRect(gWindows.hWinMain, &rc);
		OnMainWindowSize(gWindows.hWinMain, SIZE_RESTORED, rc.right, rc.bottom); // Redraw
					
		gWindows.hDlgSettings = NULL;
		EndDialog(hwnd, 0);
		return;
	}
		
	case IDCANCEL: {
		gWindows.hDlgSettings = NULL;
		EndDialog(hwnd, 0);
		return;
	}
	}
}

LRESULT CALLBACK SettingsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg) {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnSettingsDlgInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnSettingsDlgCommand);
    }
    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Rotation Dialog                                                                 |
//{-------------------------------------------------------------------------------------------------

void LoadRotationToListbox(HWND hListBox) {
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}
	
	MainWindowLogExceptionsToConsole([&]() {
		const std::string response = pb2lib::send_rcon(server->address, server->rcon_password, "sv maplist", gSettings.timeout);
		const std::regex rx(R"(^\d+ (.*?)$)");

		ListBox_ResetContent(hListBox);
		for (auto it = std::sregex_iterator(response.begin(), response.end(), rx); it != std::sregex_iterator{}; ++it){
			const std::smatch match = *it;
			const std::string map = match[1];
			ListBox_AddString(hListBox, map.c_str());
		}
	}, "loading rotation");
}

BOOL OnRotationDlgInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam) {
	OnRotationDlgReloadContent(hwnd);
	ListBox_SendSelChange(GetDlgItem(hwnd, IDC_ROTATION_LIST));

	// Focus the map edit field
	PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_ROTATION_EDITMAP), TRUE);
	return TRUE;
}

void OnRotationDlgReloadContent(HWND hwnd) {
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	LoadRotationToListbox(GetDlgItem(hwnd, IDC_ROTATION_LIST));

	MainWindowLogExceptionsToConsole([&]() {
		std::string answer = pb2lib::get_cvar(server->address, server->rcon_password, "rot_file", gSettings.timeout);
		SetDlgItemText(hwnd, IDC_ROTATION_EDITFILE, answer.c_str());
	}, "getting rot_file");

	std::optional<std::string> pb2InstallPath = GetPb2InstallPath();
	if (!pb2InstallPath)
		return;

	std::string sMapshot = pb2InstallPath.value() + "\\pball\\pics\\mapshots\\-no-preview-.jpg";
	std::wstring sWideMapshot(sMapshot.begin(), sMapshot.end());

	g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());
}

void OnRotationDlgPaint(HWND hwnd) {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(GetDlgItem(hwnd, IDC_ROTATION_MAPSHOT), &ps);
	const RECT ui_rect = ps.rcPaint;

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
		auto win_color = GetSysColor(COLOR_BTNFACE);
		Gdiplus::Color gdi_color(GetRValue(win_color), GetGValue(win_color), GetBValue(win_color));
		Gdiplus::SolidBrush brush(gdi_color);
		graphics.DrawImage(g_pMapshotBitmap.get(), offset_x, offset_y, draw_width, draw_height);

		graphics.FillRectangle(&brush, 0, 0, draw_width, offset_y);  // top bar
		graphics.FillRectangle(&brush, 0, offset_y + draw_height, draw_width, offset_y + 1);  // bottom bar

		graphics.FillRectangle(&brush, 0, 0, offset_x, ui_height); // left bar
		graphics.FillRectangle(&brush, offset_y + draw_width, 0, offset_x + 100, ui_height); // right bar
	}
	else {
		FillRect(hdc, &ui_rect, (HBRUSH)(COLOR_WINDOW));
	}

	EndPaint(GetDlgItem(hwnd, IDC_ROTATION_MAPSHOT), &ps);
	DeleteDC(hdc);
}

void OnRotationDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	HWND hList = GetDlgItem(hwnd, IDC_ROTATION_LIST);
	HWND hEdit = GetDlgItem(hwnd, IDC_ROTATION_EDITMAP);

	auto executeSubcommandOnSelectedServer = [&](std::string subcommand) {
		auto* server = MainWindowGetSelectedServerOrLoggedNull();
		if (!server) {
			return std::string();
		}

		std::string command = "sv rotation " + subcommand;
		std::string answer;

		MainWindowLogExceptionsToConsole([&]() {
			answer = pb2lib::send_rcon(server->address, server->rcon_password, command, gSettings.timeout);
		}, "changing rotation");

		LoadRotationToListbox(hList);
		return answer;
	};

	auto update_button_enabled = [&]() {
		const auto selected_index = ListBox_GetCurSel(hList);
		EnableWindow(GetDlgItem(hwnd, IDC_ROTATION_BUTTONREMOVE), selected_index != LB_ERR);
	};

	switch (id) {
		case IDC_ROTATION_BUTTONADD: {
			std::vector<char> buffer(1ull + Edit_GetTextLength(hEdit));
			Edit_GetText(hEdit, buffer.data(), static_cast<int>(buffer.size()));
			executeSubcommandOnSelectedServer("add "s + buffer.data());

			auto index = ListBox_FindStringExact(hList, 0, buffer.data());
			ListBox_SetCurSel(hList, index);
			if (index >= 0) {
				Edit_SetText(hEdit, "");
			}
			update_button_enabled();
			SetFocus(hEdit);

			return;
		}
	
		case IDC_ROTATION_BUTTONREMOVE: {
			auto selected_index = ListBox_GetCurSel(hList);

			std::vector<char> buffer(1ull + Edit_GetTextLength(hEdit));
			Edit_GetText(hEdit, buffer.data(), static_cast<int>(buffer.size()));
			executeSubcommandOnSelectedServer("remove "s + buffer.data());

			ListBox_SetCurSel(hList, min(selected_index, ListBox_GetCount(hList) - 1));
			ListBox_SendSelChange(hList);
			return;
		}
	
		case IDC_ROTATION_BUTTONCLEAR: {
			executeSubcommandOnSelectedServer("clear");
			update_button_enabled();
			return;
		}
	
		case IDC_ROTATION_BUTTONWRITE: {
			std::vector<char> buffer(1ull + GetWindowTextLength(GetDlgItem(hwnd, IDC_ROTATION_EDITFILE)));
			GetDlgItemText(hwnd, IDC_ROTATION_EDITFILE, buffer.data(), static_cast<int>(buffer.size()));
			auto sAnswer = executeSubcommandOnSelectedServer("save "s + buffer.data());

			if (sAnswer.find("Saved maplist to") != std::string::npos)
				MessageBox(hwnd, "The maplist was saved successfully", "Success", MB_OK | MB_ICONINFORMATION);
			else {
				std::string sContent = "An error occured. The server answered: " + sAnswer;
				MessageBox(hwnd, sContent.c_str(), "Error", MB_OK | MB_ICONERROR);
			}
			return;
		}
	
		case IDC_ROTATION_BUTTONREAD: {
			std::vector<char> buffer(1ull + GetWindowTextLength(GetDlgItem(hwnd, IDC_ROTATION_EDITFILE)));
			GetDlgItemText(hwnd, IDC_ROTATION_EDITFILE, buffer.data(), static_cast<int>(buffer.size()));
			executeSubcommandOnSelectedServer("load "s + buffer.data());
			return;
		}
	
		case IDCANCEL: {
			gWindows.hDlgRotation = NULL;
			EndDialog(hwnd, 0);
			return;
		}
		
		case IDC_ROTATION_LIST: {
			if (codeNotify == LBN_SELCHANGE) {
				update_button_enabled();

				const auto selected_index = ListBox_GetCurSel(hList);
				if (selected_index == LB_ERR) {
					return;
				}

				std::vector<char> buffer(1ull + ListBox_GetTextLen(hList, selected_index));
				ListBox_GetText(GetDlgItem(hwnd, IDC_ROTATION_LIST), selected_index, buffer.data());
				Edit_SetText(hEdit, buffer.data());
			}
			return;
		}
	
		case IDC_ROTATION_EDITMAP: {
			if (codeNotify == EN_SETFOCUS) {
				SendMessage(hwnd, DM_SETDEFID, IDC_ROTATION_BUTTONADD, 0);
				return;
			}
			if (codeNotify == EN_KILLFOCUS) {
				SendMessage(hwnd, DM_SETDEFID, IDC_ROTATION_BUTTONOK, 0);
				Button_SetStyle(GetDlgItem(hwnd, IDC_ROTATION_BUTTONADD), BS_PUSHBUTTON, true);
				return;
			}

			if (codeNotify == EN_CHANGE) {
				std::vector<char> buffer(1ull + Edit_GetTextLength(hEdit));
				Edit_GetText(hEdit, buffer.data(), static_cast<int>(buffer.size()));

				auto index = ListBox_FindStringExact(hList, 0, buffer.data());
				ListBox_SetCurSel(hList, index);

				update_button_enabled();

				std::optional<std::string> pb2InstallPath = GetPb2InstallPath();
				if (!pb2InstallPath)
					return;

				std::string sMapshot = pb2InstallPath.value() + R"(\pball\pics\mapshots\)";
				int iBufferSize = GetWindowTextLength(hEdit) + 1;
				std::vector<char> mapnameBuffer(iBufferSize);
				GetDlgItemText(hwnd, IDC_ROTATION_EDITMAP, mapnameBuffer.data(), iBufferSize);
				sMapshot += mapnameBuffer.data();
				sMapshot += ".jpg";

				DWORD dwAttributes = GetFileAttributes(sMapshot.c_str());
				if (dwAttributes == INVALID_FILE_ATTRIBUTES || (dwAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					sMapshot = pb2InstallPath.value() + R"(\pball\pics\mapshots\-no-preview-.jpg)";
				}

				std::wstring sWideMapshot(sMapshot.begin(), sMapshot.end());
				g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());

				RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_INVALIDATE);
			}
			return;
		}
	}
}

int OnRotationDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret) {
	if (vk == VK_DELETE && hwndListbox == GetDlgItem(hwnd, IDC_ROTATION_LIST)) {
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ROTATION_BUTTONREMOVE, BN_CLICKED), (LPARAM)GetDlgItem(hwnd, IDC_ROTATION_BUTTONREMOVE));
		return -2;
	}
	return -1;
}

LRESULT CALLBACK RotationDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
    	HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnRotationDlgInitDialog);
    	HANDLE_MSG(hWndDlg, WM_COMMAND,    OnRotationDlgCommand);
    	HANDLE_MSG(hWndDlg, WM_PAINT,      OnRotationDlgPaint);
		HANDLE_MSG(hWndDlg, WM_VKEYTOITEM, OnRotationDlgVkeyToItem);
    }
    
    if (Msg == WM_SERVERCHANGED) {
		OnRotationDlgReloadContent(hWndDlg);
		return TRUE;
	}

    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback RCON Commands Dialog                                                                   |
//{-------------------------------------------------------------------------------------------------

BOOL OnRCONCommandsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	SetWindowText(GetDlgItem(hwnd, IDC_RCONCOMMANDS_INFOTEXT),
		"The `sv` prefix lets you use most of the in-game commands, for example:\r\n"
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
		"Additionally, these might be useful:\r\n"
		"VARNAME VALUE - sets the specified variable to the value\r\n"
		"status        - shows the current map and lists players\r\n"
		"kick NUMBER   - kicks a player by his number\r\n"
		"map NAME      - instantly restarts the server and loads a map\r\n"
		"say TEXT      - says the text as server\r\n"
		"quit, exit    - quit the server\r\n"
		"exec PATH     - executes a config file\r\n"
		"unset VARNAME - unsets a variable"
	);

	SendMessage(GetDlgItem(hwnd, IDC_RCONCOMMANDS_INFOTEXT), WM_SETFONT, (LPARAM)(HFONT)g_MonospaceFont, true);
	return TRUE;
}

void OnRCONCommandsDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	switch (id) {
		case IDCANCEL: {
			gWindows.hDlgRconCommands = NULL;
			EndDialog(hwnd, 0);
			return;
		}
	}
}

LRESULT CALLBACK RCONCommandsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnRCONCommandsInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND, OnRCONCommandsDlgCommand);
    }
    return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Servers Dialog                                                                  |
//{-------------------------------------------------------------------------------------------------

void ServersDlgAddOrUpdateServer(HWND list, const Server* stable_server_ptr) noexcept {
	std::string display_string = static_cast<std::string>(*stable_server_ptr);
	ListBox_AddOrUpdateString(list, display_string, stable_server_ptr);
}

void ServersDlgFetchHostname(HWND hDlg, Server* server) noexcept {
	HWND hWinMain = gWindows.hWinMain;
	UINT message = WM_HOSTNAMEREADY;
	server->hostname = g_HostnameResolver.resolve(server->address, [hWinMain, hDlg, message, server](const std::string& resolved_hostname) {
		PostMessage(hWinMain, message, 0, (LPARAM)server);
		PostMessage(hDlg, message, 0, (LPARAM)server);
	});
}

BOOL OnServersDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	for (const auto& ptr : g_ServersWithRcon) {
		ServersDlgAddOrUpdateServer(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), ptr.get());
	}

	std::promise<std::vector<std::unique_ptr<Server>>> promise;
	g_ServerlistFuture = promise.get_future();

	std::jthread thread([hwnd](std::promise<std::vector<std::unique_ptr<Server>>> promise) {
		std::string serverlist = GetHttpResponse(gSettings.sServerlistAddress);

		std::vector<std::unique_ptr<Server>> result;

		const std::regex rx(R"((\d+\.\d+\.\d+\.\d+):(\d{2,5}))");
		for (auto it = std::sregex_iterator(serverlist.begin(), serverlist.end(), rx); it != std::sregex_iterator{}; ++it) {
			const std::smatch match = *it;

			std::unique_ptr<Server>& server = result.emplace_back(std::make_unique<Server>());
			server->address.ip = match[1];
			server->address.port = std::stoi(match[2]);
		}

		promise.set_value(std::move(result));
		PostMessage(hwnd, WM_SERVERLISTREADY, 0, 0);
	}, std::move(promise));
	thread.detach();

	EnableWindow(GetDlgItem(hwnd, IDC_SERVERS_BUTTONREMOVE), FALSE);
	EnableWindow(GetDlgItem(hwnd, IDC_SERVERS_BUTTONSAVE), FALSE);

	return TRUE;
}

void OnServersDlgServerlistReady(HWND hWndDlg) noexcept {
	if (!g_ServerlistFuture.valid() || g_ServerlistFuture.wait_for(0s) == std::future_status::timeout) {
		return;
	}
	g_Serverlist = g_ServerlistFuture.get();

	for (const auto& server_ptr : g_Serverlist) {
		ServersDlgAddOrUpdateServer(GetDlgItem(hWndDlg, IDC_SERVERS_LISTLEFT), server_ptr.get());
		ServersDlgFetchHostname(hWndDlg, server_ptr.get());
	}
}

void OnServersDlgHostnameReady(HWND hWndDlg, Server* server_instance) {
	std::lock_guard guard(g_ThreadGlobalReadMutex);

	for (const auto& server_ptr : g_Serverlist) {
		if (server_ptr.get() == server_instance) {
			ServersDlgAddOrUpdateServer(GetDlgItem(hWndDlg, IDC_SERVERS_LISTLEFT), server_ptr.get());
		}
	}

	for (const auto& server_ptr : g_ServersWithRcon) {
		if (server_ptr.get() == server_instance) {
			ServersDlgAddOrUpdateServer(GetDlgItem(hWndDlg, IDC_SERVERS_LISTRIGHT), server_ptr.get());
		}
	}
}

void OnServersDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	auto server_from_inputs = [&]() -> std::optional<Server> {
		EDITBALLOONTIP balloon_tip = { 0 };
		balloon_tip.cbStruct = sizeof(EDITBALLOONTIP);
		balloon_tip.ttiIcon = TTI_ERROR;

		Server server;

		DWORD dwIP = 0;
		SendMessage(GetDlgItem(hwnd, IDC_SERVERS_EDITIP), IPM_GETADDRESS, 0, (LPARAM)&dwIP);
		server.address.ip = std::to_string(FIRST_IPADDRESS(dwIP)) + "." +
			std::to_string(SECOND_IPADDRESS(dwIP)) + "." +
			std::to_string(THIRD_IPADDRESS(dwIP)) + "." +
			std::to_string(FOURTH_IPADDRESS(dwIP));

		std::vector<char> buffer;
		buffer.resize(1ull + GetWindowTextLength(GetDlgItem(hwnd, IDC_SERVERS_EDITPORT)));
		Edit_GetText(GetDlgItem(hwnd, IDC_SERVERS_EDITPORT), buffer.data(), static_cast<int>(buffer.size()));
		server.address.port = atoi(buffer.data());

		if (dwIP == 0 || server.address.port == 0) {
			balloon_tip.pszText = L"Please enter the address of the server";
			balloon_tip.pszTitle = L"IP and Port required";
			Edit_ShowBalloonTip(GetDlgItem(hwnd, IDC_SERVERS_EDITPORT), &balloon_tip);
			return std::nullopt;
		}

		buffer.resize(1ull + GetWindowTextLength(GetDlgItem(hwnd, IDC_SERVERS_EDITPW)));
		Edit_GetText(GetDlgItem(hwnd, IDC_SERVERS_EDITPW), buffer.data(), static_cast<int>(buffer.size()));
		server.rcon_password = buffer.data();
		if (server.rcon_password.empty()) {
			balloon_tip.pszText = L"Please enter the rcon_password of the server";
			balloon_tip.pszTitle = L"Password required";
			Edit_ShowBalloonTip(GetDlgItem(hwnd, IDC_SERVERS_EDITPW), &balloon_tip);
			return std::nullopt;
		}
		return server;
	};

	switch(id) {
		case IDCANCEL: {
			gWindows.hDlgServers = NULL;
			EndDialog(hwnd, 0);
			return;
		}
		case IDC_SERVERS_BUTTONADD: {
			std::optional<Server> server = server_from_inputs();
			if (!server) {
				return;
			}

			g_ServersWithRcon.emplace_back(std::make_unique<Server>(server.value()));
			Server* raw_server_ptr = g_ServersWithRcon.back().get();

			ServersDlgFetchHostname(hwnd, raw_server_ptr);
			MainWindowAddOrUpdateOwnedServer(raw_server_ptr);
			ServersDlgAddOrUpdateServer(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), raw_server_ptr);
			return;
		}
		case IDC_SERVERS_BUTTONREMOVE: {
			auto selected_index = ListBox_GetCurSel(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT));
			if (selected_index == LB_ERR)
				return;

			const Server* stored_server = reinterpret_cast<Server*>(ListBox_GetItemData(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), selected_index));
			auto it = std::ranges::find_if(g_ServersWithRcon, [&](const auto& unique_ptr) { return unique_ptr.get() == stored_server; });
			assert(it != g_ServersWithRcon.end());

			{
				std::lock_guard guard(g_ThreadGlobalReadMutex);
				std::unique_ptr<Server> moved_out = std::move(*it);
				g_ServersWithRcon.erase(it);
			}

			MainWindowRemoveOwnedServer(stored_server);

			const auto found_index = ListBox_CustomFindItemData(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), stored_server);
			ListBox_CustomDeleteString(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), found_index);

			return;
		}
		case IDC_SERVERS_BUTTONSAVE: {
			auto selected_index = ListBox_GetCurSel(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT));
			if (selected_index == LB_ERR)
				return;
			Server* stored_server = reinterpret_cast<Server*>(ListBox_GetItemData(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), selected_index));

			std::optional<Server> input_data_server = server_from_inputs();
			if (!input_data_server) return;

			{
				std::lock_guard guard(g_ThreadGlobalReadMutex);
				*stored_server = input_data_server.value();
			}
			auto it = std::ranges::find_if(g_ServersWithRcon, [&](const auto& unique_ptr) { return unique_ptr.get() == stored_server; });
			assert(it != g_ServersWithRcon.end());

			ServersDlgFetchHostname(hwnd, stored_server);
			ServersDlgAddOrUpdateServer(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), stored_server);
			MainWindowAddOrUpdateOwnedServer(stored_server);
			return;
		}
	}
	
	switch (codeNotify) {
		case LBN_SELCHANGE: {
			auto selected_index = ListBox_GetCurSel(GetDlgItem(hwnd, id));
			if (selected_index == LB_ERR)
				return;
			Server* stored_server = reinterpret_cast<Server*>(ListBox_GetItemData(GetDlgItem(hwnd, id), selected_index));

			BYTE b0, b1, b2, b3;
			SplitIpAddressToBytes(stored_server->address.ip, &b0, &b1, &b2, &b3);

#pragma warning (suppress : 26451)
			SendMessage(GetDlgItem(hwnd, IDC_SERVERS_EDITIP), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			Edit_SetText(GetDlgItem(hwnd, IDC_SERVERS_EDITPORT), std::to_string(stored_server->address.port).c_str());
			Edit_SetText(GetDlgItem(hwnd, IDC_SERVERS_EDITPW), stored_server->rcon_password.c_str());

			if (id == IDC_SERVERS_LISTLEFT) {
				ListBox_SetCurSel(GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT), -1);
				EnableWindow(GetDlgItem(hwnd, IDC_SERVERS_BUTTONREMOVE), FALSE);
				EnableWindow(GetDlgItem(hwnd, IDC_SERVERS_BUTTONSAVE), FALSE);
			}
			else if (id == IDC_SERVERS_LISTRIGHT) {
				ListBox_SetCurSel(GetDlgItem(hwnd, IDC_SERVERS_LISTLEFT), -1);
				EnableWindow(GetDlgItem(hwnd, IDC_SERVERS_BUTTONREMOVE), TRUE);
				EnableWindow(GetDlgItem(hwnd, IDC_SERVERS_BUTTONSAVE), TRUE);
			}
			return;
		}
		case LBN_DBLCLK: {
			if (id == IDC_SERVERS_LISTLEFT) {
				SetFocus(GetDlgItem(hwnd, IDC_SERVERS_EDITPW));
			}
			return;
		}

		case EN_SETFOCUS: {
			if (id == IDC_SERVERS_EDITPW) {
				SendMessage(hwnd, DM_SETDEFID, IDC_SERVERS_BUTTONADD, 0);
			}
			return;
		}
		case EN_KILLFOCUS: {
			SendMessage(hwnd, DM_SETDEFID, 0, 0);
			Button_SetStyle(GetDlgItem(hwnd, IDC_SERVERS_BUTTONADD), BS_PUSHBUTTON, true);
			return;
		}
	}
}

int OnServersDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret) {
	if (vk == VK_DELETE && hwndListbox == GetDlgItem(hwnd, IDC_SERVERS_LISTRIGHT)) {
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SERVERS_BUTTONREMOVE, BN_CLICKED), (LPARAM)GetDlgItem(hwnd, IDC_SERVERS_BUTTONREMOVE));
		return -2;
	}

	if (vk == VK_RETURN && hwndListbox == GetDlgItem(hwnd, IDC_SERVERS_LISTLEFT)) {
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SERVERS_LISTLEFT, LBN_DBLCLK), (LPARAM)GetDlgItem(hwnd, IDC_SERVERS_LISTLEFT));
		return -2;
	}

	return -1;
}

LRESULT CALLBACK ServersDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam) {
	switch(Msg) {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnServersDlgInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnServersDlgCommand);
		HANDLE_MSG(hWndDlg, WM_VKEYTOITEM, OnServersDlgVkeyToItem);
	}

	if (Msg == WM_HOSTNAMEREADY) { OnServersDlgHostnameReady(hWndDlg, (Server*)lParam); return 0; }
	if (Msg == WM_SERVERLISTREADY) { OnServersDlgServerlistReady(hWndDlg); return 0; };

	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Forcejoin Dialog                                                                       |
//{-------------------------------------------------------------------------------------------------

BOOL OnForcejoinInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	const std::vector button_ids = { IDC_FJ_BUTTONRED, IDC_FJ_BUTTONBLUE, IDC_FJ_BUTTONPURPLE, IDC_FJ_BUTTONYELLOW, IDC_FJ_BUTTONOBSERVER, IDC_FJ_BUTTONAUTO };
	const std::vector colors = { Color::RED, Color::BLUE, Color::PURPLE, Color::YELLOW, Color::WHITE };

	// We want the bitmap to stay around for the lifetime of the dialog
	// We want to change it the next time the dialog is opened (possibly with other DPI settings)
	// We want all created bitmaps to be eventually deleted
	static std::vector<DeleteObjectRAIIWrapper<HBITMAP>> bitmaps(colors.size());

	RECT rect;
	GetWindowRect(GetDlgItem(hwnd, IDC_FJ_BUTTONRED), &rect);
	const int color_bitmap_side_length = static_cast<int>(0.4 * (0ll + rect.bottom - rect.top));

	for (size_t i = 0; i < colors.size(); ++i) {
		bitmaps[i] = GetFilledSquareBitmap(GetDC(hwnd), color_bitmap_side_length, colors[i]);
		SendMessage(GetDlgItem(hwnd, button_ids[i]), BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)(HBITMAP)(bitmaps[i]));
	}

	// show underlined keyboard shortcut without pressing alt
	for (auto button_id : button_ids) {
		SendMessage(GetDlgItem(hwnd, button_id), WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL), 0);
	}

	return TRUE;
}

void OnForcejoinCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id) {
		case IDC_FJ_BUTTONRED: EndDialog(hwnd, (INT_PTR)'r'); return;
		case IDC_FJ_BUTTONBLUE: EndDialog(hwnd, (INT_PTR)'b'); return;
		case IDC_FJ_BUTTONYELLOW: EndDialog(hwnd, (INT_PTR)'y'); return;
		case IDC_FJ_BUTTONPURPLE: EndDialog(hwnd, (INT_PTR)'p'); return;
		case IDC_FJ_BUTTONAUTO: EndDialog(hwnd, (INT_PTR)'a'); return;
		case IDC_FJ_BUTTONOBSERVER: EndDialog(hwnd, (INT_PTR)'o'); return;
		case IDCANCEL: EndDialog(hwnd, (INT_PTR)-1); return;  // Allows closing with escape
	}
}

LRESULT CALLBACK ForcejoinDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg) {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnForcejoinInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnForcejoinCommand);
	}
	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback AutoKick Entries Dialog                                                                |
//{-------------------------------------------------------------------------------------------------


void AutoKickEntriesDlgAddOrUpdateEntry(HWND list, const AutoKickEntry* stable_entry_ptr) {
	std::string display_string = static_cast<std::string>(*stable_entry_ptr);
	ListBox_AddOrUpdateString(list, display_string, stable_entry_ptr);
}

void AutoKickEntriesDlgRefillList(HWND list) {
	for (auto& entry : g_vAutoKickEntries) {
		AutoKickEntriesDlgAddOrUpdateEntry(list, entry.get());
	}
}

BOOL OnAutoKickEntriesDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	AutoKickEntriesDlgRefillList(GetDlgItem(hwnd, IDC_AUTOKICK_LIST));
	Button_SetCheck(GetDlgItem(hwnd, IDC_AUTOKICK_RADIONAME), true);
	ListBox_SendSelChange(GetDlgItem(hwnd, IDC_AUTOKICK_LIST));
	return TRUE;
}

void OnAutoKickEntriesDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	auto entry_from_inputs = [&]() {
		std::vector<char> buffer(static_cast<size_t>(GetWindowTextLength(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT))) + 1);
		GetWindowText(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT), buffer.data(), static_cast<int>(buffer.size()));
		std::string text = buffer.data();

		AutoKickEntry entry;
		if (IsDlgButtonChecked(hwnd, IDC_AUTOKICK_RADIOID)) {
			entry.value = atoi(text.c_str());
		}
		else {
			assert(IsDlgButtonChecked(hwnd, IDC_AUTOKICK_RADIONAME));
			entry.value = text;
		}
		return entry;
	};

	auto update_edit_field = [&]() {
		if (Button_GetCheck(GetDlgItem(hwnd, IDC_AUTOKICK_RADIOID))) {
			AddStyle(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT), ES_NUMBER);
			SetDlgItemInt(hwnd, IDC_AUTOKICK_EDIT, GetDlgItemInt(hwnd, IDC_AUTOKICK_EDIT, NULL, FALSE), FALSE);
		}
		else {
			RemoveStyle(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT), ES_NUMBER);
		}
	};

	switch(id) {
		case IDC_AUTOKICK_BUTTONADD: {
			std::lock_guard guard(g_ThreadGlobalReadMutex);
			g_vAutoKickEntries.push_back(std::make_unique<AutoKickEntry>(entry_from_inputs()));
			AutoKickEntriesDlgAddOrUpdateEntry(GetDlgItem(hwnd, IDC_AUTOKICK_LIST), g_vAutoKickEntries.back().get());
			SetFocus(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT));
			return;
		}

		case IDC_AUTOKICK_BUTTONREMOVE: {
			auto selected_index = ListBox_GetCurSel(GetDlgItem(hwnd, IDC_AUTOKICK_LIST));
			if (selected_index == LB_ERR)
				return;

			AutoKickEntry* selected_entry = (AutoKickEntry*)ListBox_GetItemData(GetDlgItem(hwnd, IDC_AUTOKICK_LIST), selected_index);

			auto it = std::ranges::find_if(g_vAutoKickEntries, [selected_entry](auto&& element) {return element.get() == selected_entry; });
			assert(it != g_vAutoKickEntries.end());

			{
				std::lock_guard guard(g_ThreadGlobalReadMutex);
				g_vAutoKickEntries.erase(it);
			}

			ListBox_CustomDeleteString(GetDlgItem(hwnd, IDC_AUTOKICK_LIST), selected_index);
			return;
		}
		
		case IDC_AUTOKICK_BUTTONOVERWRITE: {
			auto selected_index = ListBox_GetCurSel(GetDlgItem(hwnd, IDC_AUTOKICK_LIST));
			if (selected_index == LB_ERR)
				return;

			AutoKickEntry* selected_entry = (AutoKickEntry*)ListBox_GetItemData(GetDlgItem(hwnd, IDC_AUTOKICK_LIST), selected_index);
			*selected_entry = entry_from_inputs();
			AutoKickEntriesDlgAddOrUpdateEntry(GetDlgItem(hwnd, IDC_AUTOKICK_LIST), selected_entry);
			return;
		}
		
		case IDC_AUTOKICK_LIST: {
			if (codeNotify == LBN_SELCHANGE) {
				auto selected_index = ListBox_GetCurSel(GetDlgItem(hwnd, IDC_AUTOKICK_LIST));
				if (selected_index == LB_ERR) {
					EnableWindow(GetDlgItem(hwnd, IDC_AUTOKICK_BUTTONOVERWRITE), false);
					EnableWindow(GetDlgItem(hwnd, IDC_AUTOKICK_BUTTONREMOVE), false);
					return;
				}
				AutoKickEntry* selected_entry = (AutoKickEntry*)ListBox_GetItemData(GetDlgItem(hwnd, IDC_AUTOKICK_LIST), selected_index);

				Edit_SetText(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT), selected_entry->value_string().c_str());
				
				Button_SetCheck(GetDlgItem(hwnd, IDC_AUTOKICK_RADIOID), std::holds_alternative<AutoKickEntry::IdT>(selected_entry->value));
				Button_SetCheck(GetDlgItem(hwnd, IDC_AUTOKICK_RADIONAME), std::holds_alternative<AutoKickEntry::NameT>(selected_entry->value));

				EnableWindow(GetDlgItem(hwnd, IDC_AUTOKICK_BUTTONOVERWRITE), true);
				EnableWindow(GetDlgItem(hwnd, IDC_AUTOKICK_BUTTONREMOVE), true);

				update_edit_field();
			}
			else if (codeNotify == LBN_DBLCLK) {
				SetFocus(GetDlgItem(hwnd, IDC_AUTOKICK_EDIT));
			}
			return;
		}

		case IDC_AUTOKICK_RADIOID:
		case IDC_AUTOKICK_RADIONAME: {
			update_edit_field();
			return;
		}
		
		case IDCANCEL: {
			gWindows.hDlgAutoKickEntries = NULL;
			EndDialog(hwnd, 0);
			return;
		}
	}

	switch (codeNotify) {
		case EN_SETFOCUS: {
			if (id == IDC_AUTOKICK_EDIT) {
				SendMessage(hwnd, DM_SETDEFID, IDC_AUTOKICK_BUTTONADD, 0);
			}
			return;
		}
		case EN_KILLFOCUS: {
			SendMessage(hwnd, DM_SETDEFID, 0, 0);
			Button_SetStyle(GetDlgItem(hwnd, IDC_AUTOKICK_BUTTONADD), BS_PUSHBUTTON, true);
			return;
		}
	}
}

int OnAutoKickEntriesDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret) {
	if (vk == VK_DELETE && hwndListbox == GetDlgItem(hwnd, IDC_AUTOKICK_LIST)) {
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_AUTOKICK_BUTTONREMOVE, BN_CLICKED), (LPARAM)GetDlgItem(hwnd, IDC_AUTOKICK_BUTTONREMOVE));
		return -2;
	}
	return -1;
}

LRESULT CALLBACK AutoKickEntriesDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam) {
	switch(Msg) {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnAutoKickEntriesDlgInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnAutoKickEntriesDlgCommand);
		HANDLE_MSG(hWndDlg, WM_VKEYTOITEM, OnAutoKickEntriesDlgVkeyToItem);
	}

	if (Msg == WM_AUTOKICKENTRYADDED) {
		AutoKickEntriesDlgRefillList(GetDlgItem(hWndDlg, IDC_AUTOKICK_LIST));
		return TRUE;
	}

	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Callback Banned IPs Dialog                                                                      |
//{-------------------------------------------------------------------------------------------------

void LoadBannedIPsToListbox(HWND hListBox) {
	auto* server = MainWindowGetSelectedServerOrLoggedNull();
	if (!server) {
		return;
	}

	MainWindowLogExceptionsToConsole([&]() {
		const std::string response = pb2lib::send_rcon(server->address, server->rcon_password, "sv listip", gSettings.timeout);
		const std::regex rx(R"([\d ]{3}\.[\d ]{3}\.[\d ]{3}\.[\d ]{3})");

		ListBox_ResetContent(hListBox);
		for (auto it = std::sregex_iterator(response.begin(), response.end(), rx); it != std::sregex_iterator{}; ++it) {
			const std::smatch match = *it;
			const std::string ip = match[0];
			ListBox_AddString(hListBox, ip.c_str());
		}
	}, "loading banned IPs");
}

BOOL OnBannedIPsDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_IPS_LIST));
	ListBox_SendSelChange(GetDlgItem(hwnd, IDC_IPS_LIST));

	PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hwnd, IDC_IPS_IPCONTROL), TRUE); // Focus the edit field

	SendMessage(GetDlgItem(hwnd, IDC_IPS_LIST), WM_SETFONT, (LPARAM)(HFONT)g_MonospaceFont, true);
	return TRUE;
}

void OnBannedIPsDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	HWND hList = GetDlgItem(hwnd, IDC_IPS_LIST);
	HWND hEdit = GetDlgItem(hwnd, IDC_IPS_IPCONTROL);

	DWORD ip = 0;
	SendMessage(hEdit, IPM_GETADDRESS, 0, (LPARAM)&ip);

	const std::string edit_ip = std::format("{:3}.{:3}.{:3}.{:3}", FIRST_IPADDRESS(ip), SECOND_IPADDRESS(ip), THIRD_IPADDRESS(ip), FOURTH_IPADDRESS(ip));
	const std::string compact_edit_ip = std::format("{}.{}.{}.{}", FIRST_IPADDRESS(ip), SECOND_IPADDRESS(ip), THIRD_IPADDRESS(ip), FOURTH_IPADDRESS(ip));

	auto helper_run_rcon_command_with_current_ip = [&](std::string command) {
		auto* server = MainWindowGetSelectedServerOrLoggedNull();
		if (!server) {
			return;
		}

		command += compact_edit_ip;
		MainWindowLogExceptionsToConsole([&]() {
			pb2lib::send_rcon(server->address, server->rcon_password, command, gSettings.timeout);
		}, "modifying banned IPs");

		LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_IPS_LIST));
	};

	auto update_button_enabled = [&]() {
		const auto selected_index = ListBox_GetCurSel(hList);
		EnableWindow(GetDlgItem(hwnd, IDC_IPS_BUTTONREMOVE), selected_index != LB_ERR);
	};

	switch(id) {
		case IDC_IPS_BUTTONADD: {
			helper_run_rcon_command_with_current_ip("sv addip ");
			ListBox_SetCurSel(hList, ListBox_FindStringExact(hList, 0, edit_ip.c_str()));
			update_button_enabled();
			SetFocus(hEdit);
			return;
		}
	
		case IDC_IPS_BUTTONREMOVE: {
			auto selected_index = ListBox_GetCurSel(hList);
			helper_run_rcon_command_with_current_ip("sv removeip ");
			ListBox_SetCurSel(hList, min(selected_index, ListBox_GetCount(hList) - 1));
			ListBox_SendSelChange(hList);
			return;
		}
	
		case IDC_IPS_IPCONTROL:
			switch (codeNotify) {
				case EN_CHANGE: {
					auto index = ListBox_FindStringExact(hList, 0, edit_ip.c_str());
					ListBox_SetCurSel(hList, index);
					update_button_enabled();
					break;
				}
				case EN_SETFOCUS: {
					SendMessage(hwnd, DM_SETDEFID, IDC_IPS_BUTTONADD, 0);
					break;
				}
				case EN_KILLFOCUS: {
					SendMessage(hwnd, DM_SETDEFID, 0, 0);
					Button_SetStyle(GetDlgItem(hwnd, IDC_IPS_BUTTONADD), BS_PUSHBUTTON, true);
					break;
				}
			}
			return;

		case IDCANCEL:
			gWindows.hDlgBannedIps = NULL;
			EndDialog(hwnd, 0);
			return;
		
		case IDC_IPS_LIST: {
			switch (codeNotify) {
				case LBN_SELCHANGE: {
					auto selected_index = ListBox_GetCurSel(hList);
					std::vector<char> buffer(1ull + ListBox_GetTextLen(hList, selected_index));
					ListBox_GetText(hList, selected_index, buffer.data());
					if (selected_index < 0 || buffer.empty()) {
						return;
					}

					BYTE b0, b1, b2, b3;
					SplitIpAddressToBytes({ buffer.data(), buffer.size() - 1 }, &b0, &b1, &b2, &b3);
	#pragma warning (suppress : 26451)
					SendMessage(hEdit, IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
					break;
				}
				case LBN_DBLCLK: {
					SetFocus(hEdit);
					break;
				}
			}
			return;
		}
	}
}

void OnBannedIPsDlgReloadContent(HWND hwnd) {
	LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_IPS_LIST));
}

int OnBannedIPsDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret) {
	if (vk == VK_DELETE && hwndListbox == GetDlgItem(hwnd, IDC_IPS_LIST)) {
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_IPS_BUTTONREMOVE, BN_CLICKED), (LPARAM)GetDlgItem(hwnd, IDC_IPS_BUTTONREMOVE));
		return -2;
	}
	return -1;
}

LRESULT CALLBACK BannedIPsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg) {
		HANDLE_MSG(hWndDlg, WM_INITDIALOG, OnBannedIPsDlgInitDialog);
		HANDLE_MSG(hWndDlg, WM_COMMAND,    OnBannedIPsDlgCommand);
		HANDLE_MSG(hWndDlg, WM_VKEYTOITEM, OnBannedIPsDlgVkeyToItem);
	}

	if (Msg == WM_SERVERCHANGED) {
		OnBannedIPsDlgReloadContent(hWndDlg);
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

DeleteObjectRAIIWrapper<HBITMAP> GetFilledSquareBitmap(HDC device_context, int side_length, DWORD color) {
	HBITMAP result_bitmap = CreateCompatibleBitmap(device_context, side_length, side_length);

	HDC bitmap_context = CreateCompatibleDC(device_context);
	auto default_bitmap = static_cast<HBITMAP>(SelectObject(bitmap_context, result_bitmap));

	RECT rect = { .left = 0, .top = 0, .right = side_length, .bottom = side_length };
	DeleteObjectRAIIWrapper<HBRUSH> brush = CreateSolidBrush(color);
	FillRect(bitmap_context, &rect, brush);

	SelectObject(bitmap_context, default_bitmap);
	DeleteDC(bitmap_context);

	return result_bitmap;
}

std::string GetHttpResponse(const std::string& url) {
	std::string user_agent = std::format("Digital Paint: Paintball2 Rconpanel v{}.{}.{}", Version::MAJOR, Version::MINOR, Version::BUILD);
	HINTERNET hInternet = InternetOpen(user_agent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
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
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, content.size() + 1);
	if (hMem == NULL) {
		return;
	}

	LPVOID pLocked = GlobalLock(hMem);
	if (pLocked == NULL) {
		GlobalFree(hMem);
		return;
	}
	memcpy(pLocked, content.c_str(), content.size() + 1);
	GlobalUnlock(hMem);

	OpenClipboard(gWindows.hWinMain);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);  // transfers ownership of hMem
	CloseClipboard();
}

BOOL CALLBACK EnumWindowsSetFontCallback(HWND child, LPARAM font) {
	SendMessage(child, WM_SETFONT, font, true);
	return true;
};

void AddStyle(HWND hwnd, LONG style) {
	auto old_style = GetWindowLong(hwnd, GWL_STYLE);
	SetWindowLong(hwnd, GWL_STYLE, old_style | style);
}

void RemoveStyle(HWND hwnd, LONG style) {
	auto old_style = GetWindowLong(hwnd, GWL_STYLE);
	SetWindowLong(hwnd, GWL_STYLE, old_style & ~style);
}

bool HasStyle(HWND hwnd, LONG style) {
	auto set_style = GetWindowLong(hwnd, GWL_STYLE);
	return set_style & style;
}

bool HasClass(HWND hwnd, std::string_view classname) {
	std::array<char, 512> buffer;
	GetClassName(hwnd, buffer.data(), static_cast<int>(buffer.size()));
	return classname == buffer.data();
}

void Edit_ReduceLines(HWND hEdit, int iLines) {
	if (iLines <= 0)
		return;
	
	while (Edit_GetLineCount(hEdit) > iLines) {
		Edit_SetSel(hEdit, 0, 1ull + Edit_LineLength(hEdit, 0));
		Edit_ReplaceSel(hEdit, "");
	}
}

void Edit_ScrollToEnd(HWND hEdit) {
	auto text_length = Edit_GetTextLength(hEdit);
	Edit_SetSel(hEdit, text_length, text_length);
	Edit_ScrollCaret(hEdit);
}

int ComboBox_CustomFindItemData(HWND hComboBox, const void* itemData) noexcept {
	int found_index = -1;
	for (int i = 0; i < ComboBox_GetCount(hComboBox); ++i) {
		if (reinterpret_cast<void*>(ComboBox_GetItemData(hComboBox, i)) == itemData) {
			found_index = i;
			break;
		}
	}
	return found_index;
}

int ListBox_CustomFindItemData(HWND hList, const void* itemData) noexcept {
	int found_index = -1;
	for (int i = 0; i < ListBox_GetCount(hList); ++i) {
		if (reinterpret_cast<void*>(ListBox_GetItemData(hList, i)) == itemData) {
			found_index = i;
			break;
		}
	}
	return found_index;
}

void ListBox_AddOrUpdateString(HWND list, const std::string& item_text, const void* item_data) {
	const auto selected_index = ListBox_GetCurSel(list);
	const auto found_index = ListBox_CustomFindItemData(list, item_data);

	if (found_index >= 0) {
		std::vector<char> buffer(1ull + ListBox_GetTextLen(list, found_index));
		ListBox_GetText(list, found_index, buffer.data());
		if (std::string_view(buffer.data(), buffer.size() - 1) == item_text) {
			return;
		}
		ListBox_DeleteString(list, found_index);
	}

	const auto created_index = ListBox_AddString(list, item_text.c_str());
	ListBox_SetItemData(list, created_index, item_data);

	if (selected_index != LB_ERR && selected_index == found_index) {
		ListBox_SetCurSel(list, created_index);
	}
}

void ListBox_CustomDeleteString(HWND list, int index) noexcept {
	const auto selected_index = ListBox_GetCurSel(list);
	ListBox_DeleteString(list, index);

	if (index == selected_index) {
		const auto new_index = min(ListBox_GetCount(list) - 1, selected_index);
		ListBox_SetCurSel(list, new_index);
		ListBox_SendSelChange(list);
	}
}

void ListBox_SendSelChange(HWND list) noexcept {
	SendMessage(GetParent(list), WM_COMMAND, MAKEWPARAM(GetWindowLong(list, GWL_ID), LBN_SELCHANGE), (LPARAM)list);
}

void SplitIpAddressToBytes(std::string_view ip, BYTE* pb0, BYTE* pb1, BYTE* pb2, BYTE* pb3) {
	*pb0 = *pb1 = *pb2 = *pb3 = NULL;

	auto substrings = ip
		| std::ranges::views::split('.')
		| std::ranges::views::transform([](auto&& rng) { return std::string(rng.begin(), rng.end()); });

	if (std::ranges::distance(substrings) != 4) {
		return;
	}

	auto it = substrings.begin();
	*pb0 = atoi((*it++).c_str());
	*pb1 = atoi((*it++).c_str());
	*pb2 = atoi((*it++).c_str());
	*pb3 = atoi((*it++).c_str());
}

std::optional<std::string> GetPb2InstallPath() {
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

void StartServerbrowser(void) {
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

void AutoKickTimerFunction() {

	std::vector<Server> servers;
	std::vector<AutoKickEntry> autokick_entries;

	{
		std::lock_guard guard(g_ThreadGlobalReadMutex);
		servers = FlatCopyVectorOfUniquePtrs(g_ServersWithRcon);
		autokick_entries = FlatCopyVectorOfUniquePtrs(g_vAutoKickEntries);
	}

    for (const auto& server : servers)
	{
		std::vector <pb2lib::Player> players = pb2lib::get_players(server.address, server.rcon_password, gSettings.timeout);
			
        for (const auto& player : players)
		{
			if (gSettings.iAutoKickCheckMaxPingMsecs != 0 && player.ping > gSettings.iAutoKickCheckMaxPingMsecs)
			{
				MainWindowLogExceptionsToConsole([&]() {
					std::string command = "kick " + std::to_string(player.number);
					auto response = pb2lib::send_rcon(server.address, server.rcon_password, command, gSettings.timeout);
					MainWindowWriteConsole("Player " + player.name + " on server " + static_cast<std::string>(server) + " had a too high ping and was kicked.");
				}, "auto-kicking");
				continue;
			}

			auto autokick_it = std::ranges::find_if(autokick_entries, [&](const auto& entry) {
				return entry.matches(player.id.value_or(0)) || entry.matches(player.name);
			});

			if (autokick_it == autokick_entries.end()) {
				continue;
			}

			MainWindowLogExceptionsToConsole([&]() {
				std::string command = "kick " + std::to_string(player.number);
				auto response = pb2lib::send_rcon(server.address, server.rcon_password, command, gSettings.timeout);
				MainWindowWriteConsole("Found and kicked player " + player.name + " on server " + static_cast<std::string>(server));
			}, "auto-kicking");
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
void LoadConfig() {
	auto path = ConfigLocation();

	if (!std::filesystem::exists(path)) {
		throw RconpanelException("No configuration file found. The program will save its settings when you close it.");
	}

	std::array<char, 4096> buffer = { 0 };
	Settings defaults;

	gSettings.timeout = 1ms * GetPrivateProfileInt("general", "timeout", static_cast<int>(defaults.timeout.count()), path.c_str());
	if (gSettings.timeout == 0ms) {
		gSettings.timeout = 500ms;  // config format was changed for 1.4.0
	}

	gSettings.iMaxConsoleLineCount = GetPrivateProfileInt("general", "maxConsoleLineCount", defaults.iMaxConsoleLineCount, path.c_str());
	gSettings.bLimitConsoleLineCount = GetPrivateProfileInt("general", "limitConsoleLineCount", defaults.iMaxConsoleLineCount, path.c_str());
	gSettings.bColorPlayers = GetPrivateProfileInt("general", "colorPlayers", defaults.bColorPlayers, path.c_str());
	gSettings.bColorPings = GetPrivateProfileInt("general", "colorPings", defaults.bColorPings, path.c_str());
	gSettings.bDisableConsole = GetPrivateProfileInt("general", "disableConsole", defaults.bDisableConsole, path.c_str());
	gSettings.iAutoReloadDelaySecs = GetPrivateProfileInt("general", "autoReloadDelay", defaults.iAutoReloadDelaySecs, path.c_str());
	g_AutoReloadTimer.set_interval(gSettings.iAutoReloadDelaySecs);
	
	GetPrivateProfileString("general", "serverlistAddress", defaults.sServerlistAddress.c_str(), buffer.data(), static_cast<int>(buffer.size()), path.c_str());
	gSettings.sServerlistAddress = buffer.data();

	int server_count = GetPrivateProfileInt("server", "count", 0, path.c_str());
	g_ServersWithRcon.reserve(server_count);
	for (int server_index = 0; server_index < server_count; server_index++) {
		Server server;
		std::string key = std::to_string(server_index);

		GetPrivateProfileString("ip", key.c_str(), "0.0.0.0", buffer.data(), static_cast<int>(buffer.size()), path.c_str());
		server.address.ip = buffer.data();

		server.address.port = GetPrivateProfileInt("port", key.c_str(), 0, path.c_str());

		GetPrivateProfileString("pw", key.c_str(), "", buffer.data(), static_cast<int>(buffer.size()), path.c_str());
		server.rcon_password = buffer.data();

		g_ServersWithRcon.emplace_back(std::make_unique<Server>(server));
		MainWindowAddOrUpdateOwnedServer(g_ServersWithRcon.back().get());
	}

	gSettings.bAutoKickCheckEnable = GetPrivateProfileInt("bans", "runBanThread", defaults.bAutoKickCheckEnable, path.c_str());
	gSettings.iAutoKickCheckDelay = GetPrivateProfileInt("bans", "delay", defaults.iAutoKickCheckDelay, path.c_str());
	MainWindowUpdateAutoKickState();

	int ban_count = GetPrivateProfileInt("bans", "count", 0, path.c_str());
	g_vAutoKickEntries.reserve(ban_count);
	for (int ban_index = 0; ban_index < ban_count; ban_index++) {
		std::string key_buffer = std::to_string(ban_index);

		GetPrivateProfileString("bans", key_buffer.c_str(), "", buffer.data(), static_cast<int>(buffer.size()), path.c_str());
		const std::string value = buffer.data();
		if (value.empty()) {
			throw RconpanelException("Invalid empty ban value");
		}

		key_buffer = std::to_string(ban_index) + "type";
		GetPrivateProfileString("bans", key_buffer.c_str(), "", buffer.data(), static_cast<int>(buffer.size()), path.c_str());
		const std::string type = buffer.data();
		if (type.empty()) {
			throw RconpanelException("Invalid or missing ban type");
		}

		AutoKickEntry entry = AutoKickEntry::from_type_and_value(type, value);
		g_vAutoKickEntries.push_back(std::make_unique<AutoKickEntry>(entry));
	}
}

void SaveConfig() {
	// TODO: Store and reload max ping for auto-kick?
	// TODO: Update ini paths with renamed program terminology (e.g. ban vs autokick)

	MainWindowWriteConsole("Saving configuration file...");
	auto path = ConfigLocation();

	// remove existing entries
	WritePrivateProfileString("general", NULL, NULL, path.c_str());
	WritePrivateProfileString("ip", NULL, NULL, path.c_str());
	WritePrivateProfileString("pw", NULL, NULL, path.c_str());
	WritePrivateProfileString("port", NULL, NULL, path.c_str());
	WritePrivateProfileString("bans", NULL, NULL, path.c_str());

	WritePrivateProfileString("general", "timeout", std::to_string(gSettings.timeout.count()).c_str(), path.c_str());
	WritePrivateProfileString("general", "maxConsoleLineCount", std::to_string(gSettings.iMaxConsoleLineCount).c_str(), path.c_str());
	WritePrivateProfileString("general", "limitConsoleLineCount", std::to_string(gSettings.bLimitConsoleLineCount).c_str(), path.c_str());
	WritePrivateProfileString("general", "autoReloadDelay", std::to_string(gSettings.iAutoReloadDelaySecs).c_str(), path.c_str());
	WritePrivateProfileString("general", "colorPlayers", std::to_string(gSettings.bColorPlayers).c_str(), path.c_str());
	WritePrivateProfileString("general", "colorPings", std::to_string(gSettings.bColorPings).c_str(), path.c_str());
	WritePrivateProfileString("general", "disableConsole", std::to_string(gSettings.bDisableConsole).c_str(), path.c_str());
	WritePrivateProfileString("general", "serverlistAddress", gSettings.sServerlistAddress.c_str(), path.c_str());

	WritePrivateProfileString("server", "count", std::to_string(g_ServersWithRcon.size()).c_str(), path.c_str());
	for (size_t server_index = 0; server_index < g_ServersWithRcon.size(); server_index++) {
		const Server& server = *g_ServersWithRcon[server_index];
		WritePrivateProfileString("ip", std::to_string(server_index).c_str(), server.address.ip.c_str(), path.c_str());
		WritePrivateProfileString("pw", std::to_string(server_index).c_str(), server.rcon_password.c_str(), path.c_str());
		WritePrivateProfileString("port", std::to_string(server_index).c_str(), std::to_string(server.address.port).c_str(), path.c_str());
	}

	WritePrivateProfileString("bans", "runBanThread", std::to_string(gSettings.bAutoKickCheckEnable).c_str(), path.c_str());
	WritePrivateProfileString("bans", "delay", std::to_string(gSettings.iAutoKickCheckDelay).c_str(), path.c_str());

	WritePrivateProfileString("bans", "count", std::to_string(g_vAutoKickEntries.size()).c_str(), path.c_str());
	for (unsigned int index = 0; index < g_vAutoKickEntries.size(); index++) {
		const AutoKickEntry& entry = *g_vAutoKickEntries[index];
		WritePrivateProfileString("bans", std::to_string(index).c_str(), entry.value_string().c_str(), path.c_str());
		WritePrivateProfileString("bans", (std::to_string(index) + "type").c_str(), entry.type_string().c_str(), path.c_str());
	}
}