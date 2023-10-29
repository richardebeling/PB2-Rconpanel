#ifndef __MAIN_H_INCLUDED
#define __MAIN_H_INCLUDED

#include "pb2lib.h" // Seems like we need to include winsock2.h before defining WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN
#include <cassert>
#include <ctime>
#include <map>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <variant>

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <Gdiplus.h>


template<class... Functors>
struct Overload : Functors... {
	using Functors::operator()...;
};

class RconpanelException : private std::runtime_error {
public:
	using std::runtime_error::runtime_error;
	using std::runtime_error::what;
};

template <typename HandleT, HandleT InvalidHandle = static_cast<HandleT>(NULL)>
class DeleteObjectRAIIWrapper {
public:
	DeleteObjectRAIIWrapper() noexcept = default;
	DeleteObjectRAIIWrapper(HandleT handle) noexcept : handle_{ handle } {}
	DeleteObjectRAIIWrapper(const DeleteObjectRAIIWrapper&) = delete;
	DeleteObjectRAIIWrapper(DeleteObjectRAIIWrapper&& other) noexcept { *this = std::move(other); };
	DeleteObjectRAIIWrapper& operator= (const DeleteObjectRAIIWrapper&) = delete;
	DeleteObjectRAIIWrapper& operator= (DeleteObjectRAIIWrapper&& other) noexcept { std::swap(this->handle_, other.handle_); return *this; };
	~DeleteObjectRAIIWrapper() { DeleteObject(handle_); }
	operator HandleT() noexcept { return handle_; };
private:
	HandleT handle_ = InvalidHandle;
};

struct WindowHandles {
	HWND hWinMain = NULL;

	HWND hDlgRotation = NULL;
	HWND hDlgBannedIps = NULL;
	HWND hDlgAutoKickEntries = NULL;
	HWND hDlgServers = NULL;
	HWND hDlgSettings = NULL;
	HWND hDlgRconCommands = NULL;

	HWND hComboServer = NULL;
	HWND hListPlayers = NULL;
	HWND hButtonKick = NULL;
	HWND hButtonAutoKick = NULL;
	HWND hButtonBanIP = NULL;
	HWND hButtonReload = NULL;
	HWND hButtonDPLoginProfile = NULL;
	HWND hButtonWhois = NULL;
	HWND hButtonForcejoin = NULL;
	HWND hEditConsole = NULL;
	HWND hComboRcon = NULL;
	HWND hButtonSend = NULL;
	HWND hButtonJoin = NULL;
	HWND hStaticServerInfo = NULL;
	HWND hStaticServer = NULL;
};

struct WindowDimensions {
	int	button_height = 0;
	int button_width = 0;

	int combobox_offset = 0;

	int padding_large = 0;
	int padding_small = 0;
	int padding_tiny = 0;

	int static_height = 0;
	int static_vertical_offset_from_button = 0;

	int static_server_width = 0;
};

struct AutoKickEntry {
	using NameT = std::string;
	using IdT = int;

	std::variant<NameT, IdT> value;

	static AutoKickEntry from_type_and_value(std::string_view type, std::string_view value);

	bool matches(NameT name) const;
	bool matches(IdT id) const;

	std::string type_string() const;
	std::string value_string() const;
	explicit operator std::string() const;
};

struct Server {
	pb2lib::Address address;
	std::shared_future<std::string> hostname;
	std::string rcon_password;

	explicit operator std::string() const;
};

struct ServerCvars {
	std::string mapname;
	std::string password;
	int elim = 0;
	int timelimit = 0;
	int maxclients = 0;

	static ServerCvars from_server(const Server& server, std::chrono::milliseconds timeout);
};

// TODO: Mark stuff as noexcept
// TODO: Clang format, clang tidy

[[noreturn]] void HandleCriticalError(const std::string& message) noexcept;

std::string ConfigLocation(void);
void LoadConfig(void);
void SaveConfig(void);

UINT RegisterWindowMessageOrCriticalError(const std::string& message_name) noexcept;
void SetClipboardContent(const std::string& content);
std::string GetHttpResponse(const std::string& url);
void SplitIpAddressToBytes(std::string_view ip, BYTE* pb0, BYTE* pb1, BYTE* pb2, BYTE* pb3);

DeleteObjectRAIIWrapper<HBITMAP> GetFilledSquareBitmap(HDC hDC, int side_length, DWORD color);
BOOL CALLBACK EnumWindowsSetFontCallback(HWND child, LPARAM font);
void AddStyle(HWND hwnd, LONG style);
void RemoveStyle(HWND hwnd, LONG style);
int GetStaticTextWidth(HWND hwndStatic);
bool HasStyle(HWND hwnd, LONG style);
bool HasClass(HWND hwnd, std::string_view classname);
void Edit_ReduceLines(HWND hEdit, int iLines);
void Edit_ScrollToEnd(HWND hEdit);
// original [List|Combo]Box_FindItemData doesn't find item data but item string.
int ComboBox_CustomFindItemData(HWND hComboBox, const void* itemData) noexcept;
int ListBox_CustomFindItemData(HWND hList, const void* itemData) noexcept;
void ListBox_CustomDeleteString(HWND list, int index) noexcept;
void ListBox_AddOrUpdateString(HWND list, const std::string& item_text, const void* item_data);
void ListBox_SendSelChange(HWND list) noexcept;

std::optional<std::string> GetPb2InstallPath(void);
void StartServerbrowser(void);

template<typename T>
std::vector<T> FlatCopyVectorOfUniquePtrs(const std::vector<std::unique_ptr<T>>& input) {
	std::vector<T> result;
	result.reserve(input.size());
	for (const auto& ptr : input) {
		result.push_back(*ptr);
	}
	return result;
}

constexpr UINT WM_ENTER = WM_USER + 3;
constexpr UINT WM_REFETCHPLAYERS = WM_USER + 4;
constexpr UINT WM_SERVERCHANGED = WM_USER + 5;
constexpr UINT WM_PLAYERSREADY = WM_USER + 6;
constexpr UINT WM_SERVERCVARSREADY = WM_USER + 7;
constexpr UINT WM_RCONRESPONSEREADY = WM_USER + 8;
constexpr UINT WM_HOSTNAMEREADY = WM_USER + 9;
constexpr UINT WM_SERVERLISTREADY = WM_USER + 10;
constexpr UINT WM_AUTOKICKENTRYADDED = WM_USER + 11;

//--------------------------------------------------------------------------------------------------
// Main Window
//--------------------------------------------------------------------------------------------------

void AutoKickTimerFunction();
void MainWindowLogExceptionsToConsole(std::function<void()> func, std::string_view action_description);
void MainWindowAddOrUpdateOwnedServer(const Server* stable_server_ptr) noexcept;
void MainWindowRemoveOwnedServer(const Server* stored_server_ptr) noexcept;
void MainWindowUpdateAutoKickState() noexcept;
void MainWindowUpdatePlayersListview() noexcept;
void MainWindowRefetchServerInfo() noexcept;
void MainWindowSendRcon(const std::string& command) noexcept;

Server* MainWindowGetSelectedServerOrLoggedNull() noexcept;
void MainWindowWriteConsole(std::string_view);
void LoadBannedIPsToListbox(HWND hListBox);
void LoadRotationToListbox(HWND hListBox);
void ShowAboutDialog(HWND hwnd);
void ShowPlayerInfo(HWND hwnd);

LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);
void OnMainWindowAutoKick(void);
void OnMainWindowBanIP(void);
void OnMainWindowCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnMainWindowCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct);
void OnMainWindowDestroy(HWND hwnd);
void OnMainWindowForcejoin(void);
HBRUSH OnMainWindowCtlColorStatic(HWND hwnd, HDC hdc, HWND hwndChild, int type_string);
void OnMainWindowGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo);
void OnMainWindowJoinServer(void);
void OnMainWindowKickPlayer(void);
int  CALLBACK OnMainWindowListViewSort(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
LRESULT OnMainWindowNotify(HWND hwnd, int id, NMHDR* nmh);
void OnMainWindowOpenDPLogin(void);
void OnMainWindowOpenWhois(void);
void OnMainWindowSendRcon(void);
void OnMainWindowSize(HWND hwnd, UINT state, int cx, int cy);
void OnMainWindowPlayersReady() noexcept;
void OnMainWindowServerCvarsReady() noexcept;
void OnMainWindowRconResponseReady() noexcept;
void OnMainWindowHostnameReady(Server* server_instance) noexcept;

//--------------------------------------------------------------------------------------------------
// Forcejoin Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ForcejoinDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnForcejoinCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnForcejoinInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

//--------------------------------------------------------------------------------------------------
// Auto Kick Dialog
//--------------------------------------------------------------------------------------------------
void AutoKickEntriesDlgAddOrUpdateEntry(HWND list, const AutoKickEntry* stable_entry_ptr);
void AutoKickEntriesDlgRefillList(HWND list);

LRESULT CALLBACK AutoKickEntriesDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnAutoKickEntriesDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
int OnAutoKickEntriesDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret);
BOOL OnAutoKickEntriesDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

//--------------------------------------------------------------------------------------------------
// Banned IPs Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK BannedIPsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnBannedIPsDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnBannedIPsDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
int OnBannedIPsDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret);
void OnBannedIPsDlgReloadContent(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Rotation Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK RotationDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
int OnRotationDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret);
void OnRotationDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnRotationDlgInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam);
void OnRotationDlgPaint(HWND hwnd);
void OnRotationDlgReloadContent(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Servers Dialog
//--------------------------------------------------------------------------------------------------
void ServersDlgAddOrUpdateServer(HWND list, const Server* stable_server_ptr) noexcept;
void ServersDlgFetchHostname(HWND hDlg, Server* server) noexcept;

LRESULT CALLBACK ServersDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL OnServersDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
void OnServersDlgServerlistReady(HWND hWndDlg) noexcept;
void OnServersDlgHostnameReady(HWND hWndDlg, Server* server_instance);
int OnServersDlgVkeyToItem(HWND hwnd, UINT vk, HWND hwndListbox, int iCaret);
void OnServersDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);

//--------------------------------------------------------------------------------------------------
// Program Settings Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK SettingsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnSettingsDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnSettingsDlgInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam);

//--------------------------------------------------------------------------------------------------
// RCON Commands Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK RCONCommandsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnRCONCommandsDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);

//--------------------------------------------------------------------------------------------------
// Max Ping Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK SetMaxPingDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnSetMaxPingDlgCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnSetMaxPingDlgInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

#endif // __MAIN_H_INCLUDED