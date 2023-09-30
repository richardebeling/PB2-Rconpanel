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
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

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


// TODO: Move to separate files
#include <functional>
#include <chrono>
#include <condition_variable>

// Runs a background thread that triggers at regular intervals
class AsyncRepeatedTimer {
public:
	using Duration = std::chrono::duration<double>;

	AsyncRepeatedTimer() : thread_([this](auto&& arg) {thread_function(std::forward<decltype(arg)>(arg)); }) {}

	void set_interval(std::chrono::duration<double> interval) noexcept { // 0-duration = disable
		{
			std::lock_guard guard(mutex_);
			interval_ = interval;
		}
		condition_variable_.notify_all();
	}

	void set_interval(int interval) noexcept { return set_interval(Duration(interval)); }

	void reset_current_timeout() noexcept {
		std::lock_guard guard(mutex_);
		last_triggered_at_ = std::chrono::steady_clock::now();
	};

	void set_trigger_action(std::function<void()> trigger_action) noexcept {
		std::lock_guard guard(mutex_);
		trigger_action_ = std::move(trigger_action);
	}

private:
	void thread_function(std::stop_token stop_token) {
		std::unique_lock<std::mutex> lock(mutex_);
		while (!stop_token.stop_requested()) {
			if (interval_ != Duration(0) && std::chrono::steady_clock::now() > last_triggered_at_ + interval_) {
				trigger_action_();
				last_triggered_at_ = std::chrono::steady_clock::now();
			}

			// Wait for stop_request or interval becoming non-0 (without timeout)
			condition_variable_.wait(lock, stop_token, [&]() { return interval_ != Duration(0); });

			// Wait for interval change, stop request, or timeout with current interval to trigger
			condition_variable_.wait_until(lock, stop_token, last_triggered_at_ + interval_, [&]() {
				return std::chrono::steady_clock::now() > last_triggered_at_ + interval_;
			});
		}
	}

	std::chrono::steady_clock::time_point last_triggered_at_;
	Duration interval_;

	std::function<void()> trigger_action_;

	std::condition_variable_any condition_variable_;
	std::mutex mutex_;
	std::jthread thread_; // needs to be destroyed first
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
	HWND hButtonBanID = NULL;
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

struct Colors {
	static constexpr DWORD dwRed    = RGB(255, 100, 60);
	static constexpr DWORD dwBlue   = RGB(100, 150, 255);
	static constexpr DWORD dwPurple = RGB(225, 0,   225);
	static constexpr DWORD dwYellow = RGB(240, 240, 0);
	static constexpr DWORD dwWhite  = RGB(255, 255, 255);
};

DWORD ColorFromTeam(pb2lib::Team team) {
	switch (team) {
		case pb2lib::Team::BLUE: return Colors::dwBlue;
		case pb2lib::Team::RED: return Colors::dwRed;
		case pb2lib::Team::PURPLE: return Colors::dwPurple;
		case pb2lib::Team::YELLOW: return Colors::dwYellow;
		case pb2lib::Team::OBSERVER: return Colors::dwWhite;
		case pb2lib::Team::AUTO: return Colors::dwWhite;
	}

	assert(false);
	return Colors::dwWhite;
}

struct Ban {
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
std::unique_ptr<Gdiplus::Bitmap> CreateResizedBitmapClone(Gdiplus::Bitmap* bmp, unsigned int width, unsigned int height);

void Edit_ReduceLines(HWND hEdit, int iLines);
void Edit_ScrollToEnd(HWND hEdit);
void ListView_SetImage(HWND hListview, std::string_view sImagePath);
void PostMessageToAllWindows(UINT message);  // TODO: Maybe only to windows of our process?

std::optional<std::string> GetPb2InstallPath(void);
void StartServerbrowser(void);

void BanThreadFunction(void);
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