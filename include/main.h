#ifndef __MAIN_H_INCLUDED
#define __MAIN_H_INCLUDED

#include "pb2lib.h" // Seems like we need to include winsock2.h before defining WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN
#if _WIN32_IE < 0x401
#define _WIN32_IE 0x401 //needed for commctrl.h so it includes InitCommonControls (0x300) and the ability to color subitems differently (0x400)
#endif

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

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <Gdiplus.h>


template <typename HandleT>
class DeleteObjectRAIIWrapper {
public:
	DeleteObjectRAIIWrapper(HandleT handle) : handle_{ handle } {}
	DeleteObjectRAIIWrapper(const DeleteObjectRAIIWrapper&) = delete;
	DeleteObjectRAIIWrapper(DeleteObjectRAIIWrapper&&) = delete;
	DeleteObjectRAIIWrapper& operator= (const DeleteObjectRAIIWrapper&) = delete;
	DeleteObjectRAIIWrapper& operator= (DeleteObjectRAIIWrapper&&) = delete;
	~DeleteObjectRAIIWrapper() { DeleteObject(handle_); }
	operator HandleT() noexcept { return handle_; };
private:
	HandleT handle_;
};

struct WindowHandles {
	HWND hDlgManageRotation = NULL;
	HWND hDlgManageIds = NULL;
	HWND hDlgManageIps = NULL;
	HWND hDlgSettings = NULL;
	HWND hDlgRconCommands = NULL;
	HWND hWinMain = NULL;
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
};

struct AutoKickEntry {
	enum class Type {
		ID = 0,
		NAME = 1,
	};

    Type tType = Type::ID;
    std::string sText;
};

struct Server {
	pb2lib::Address address;
	std::shared_future<std::string> hostname;
	std::string rcon_password;

	explicit operator std::string() const;
};

// TODO: Mark stuff as noexcept
// TODO: Clang format, clang tidy

[[noreturn]] void HandleCriticalError(const std::string& message) noexcept;

std::string ConfigLocation(void);
int  LoadConfig(void);
void SaveConfig(void);
void DeleteConfig(void);

UINT RegisterWindowMessageOrCriticalError(const std::string& message_name) noexcept;
void SetClipboardContent(const std::string& content);
std::string GetHttpResponse(const std::string& url);
void SplitIpAddressToBytes(std::string_view ip, BYTE* pb0, BYTE* pb1, BYTE* pb2, BYTE* pb3);

void Edit_ReduceLines(HWND hEdit, int iLines);
void Edit_ScrollToEnd(HWND hEdit);
// original [List|Combo]Box_FindItemData doesn't find item data but item string.
int ComboBox_CustomFindItemData(HWND hComboBox, const void* itemData) noexcept;
int ListBox_CustomFindItemData(HWND hList, const void* itemData) noexcept;
void PostMessageToAllWindows(UINT message);  // TODO: Maybe only to windows of our process?

std::optional<std::string> GetPb2InstallPath(void);
void StartServerbrowser(void);


//--------------------------------------------------------------------------------------------------
// Main Window
//--------------------------------------------------------------------------------------------------

void AutoKickTimerFunction() noexcept;
void MainWindowLogPb2LibExceptionsToConsole(std::function<void()> func);
void MainWindowAddOrUpdateOwnedServer(const Server* stable_server_ptr) noexcept;
void MainWindowRemoveOwnedServer(const Server* stored_server_ptr) noexcept;
void MainWindowUpdateAutoKickState() noexcept;
void MainWindowUpdatePlayersListview() noexcept;
void MainWindowRefetchServerInfo() noexcept;

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
HBRUSH OnMainWindowCtlColorStatic(HWND hwnd, HDC hdc, HWND hwndChild, int type);
void OnMainWindowGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo);
void OnMainWindowJoinServer(void);
void OnMainWindowKickPlayer(void);
int  CALLBACK OnMainWindowListViewSort(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
int  OnMainWindowNotify(HWND hwnd, int id, NMHDR* nmh);
void OnMainWindowOpenDPLogin(void);
void OnMainWindowOpenWhois(void);
void OnMainWindowSendRcon(void);
void OnMainWindowSize(HWND hwnd, UINT state, int cx, int cy);
void OnMainWindowPlayersReady() noexcept;
void OnMainWindowServerCvarsReady() noexcept;
void OnMainWindowRconResponseReady() noexcept;
void OnMainWindowHostnameReady() noexcept;

//--------------------------------------------------------------------------------------------------
// Forcejoin Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ForcejoinDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnForcejoinClose(HWND hwnd);
void OnForcejoinCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnForcejoinInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
void OnForcejoinKeyDown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags);

//--------------------------------------------------------------------------------------------------
// Manage IDs Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageIDsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageIDsClose(HWND hwnd);
void OnManageIDsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnManageIDsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

//--------------------------------------------------------------------------------------------------
// Manage IPs Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageIPsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageIPsClose(HWND hwnd);
void OnManageIPsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnManageIPsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
void OnManageIPsReloadContent(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Manage Rotation Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageRotationDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageRotationClose(HWND hwnd);
void OnManageRotationCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnManageRotationInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam);
void OnManageRotationPaint(HWND hwnd);
void OnManageRotationReloadContent(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Manage Servers Dialog
//--------------------------------------------------------------------------------------------------
void ManageServersAddOrUpdateServer(HWND list, const Server* stable_server_ptr) noexcept;
void ManageServersRemoveServer(HWND list, const Server* stored_server_ptr) noexcept;
void ManageServersFetchHostname(HWND hDlg, Server* server) noexcept;

LRESULT CALLBACK ManageServersDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageServersClose(HWND hwnd);
BOOL OnManageServersInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
void OnManageServersServerlistReady(HWND hWndDlg) noexcept;
void OnManageServersHostnameReady(HWND hWndDlg, Server* server_instance);
void OnManageServersCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);

//--------------------------------------------------------------------------------------------------
// Program Settings Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ProgramSettingsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnProgramSettingsClose(HWND hwnd);
void OnProgramSettingsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnProgramSettingsInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam);
void OnProgramSettingsHScroll(HWND hwnd, HWND hwndCtl, UINT code, int pos);

//--------------------------------------------------------------------------------------------------
// RCON Commands Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK RCONCommandsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnRCONCommandsClose(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Set Ping Dialog
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK SetPingDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnSetPingClose(HWND hwnd);
void OnSetPingCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnSetPingInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

#endif // __MAIN_H_INCLUDED