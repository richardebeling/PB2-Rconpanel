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

#define WIN32_LEAN_AND_MEAN
#include <windowsx.h>

#if _WIN32_IE < 0x401
	#define _WIN32_IE 0x401 //needed for commctrl.h so it includes InitCommonControls (0x300) and the ability to color subitems differently (0x400)
#endif
#include <commctrl.h>

#include <shellapi.h>
#include <wininet.h>

#include <assert.h>
#include <ctime>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

#include <Gdiplus.h>

struct WindowHandles
{
	HWND hDlgManageRotation;
	HWND hDlgManageIds;
	HWND hDlgManageIps;
	HWND hDlgSettings;
	HWND hDlgRconCommands;
	HWND hWinMain;
	HWND hComboServer;
	HWND hListPlayers;
	HWND hButtonKick;
	HWND hButtonBanID;
	HWND hButtonBanIP;
	HWND hButtonReload;
	HWND hButtonDPLoginProfile;
	HWND hButtonUtrace;
	HWND hButtonForcejoin;
	HWND hEditConsole;
	HWND hComboRcon;
	HWND hButtonSend;
	HWND hButtonJoin;
	HWND hStaticServerInfo;
};

struct Colors
{
	static constexpr DWORD dwRed    = RGB(255, 100, 60);
	static constexpr DWORD dwBlue   = RGB(100, 150, 255);
	static constexpr DWORD dwPurple = RGB(225, 0,   225);
	static constexpr DWORD dwYellow = RGB(240, 240, 0);
	static constexpr DWORD dwWhite  = RGB(255, 255, 255);
};

struct Ban
{
	enum class Type {
		ID = 0,
		NAME = 1,
	};

    Type tType;
    std::string sText;
};

struct LoadServersArgs
{
	int iKey;
	HWND hwnd;
};

std::unique_ptr<Gdiplus::Bitmap> CreateResizedBitmapClone(Gdiplus::Bitmap *bmp, unsigned int width, unsigned int height);
int iGetFirstUnusedMapIntKey(const std::map<int, HANDLE>& m);

std::string ConfigLocation(void);
int  LoadConfig(void);
void SaveConfig(void);
void DeleteConfig(void);

void AutoReloadThreadFunction(void);
void BanThreadFunction(void);
void Edit_ReduceLines(HWND hEdit, int iLines);
void Edit_ScrollToEnd(HWND hEdit);
int  GetPb2InstallPath(std::string * sPath);
void MainWindowRefreshThread(LPVOID lpArgument);
inline bool MainWindowRefreshThreadExitIfSignaled(int iUID, SOCKET hSocket);
int  MainWindowLoadPlayers(SOCKET hSocket, HANDLE hExitEvent);
int  MainWindowLoadServerInfo(SOCKET hSocket, HANDLE hExitEvent);
inline void SignalAllThreads(std::map<int, HANDLE> * m);
void MainWindowWriteConsole(std::string_view);
void ListView_SetImage(HWND hListview, std::string_view sImagePath);
std::string ListView_CustomGetItemText(HWND hListView, int iItemIndex, int iSubItem);
void LoadBannedIPsToListbox(HWND hListBox);
void LoadRotationToListbox(HWND hListBox);
void LoadServersToListbox(LPVOID lpArgumentStruct);
void ShowAboutDialog(HWND hwnd);
void ShowPlayerInfo(HWND hwnd);
void SplitIpAddressToBytes(char * szIp, BYTE * pb0, BYTE * pb1, BYTE * pb2, BYTE * pb3);
void StartServerbrowser(void);

//--------------------------------------------------------------------------------------------------
// Callback Main Window                                                                            |
//--------------------------------------------------------------------------------------------------
LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK MainWindowEnumChildProc(HWND hwnd, LPARAM lParam);
void OnMainWindowBanID(void);
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
void OnMainWindowOpenUtrace(void);
int  OnMainWindowSendRcon(void); //according to msdn functions used for threads should return sth.
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