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

struct LoadServersArgs {
	size_t uid = 0;
	HWND hwnd = NULL;
};

// TODO: Mark stuff as noexcept
// TODO: Clang format, clang tidy

static const std::string unreachable_hostname = "Server did not respond -- Offline?";

[[noreturn]] void HandleCriticalError(const std::string& message) noexcept;

std::string ConfigLocation(void);
int  LoadConfig(void);
void SaveConfig(void);
void DeleteConfig(void);

size_t GetFirstUnusedMapKey(const std::map<size_t, HANDLE>& m);

UINT RegisterWindowMessageOrCriticalError(const std::string& message_name) noexcept;
void SetClipboardContent(const std::string& content);
std::string GetHttpResponse(const std::string& url);
void SplitIpAddressToBytes(char* szIp, BYTE* pb0, BYTE* pb1, BYTE* pb2, BYTE* pb3);

void Edit_ReduceLines(HWND hEdit, int iLines);
void Edit_ScrollToEnd(HWND hEdit);
void ListView_SetImage(HWND hListview, std::string_view sImagePath);
void PostMessageToAllWindows(UINT message);  // TODO: Maybe only to windows of our process?

std::optional<std::string> GetPb2InstallPath(void);
void StartServerbrowser(void);

void AutoKickTimerFunction() noexcept;
void MainWindowUpdateAutoKickState() noexcept;
void MainWindowUpdatePlayersListview() noexcept;
void SignalAllThreads(std::map<size_t, HANDLE> * map);
pb2lib::Server* MainWindowGetSelectedServerOrLoggedNull() noexcept;
void MainWindowWriteConsole(std::string_view);
void LoadBannedIPsToListbox(HWND hListBox);
void LoadRotationToListbox(HWND hListBox);
void LoadServersToListbox(LPVOID lpArgumentStruct);
void ShowAboutDialog(HWND hwnd);
void ShowPlayerInfo(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Callback Main Window                                                                            |
//--------------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------
// Callback Forcejoin Dialog                                                                       |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ForcejoinDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnForcejoinClose(HWND hwnd);
void OnForcejoinCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnForcejoinInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
void OnForcejoinKeyDown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags);

//--------------------------------------------------------------------------------------------------
// Callback Manage IDs Dialog                                                                      |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageIDsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageIDsClose(HWND hwnd);
void OnManageIDsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnManageIDsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

//--------------------------------------------------------------------------------------------------
// Callback Manage IPs Dialog                                                                      |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageIPsDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageIPsClose(HWND hwnd);
void OnManageIPsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnManageIPsInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);
void OnManageIPsReloadContent(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Callback Manage Rotation Dialog                                                                 |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageRotationDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageRotationClose(HWND hwnd);
void OnManageRotationCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnManageRotationInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam);
void OnManageRotationPaint(HWND hwnd);
void OnManageRotationReloadContent(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Callback Manage Servers Dialog                                                                  |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ManageServersDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnManageServersClose(HWND hwnd);
void OnManageServersCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
void OnManageServersDestroy(HWND hwnd);
BOOL OnManageServersInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

//--------------------------------------------------------------------------------------------------
// Callback Program Settings Dialog                                                                |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK ProgramSettingsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnProgramSettingsClose(HWND hwnd);
void OnProgramSettingsCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnProgramSettingsInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam);
void OnProgramSettingsHScroll(HWND hwnd, HWND hwndCtl, UINT code, int pos);

//--------------------------------------------------------------------------------------------------
// Callback RCON Commands Dialog                                                                   |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK RCONCommandsDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnRCONCommandsClose(HWND hwnd);

//--------------------------------------------------------------------------------------------------
// Callback Set Ping Dialog                                                                        |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK SetPingDlgProc (HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void OnSetPingClose(HWND hwnd);
void OnSetPingCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
BOOL OnSetPingInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam);

#endif // __MAIN_H_INCLUDED