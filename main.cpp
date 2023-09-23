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
//TODO: Add: Dialog that can be used to change servers settings and save current settings as configuration file for a server (IDD_MANAGECVARS is the current placeholder)
//TODO: Fix ip addresses not being shown sometimes (because assignment of information to the player fails?)
//TODO: Option to draw nothing instead of a 0 in the listview when no information is given.
//TODO: IDs and IPs Dialog: Show information about these numbers (maybe a button that links to dplogin and utrace?)
//TODO: Disable "Ban IP" button when the IP was not loaded correctly


#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#define strcasecmp _stricmp
#endif

#include "rconfunctions.h"
#include "resource.h"
#include "main.h"
#include "version.h"
#include "settings.h"

std::vector<Server> g_vSavedServers;	//used to store servers in the combo box
std::vector<Server> g_vAllServers;		//used to fill server list in the "Manage servers" dialog
std::vector<Player> g_vPlayers;			//used to fill the list view in the main window
std::vector<Ban> 	g_vBannedPlayers;	//stores either ID or name of a banned player as string

HANDLE g_hAutoReloadThread  = 0;
HANDLE g_hBanThread         = 0;
HANDLE g_hSendRconThread    = 0;

std::map <int, HANDLE> g_mRefreshThreads;		//contains UID and ExitEvent for every reload thread
std::map <int, HANDLE> g_mLoadServersThreads;	//contains UID and ExitEvent for every reload thread

bool g_bDontWriteConfig = false;
int g_iAutoReloadThreadTimeWaitedMsecs; // global so it can be reset when the list is manually reset

UINT WM_RELOADCONTENT; //custom message that is sent to child windows to make them reload their content

const HBRUSH g_hConsoleBackgroundBrush = (HBRUSH) CreateSolidBrush(RGB(255, 255, 255));
HFONT g_hFont, g_hConsoleFont;

WindowHandles gWindows; // stores all window handles
SETTINGS gSettings;     // stores all program settings

ULONG_PTR g_gdiplusStartupToken;
std::unique_ptr<Gdiplus::Bitmap> g_pMapshotBitmap;
std::unique_ptr<Gdiplus::Bitmap> g_pMapshotBitmapResized;

//--------------------------------------------------------------------------------------------------
// Program Entry Point                                                                             |
//{-------------------------------------------------------------------------------------------------

int WINAPI WinMain (HINSTANCE hThisInstance, HINSTANCE hPrevInstance, LPSTR lpszArgument, int nCmdShow)
{
	MSG messages;
	WNDCLASSEX wincl = { 0 };
	CHAR szClassName[ ] = "Rconpanel\0";
	INITCOMMONCONTROLSEX icex = { 0 };			//needed for list view control

	WM_RELOADCONTENT =  RegisterWindowMessage("RCONPANEL_RELOADCONTENT");
	if (!WM_RELOADCONTENT)
	{
		MessageBox(NULL, "Could not register RELOADCONTENT message", NULL, MB_OK | MB_ICONERROR);
		exit(-1);
	}

	icex.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);
	
	if (OleInitialize(NULL) != S_OK) {
		MessageBox(NULL, "OleInitialize returned non-ok status", NULL, MB_OK | MB_ICONERROR);
		exit(-1);
	}
	
	Gdiplus::GdiplusStartupInput gsi;
	Gdiplus::GdiplusStartup(&g_gdiplusStartupToken, &gsi, NULL);

	wincl.hInstance = hThisInstance;
	wincl.lpszClassName = szClassName;
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

	if (!RegisterClassEx (&wincl))
	{
		MessageBox(NULL, "Could not register window class. Will now exit.", NULL, MB_OK | MB_ICONERROR);
		exit(-1);
	}
	
	if (InitializeWinsock() != 0)
	{
		MessageBox(NULL, "InitializeWinsock returned 0. Will now exit.", NULL, MB_OK | MB_ICONERROR);
		exit(-1);
	}

	DWORD dwBaseUnits = GetDialogBaseUnits();
	gWindows.hWinMain = CreateWindowEx (0,
						szClassName, "DP:PB2 Rconpanel\0",
						WS_OVERLAPPEDWINDOW,
						CW_USEDEFAULT, CW_USEDEFAULT,
						MulDiv(285, LOWORD(dwBaseUnits), 4),
						MulDiv(290, HIWORD(dwBaseUnits), 8),
						HWND_DESKTOP,
						LoadMenu (hThisInstance, MAKEINTRESOURCE(IDM)),
						hThisInstance, NULL);
	
	gWindows.hDlgManageRotation = NULL;
	gWindows.hDlgManageIds = NULL;
	gWindows.hDlgManageIps = NULL;
	gWindows.hDlgSettings = NULL;
	gWindows.hDlgRconCommands = NULL;
	
	ShowWindow(gWindows.hWinMain, nCmdShow);
						
	//All other windows will be created in OnMainWindowCreate

	while (GetMessage (&messages, NULL, 0, 0))
	{
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}
	return 0;
}

//}-------------------------------------------------------------------------------------------------
// Main Window Functions                                                                           |
//{-------------------------------------------------------------------------------------------------

void ShowPlayerInfo(HWND hwnd)
{
	SetWindowText(hwnd, "DP:PB2 Rconpanel - Retrieving Information...");
	
	std::string sBoxContent = "Information about player ";
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);
	if (iSelectedRow == -1)
		return;

	std::string sPlayerName = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iName);
	sBoxContent.append(sPlayerName);
	sBoxContent.append("\r\n\r\n");

	std::string sPlayerId = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iId);
	if (sPlayerId != "0")
	{
		std::string sPlayersite ("");
		std::string sUrlBuffer = "http://www.dplogin.com/index.php?action=viewmember&playerid=";
		sUrlBuffer.append(sPlayerId);
		std::string sUserAgent = "Digital Paint: Paintball 2 RCON Panel V" + std::to_string(AutoVersion::MAJOR)
							+ "." + std::to_string(AutoVersion::MINOR) + "." + std::to_string(AutoVersion::BUILD);
		HINTERNET hInternet = InternetOpen(sUserAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		HINTERNET hFile = InternetOpenUrl(hInternet, sUrlBuffer.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
		std::vector<char> buffer(MTU);
		long unsigned int iBytesRead = 0;
		bool bSuccessful = true;
		while (bSuccessful)
		{
			InternetReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &iBytesRead);

			if (bSuccessful && iBytesRead == 0) break;
			buffer[iBytesRead] = '\0';
			sPlayersite.append(buffer.data());
		}
		InternetCloseHandle(hFile);
		InternetCloseHandle(hInternet);

		std::smatch MatchResults;
		std::regex rx ("<tr><td><b class=\"faqtitle\">(.+?:)</b></td><td>(.+?)</td></tr>");
		//												1-VARNAME		2-CONTENT
		std::regex rxWebcodes("(&gt;)|(&quot;)");
		
		std::regex rxLink1("<a href=\\\".+?\\\">");
		std::regex rxLink2("</a>");
		std::regex rxMail("&#([x0-9a-fA-F]+?);");
				
		std::string::const_iterator start = sPlayersite.begin();
		std::string::const_iterator end = sPlayersite.end();
		
		while (std::regex_search(start, end, MatchResults, rx))
		{
			start = MatchResults[0].second;
			
			sBoxContent.append(MatchResults[1]);
			sBoxContent.append(" ");
			
			std::string sContent = MatchResults[2]; //Content may contain links, must remove them			
			sContent = std::regex_replace(sContent, rxLink1, "");
			sContent = std::regex_replace(sContent, rxLink2, "");
			
			sContent = std::regex_replace(sContent, rxWebcodes, ""); //Content may contain web codes like &quot; ot &gt;
			
			if (std::string("Email:").compare(MatchResults[1]) == 0) //email is encoded as char values, this has to be made readable for humans.
			{
				std::string::const_iterator startMail = sContent.begin();
				std::string::const_iterator endMail = sContent.end();
				std::smatch MatchResultsMail;
				std::vector <char> vChars;
				std::string sNum;
				
				while (std::regex_search(startMail, endMail, MatchResultsMail, rxMail))
				{
					startMail = MatchResultsMail[0].second;
					sNum.assign(MatchResultsMail[1]);
					if (sNum.compare(0, 1, "x") == 0)
						vChars.push_back((char) strtol(sNum.substr(1, sNum.length() - 1).c_str(), NULL, 16));
					else
						vChars.push_back((char) strtol(sNum.c_str(), NULL, 10));
				}
				vChars.push_back('\0');
				sContent.assign((char *)vChars.data());
			}
			
			sBoxContent.append(sContent);
			sBoxContent.append("\r\n");
		}
		sBoxContent.append("\r\n");
	}

	std::string sPlayerIp = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iIp);

	if (sPlayerIp != "0.0.0.0")
	{
		// TODO: utrace doesn't exist anymore -- use some other service
		std::string sUtraceXml;
		std::string sUrlBuffer = "http://xml.utrace.de/?query=";
		sUrlBuffer.append(sPlayerIp);
		std::string sUserAgent = "Digital Paint: Paintball 2 RCON Panel V" + std::to_string(AutoVersion::MAJOR)
							+ "." + std::to_string(AutoVersion::MINOR) + "." + std::to_string(AutoVersion::BUILD);
		HINTERNET hInternet = InternetOpen(sUserAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		HINTERNET hFile = InternetOpenUrl(hInternet, sUrlBuffer.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
		std::vector<char> buffer(MTU);
		long unsigned int iBytesRead = 0;
		bool bSuccessful = true;
		while (bSuccessful)
		{
			InternetReadFile(hFile, buffer.data(), static_cast<int>(buffer.size()), &iBytesRead);

			if (bSuccessful && iBytesRead == 0) break;
			buffer[iBytesRead] = '\0';
			sUtraceXml.append(buffer.data());
		}
		InternetCloseHandle(hFile);
		InternetCloseHandle(hInternet);

		std::smatch MatchResults;
		std::regex rx ("\\Q<ip>\\E(.*?)\\Q</ip>\\E");
		if (std::regex_search(sUtraceXml, MatchResults, rx))
		{
			sBoxContent.append("IP: ");
			sBoxContent.append(MatchResults[1]);
			sBoxContent.append("\r\n");
		}
		rx.assign("\\Q<isp>\\E(.*?)\\Q</isp>\\E");
		if (std::regex_search(sUtraceXml, MatchResults, rx))
		{
			sBoxContent.append("ISP: ");
			sBoxContent.append(MatchResults[1]);
			sBoxContent.append("\r\n");
		}
		rx.assign("\\Q<region>\\E(.*?)\\Q</region>\\E");
		if (std::regex_search(sUtraceXml, MatchResults, rx))
		{
			sBoxContent.append("Region: ");
			sBoxContent.append(MatchResults[1]);
			sBoxContent.append("\r\n");
		}
		rx.assign("\\Q<countrycode>\\E(.*?)\\Q</countrycode>\\E");
		if (std::regex_search(sUtraceXml, MatchResults, rx))
		{
			sBoxContent.append("Countrycode: ");
			sBoxContent.append(MatchResults[1]);
			sBoxContent.append("\r\n");
		}
		sBoxContent.append("\r\nThis information was provided by dplogin.com and en.utrace.de");
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

int MainWindowLoadServerInfo(SOCKET hSocket, HANDLE hExitEvent)
{
	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
		return -3;
	
	if (hSocket == INVALID_SOCKET)
		return -1;

	std::string sAnswer;
	iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort,
						"echo $mapname;$password;$elim;$timelimit;$maxclients",
						&sAnswer, g_vSavedServers[selectedServerIndex].sRconPassword, hSocket, gSettings.fTimeoutSecs);
	
	sAnswer = sAnswer.substr(6, std::string::npos);

	std::string sContent;
	std::stringstream sAnswerStream{ sAnswer };
	int field = 0;
	std::string sCurrentValue;
	while(std::getline(sAnswerStream, sCurrentValue, ';'))
	{
		std::string sCurrentValueOrErr = (sCurrentValue.size() > 0) ? sCurrentValue : "err";
		switch(field)
		{
		case 0:
			sContent += "map: " + sCurrentValueOrErr;
			break;
		case 1:
			sContent += "  |  pw: ";
			sContent += (sCurrentValue.size() > 0) ? sCurrentValue : "none";
			break;
		case 2:
			sContent += "  |  elim: " + sCurrentValueOrErr;
			break;
		case 3:
			sContent += "  |  timelimit: " + sCurrentValueOrErr;
			break;
		case 4:
			sContent += "  |  maxclients: " + sCurrentValueOrErr;
			break;
		}
		++field;
	}
	
	if (WaitForSingleObject(hExitEvent, 0) == WAIT_OBJECT_0)
		return -2;
	
	SetWindowText(gWindows.hStaticServerInfo, sContent.c_str());

	HRGN hRegion = CreateRectRgn(0,0,0,0);
	GetWindowRgn(gWindows.hStaticServerInfo, hRegion);
	RedrawWindow(gWindows.hWinMain, NULL, hRegion, RDW_ERASE | RDW_INVALIDATE);
	DeleteObject(hRegion);
	return 1;
}

void MainWindowRefreshThread(LPVOID lpKey)
{
	int iKey = *((int*)lpKey);
	delete (int*)lpKey;
	
	SOCKET hSocket;
	HANDLE hExitEvent = g_mRefreshThreads.at(iKey);
	g_iAutoReloadThreadTimeWaitedMsecs = 0;
	
	if (g_vSavedServers.size() == 0)
	{
		MainWindowWriteConsole("There are no servers in your server list.");
		return;
	}
	
	hSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	
	MainWindowLoadPlayers(hSocket, hExitEvent);
		
	if (MainWindowRefreshThreadExitIfSignaled(iKey, hSocket)) return;
	
	MainWindowLoadServerInfo(hSocket, hExitEvent);
	
	if (MainWindowRefreshThreadExitIfSignaled(iKey, hSocket)) return;
	
	MainWindowWriteConsole("The player list was reloaded.");
	
	SetEvent(hExitEvent);
	MainWindowRefreshThreadExitIfSignaled (iKey, hSocket);
}

inline bool MainWindowRefreshThreadExitIfSignaled(int iKey, SOCKET hSocket)
{
	try
	{
		if (WaitForSingleObject(g_mRefreshThreads.at(iKey), 0) == WAIT_OBJECT_0)
		{
			CloseHandle(g_mRefreshThreads.at(iKey)); //Delete the Event
			closesocket(hSocket);
			g_mRefreshThreads.erase(g_mRefreshThreads.find(iKey));
			return true;
		}
		return false;
	}
	catch (const std::out_of_range&)
	{
		MessageBox(NULL, "Error when trying to access g_mRefreshThreads: out of range\n", "ACCESS ERROR", MB_OK | MB_ICONERROR);
		closesocket(hSocket);
		return true;
	}
}

inline void SignalAllThreads(std::map<int, HANDLE> * m)
{
	for (auto iterator = m->begin(); iterator != m->end(); iterator++)
	{
		SetEvent(iterator->second);
	}
}

int MainWindowLoadPlayers(SOCKET hSocket, HANDLE hExitEvent)
{
	ListView_DeleteAllItems(gWindows.hListPlayers);
	
	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
	{
		return -3;
	}

	int iRetVal = iPlayerStructVectorFromAddress (g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort,
										g_vSavedServers[selectedServerIndex].sRconPassword, &g_vPlayers, hSocket, gSettings.fTimeoutSecs);

	if (iRetVal > 0) //add players to the listview
	{
		LVITEM LvItem = { 0 };
		std::string sItemText;
		for(unsigned int y = 0; y < g_vPlayers.size(); y++)
		{
			LvItem.mask = LVIF_TEXT | LVIF_PARAM;
			LvItem.iItem = y;
			LvItem.iSubItem = SUBITEMS::iNumber;
			LvItem.lParam = y;
			sItemText.assign(std::to_string(g_vPlayers[y].iNumber));
			LvItem.pszText = (LPSTR) sItemText.c_str();
			SendMessage(gWindows.hListPlayers, LVM_INSERTITEM, 0, (LPARAM) &LvItem);

			for (int x = 1; x < 8; x++)
			{
				switch(x)
				{
				case SUBITEMS::iName:
					sItemText.assign(g_vPlayers[y].sName.c_str());
					break;
				case SUBITEMS::iBuild:
					sItemText.assign(std::to_string(g_vPlayers[y].iBuild));
					break;
				case SUBITEMS::iId:
					sItemText.assign(std::to_string(g_vPlayers[y].iId));
					break;
				case SUBITEMS::iOp:
					sItemText.assign(std::to_string(g_vPlayers[y].iOp));
					break;
				case SUBITEMS::iIp:
					sItemText.assign(g_vPlayers[y].sIp);
					break;
				case SUBITEMS::iPing:
					sItemText.assign(std::to_string(g_vPlayers[y].iPing));
					break;
				case SUBITEMS::iScore:
					sItemText.assign(std::to_string(g_vPlayers[y].iScore));
					break;
				}
				LvItem.mask=LVIF_TEXT;
				LvItem.iSubItem = x;
				LvItem.pszText = (LPSTR) sItemText.c_str();
				
				if (WaitForSingleObject(hExitEvent, 0) == WAIT_OBJECT_0)
				{
					return -2;
				}
				
				SendMessage(gWindows.hListPlayers, LVM_SETITEM, 0, (LPARAM) &LvItem);
			}
		}
	}
	ListView_SortItems (gWindows.hListPlayers, OnMainWindowListViewSort, 0);
	ListView_SetColumnWidth(gWindows.hListPlayers, 7, LVSCW_AUTOSIZE_USEHEADER);
	return 1;
}

void MainWindowWriteConsole(const std::string_view str) // prints text to gWindows.hEditConsole, adds timestamp and linebreak
{
	time_t rawtime;
	struct tm * timeinfo;
	char szBuffer[12];
	std::string sFinalString;
	DWORD start = 0, end = 0;

	time(&rawtime);
	timeinfo = localtime (&rawtime);

	//create timestamp
	sprintf (szBuffer, "[%02d:%02d:%02d] ", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
	sFinalString = szBuffer;

	//append text
	sFinalString.append(str);
	sFinalString = std::regex_replace(sFinalString, std::regex{"\n"}, "\n---------> "); //indent text after line ending

	while (sFinalString.ends_with("\n"))
		sFinalString = sFinalString.substr(0, sFinalString.length() - 2);

	sFinalString = std::regex_replace(sFinalString, std::regex{"\n"}, "\r\n");

	//add linebreak (if its not the first line) and the the text to the end of gWindows.hEditConsole
	SendMessage(gWindows.hEditConsole, EM_SETSEL, -2, -2);
	SendMessage(gWindows.hEditConsole, EM_GETSEL, (WPARAM) &start, (LPARAM)&end);
	if (start != 0)
		SendMessage(gWindows.hEditConsole, EM_REPLACESEL, 0,(LPARAM) "\r\n");

	//Add new text
	SendMessage(gWindows.hEditConsole, EM_REPLACESEL, 0,(LPARAM) sFinalString.c_str());

	//remove first line until linecount is equal to gSettings.iMaxConsoleLineCount
	if (gSettings.bLimitConsoleLineCount) Edit_ReduceLines(gWindows.hEditConsole, gSettings.iMaxConsoleLineCount);

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
		switch(lplvcd->iSubItem)
		{
		case SUBITEMS::iNumber:
		case SUBITEMS::iName:
		case SUBITEMS::iBuild:
		case SUBITEMS::iId:
		case SUBITEMS::iOp:
		case SUBITEMS::iIp:
		case SUBITEMS::iScore:
			if (!gSettings.bColorPlayers)
			{
				lplvcd->clrTextBk = Colors::dwWhite;
				return CDRF_NEWFONT;
			}
			
			switch (g_vPlayers.at(lplvcd->nmcd.lItemlParam).cColor)
			{
			case 'r':
				lplvcd->clrTextBk = Colors::dwRed;
				break;
			case 'b':
				lplvcd->clrTextBk = Colors::dwBlue;
				break;
			case 'p':
				lplvcd->clrTextBk = Colors::dwPurple;
				break;
			case 'y':
				lplvcd->clrTextBk = Colors::dwYellow;
				break;
			default:
				lplvcd->clrTextBk = Colors::dwWhite;
				break;
			}
			return CDRF_NEWFONT;
		
		case SUBITEMS::iPing:			
			if (gSettings.bColorPings)
			{
				if (g_vPlayers.at(lplvcd->nmcd.lItemlParam).iPing == 0) //99,9% bot
				{
					lplvcd->clrTextBk = Colors::dwWhite;
					return CDRF_NEWFONT; 
				}
				
				float fRed;
				float fGreen;
				float fHigherQuotient;
				
				fRed = (float) g_vPlayers.at(lplvcd->nmcd.lItemlParam).iPing;
				fRed = (fRed > 255) ? 255 : fRed;
				fGreen = 255 - fRed;
				
				fHigherQuotient = fGreen / 255; //Change colors so the higher one is 255 --> middle is not 128, 128 (brown) but 255, 255 (yellow)
				fHigherQuotient = ((fRed/255) > fHigherQuotient) ? fRed/255 : fHigherQuotient;
				
				fRed *= (float)(1/fHigherQuotient);
				fGreen *= (float)(1/fHigherQuotient);
				
				lplvcd->clrTextBk = RGB((int)fRed, (int)fGreen, 0);
				return CDRF_NEWFONT;
			}
			else if (gSettings.bColorPlayers)
			{				
				switch (g_vPlayers.at(lplvcd->nmcd.lItemlParam).cColor)
				{
				case 'r':
					lplvcd->clrTextBk = Colors::dwRed;
					break;
				case 'b':
					lplvcd->clrTextBk = Colors::dwBlue;
					break;
				case 'p':
					lplvcd->clrTextBk = Colors::dwPurple;
					break;
				case 'y':
					lplvcd->clrTextBk = Colors::dwYellow;
					break;
				default:
					lplvcd->clrTextBk = Colors::dwWhite;
					break;
				}
				return CDRF_NEWFONT;
			}
			return CDRF_NEWFONT;
			
		default:
			lplvcd->clrTextBk = Colors::dwWhite;
			return CDRF_NEWFONT;
		}
	}
    return CDRF_DODEFAULT;
}

BOOL CALLBACK MainWindowEnumChildProc(HWND hwnd, LPARAM lParam) //Call EnumWindows(MainWindowEnumChildProc, 0) to send a reload message to all child windows
{
	PostMessage(hwnd, WM_RELOADCONTENT, 0, 0);
	return TRUE;
}

int CALLBACK OnMainWindowListViewSort(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) //returns order of 2 items
{
	switch(lParamSort)
	{
	case SUBITEMS::iNumber:
		return g_vPlayers[lParam1].iNumber - g_vPlayers[lParam2].iNumber;
	case SUBITEMS::iName:
		return strcasecmp(g_vPlayers[lParam1].sName.c_str(), g_vPlayers[lParam2].sName.c_str());
	case SUBITEMS::iBuild:
		return g_vPlayers[lParam1].iBuild - g_vPlayers[lParam2].iBuild;
	case SUBITEMS::iId:
		return g_vPlayers[lParam1].iId - g_vPlayers[lParam2].iId;
	case SUBITEMS::iOp:
		return g_vPlayers[lParam1].iOp - g_vPlayers[lParam2].iOp;
	case SUBITEMS::iIp:
		return strcasecmp(g_vPlayers[lParam1].sIp.c_str(), g_vPlayers[lParam2].sIp.c_str());
	case SUBITEMS::iPing:
		return g_vPlayers[lParam1].iPing - g_vPlayers[lParam2].iPing;
	case SUBITEMS::iScore:
		return g_vPlayers[lParam1].iScore - g_vPlayers[lParam2].iScore;
	}
	return 0;
}

BOOL OnMainWindowCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
	//{ Create Controls
	DWORD dwBaseUnits = GetDialogBaseUnits();

	HWND hStaticServer = CreateWindowEx(0, "STATIC\0", "Server: \0",
						SS_SIMPLE | WS_CHILD | WS_VISIBLE,
						MulDiv(3  , LOWORD(dwBaseUnits), 4), // Units to Pixel
						MulDiv(4  , HIWORD(dwBaseUnits), 8),
						MulDiv(30 , LOWORD(dwBaseUnits), 4),
						MulDiv(8  , HIWORD(dwBaseUnits), 8),
						hwnd, NULL, NULL, NULL);

	//The following controls will be resized when the window is shown and HandleResize is called.
	gWindows.hComboServer = CreateWindowEx(WS_EX_CLIENTEDGE, "COMBOBOX\0", "\0",
						CBS_DROPDOWNLIST | CBS_SORT | WS_CHILD | WS_VISIBLE,
						0, 0, 0, CW_USEDEFAULT,	//automatically adapt to content
						hwnd, NULL, NULL, NULL);

	gWindows.hStaticServerInfo = CreateWindowEx(0, WC_STATIC, "\0",
						SS_LEFTNOWORDWRAP | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hListPlayers = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_RIGHTSCROLLBAR, WC_LISTVIEW, "\0",
						LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonJoin = CreateWindowEx(0, WC_BUTTON, "&Join\0", WS_CHILD | WS_VISIBLE , 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonReload = CreateWindowEx(0, WC_BUTTON, "&Reload\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonKick = CreateWindowEx(0, WC_BUTTON, "&Kick\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonBanID = CreateWindowEx(0, WC_BUTTON, "Ban I&D / Name\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonBanIP = CreateWindowEx(0, WC_BUTTON, "Ban I&P\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonDPLoginProfile = CreateWindowEx(0, WC_BUTTON, "&DPLogin Profile\0", WS_CHILD | WS_VISIBLE,
						0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonUtrace = CreateWindowEx(0, WC_BUTTON, "&uTrace\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonForcejoin = CreateWindowEx(0, WC_BUTTON, "&Forcejoin\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hComboRcon = CreateWindowEx(WS_EX_CLIENTEDGE, WC_COMBOBOX, "\0",
						CBS_AUTOHSCROLL | CBS_SIMPLE | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hButtonSend = CreateWindowEx(0, WC_BUTTON, "&Send Rcon\0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	gWindows.hEditConsole = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, "\0",
						WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_READONLY | WS_CHILD | WS_VISIBLE,
						0, 0, 0, 0,
						hwnd, NULL, NULL, NULL);

	//}

	HDC hdc = GetDC(NULL);
	LONG lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(NULL, hdc);
	g_hFont = CreateFont(lfHeight, 0, 0, 0, 0, FALSE, 0, 0, 0, 0, 0, 0, 0, "MS Shell Dlg\0");
	g_hConsoleFont = CreateFont(lfHeight, 0, 0, 0, 0, FALSE, 0, 0, 0, 0, 0, 0, 0, "Courier New\0");

	SendMessage(hStaticServer				  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hStaticServerInfo	  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hComboServer		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hListPlayers		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonJoin		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonKick		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonBanID		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonBanIP		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonReload		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonDPLoginProfile, WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonUtrace		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonForcejoin	  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hComboRcon			  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hButtonSend		  , WM_SETFONT, WPARAM(g_hFont), true);
	SendMessage(gWindows.hEditConsole		  , WM_SETFONT, WPARAM(g_hConsoleFont), true);
	
	SendMessage(gWindows.hEditConsole		  , EM_SETLIMITTEXT, WPARAM(0), LPARAM(0));

	LVCOLUMN lvc;
	char szText[32]; //maximum: "Build\0"
	lvc.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
	for (int i = 0; i <= 7; i++)
	{
		lvc.iSubItem = i;
		lvc.pszText = szText;
		lvc.fmt = LVCFMT_RIGHT;
		switch (i)
		{
			case SUBITEMS::iNumber: strcpy(szText, "Num\0");   break;
			case SUBITEMS::iName:   strcpy(szText, "Name\0");  lvc.fmt = LVCFMT_LEFT; break;
			case SUBITEMS::iBuild:  strcpy(szText, "Build\0"); break;
			case SUBITEMS::iId:     strcpy(szText, "ID\0");    break;
			case SUBITEMS::iOp:     strcpy(szText, "OP\0");    break;
			case SUBITEMS::iIp:     strcpy(szText, "IP\0");    lvc.fmt = LVCFMT_LEFT; break;
			case SUBITEMS::iPing:   strcpy(szText, "Ping\0");  break;
			case SUBITEMS::iScore:  strcpy(szText, "Score\0"); break;
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
		MainWindowWriteConsole("Error while reading bans from config file.");
	else if (retVal == 1)
		MainWindowWriteConsole("Success.");
	else
		MainWindowWriteConsole("!!! Unexpected error when loading the configuration file: " + std::to_string(retVal) + " !!!");
	
	int * piKey;
	HANDLE hThread;
	
	piKey = new int; //will be deleted as first operation in thread
	*piKey = iGetFirstUnusedMapIntKey(g_mRefreshThreads);
	
	g_mRefreshThreads.insert(std::pair<int, HANDLE>(*piKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) MainWindowRefreshThread, piKey, 0, NULL);
	if (hThread == NULL) {
		MessageBox(NULL, "Error while starting the refresh thread", "Error", MB_OK | MB_ICONERROR);
		return false;
	}
	CloseHandle(hThread);
	
	return true;
	//Will make the window procedure return 0 because the message cracker changes the return value:
	//#define HANDLE_WM_CREATE(hwnd,wParam,lParam,fn) (LRESULT)((fn)((hwnd),(LPCREATESTRUCT)(lParam)) ? 0 : -1)
}

void OnMainWindowForcejoin(void)
{
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);
	if (iSelectedRow == -1)
	{
		MainWindowWriteConsole("Please select a player first.");
		return;
	}

	std::string sOldName = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iName);
	std::string sOldNumber = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iNumber);

	auto iSelectedColor = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_FORCEJOIN), gWindows.hWinMain, (DLGPROC) ForcejoinDlgProc);
	if (iSelectedColor == -1)
		return;
	
	int * piKey;
	
	SignalAllThreads(&g_mRefreshThreads);
	
	piKey = new int; //will be deleted as first operation in thread
	*piKey = iGetFirstUnusedMapIntKey(g_mRefreshThreads);
	
	g_mRefreshThreads.insert(std::pair<int, HANDLE>(*piKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
	MainWindowRefreshThread(piKey);
	
	for (unsigned int i = 0; i < g_vPlayers.size(); i++)
	{
		if (atoi(sOldNumber.c_str()) == g_vPlayers[i].iNumber)
		{
            if (sOldName == g_vPlayers[i].sName)
			{
				auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
					if (selectedServerIndex == CB_ERR) return;

				auto& selectedServer = g_vSavedServers[selectedServerIndex];

				std::string sMsgBuffer = "sv forcejoin " + std::to_string(g_vPlayers[i].iNumber) + " " + (char) iSelectedColor;
				std::string sAnswer;
				iSelectedColor = iSendMessageToServer(selectedServer.sIp, selectedServer.iPort,
									sMsgBuffer, &sAnswer,
									selectedServer.sRconPassword, 0,
									gSettings.fTimeoutSecs);

				if (iSelectedColor > 0)
				{
					MainWindowWriteConsole("Player was force-joined successfully.");
					return;
				}
				else
				{
					MainWindowWriteConsole("Force-joining failed.");
					return;
				}
			}
		}
	}
	MainWindowWriteConsole("It seems like the player disconnected. They were not forcejoined.");
	MainWindowWriteConsole("The player list was reloaded.");
}

int OnMainWindowSendRcon(void) //according to msdn functions used for threads should return sth.
{
	if (g_vSavedServers.size() == 0)
	{
		MainWindowWriteConsole("There are no servers in your server list.");
		return 0;
	}
	std::string sAnswer;
	int iBufferSize = GetWindowTextLength(gWindows.hComboRcon) + 1;
	std::vector<char> commandBuffer(iBufferSize);
	GetWindowText(gWindows.hComboRcon, commandBuffer.data(), iBufferSize);

	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
	{
		return 0;
	}
	auto& selectedServer = g_vSavedServers[selectedServerIndex];
	
	//send the content of gWindows.hComboRcon to the server and receive the answer
	if (iSendMessageToServer(selectedServer.sIp, selectedServer.iPort, commandBuffer.data(), &sAnswer, selectedServer.sRconPassword, 0, gSettings.fTimeoutSecs) > 0)
	{
		MainWindowWriteConsole("Got answer from Server:\r\n" + sAnswer); //print the answer to the console
	}
	else
	{
		MainWindowWriteConsole("Server did not answer.");
	}
	
	return 1;
}

void OnMainWindowJoinServer(void)
{
	if (g_vSavedServers.size() == 0)
	{
		MainWindowWriteConsole("There are no servers in your server list.");
		return;
	}

	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
		return;

	std::string sArgs = "+connect ";
	sArgs.append(g_vSavedServers.at(selectedServerIndex).sIp);
	sArgs.append(":");
	sArgs.append(std::to_string(g_vSavedServers.at(selectedServerIndex).iPort));

	std::string sPb2Path;
	if (GetPb2InstallPath(&sPb2Path))
	{
		sPb2Path.append("\\paintball2.exe");
		auto ret = (INT_PTR) ShellExecute(0, "open", sPb2Path.c_str(), sArgs.c_str(), 0, 1); //start it
		if (ret <= 32)
		{
			MainWindowWriteConsole("Error while starting:\r\n" + sPb2Path + "\r\nShellExecute returned: " + std::to_string(ret));
		}
	}
	else
	{
		MainWindowWriteConsole("Could not find the path of you DP:PB2 install directory in the registry.");
	}
}

void OnMainWindowOpenUtrace(void)
{
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);
	if (iSelectedRow == -1)
	{
		MainWindowWriteConsole("Please select a player first.");
		return;
	}

	std::string sPlayerIp = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iIp);

	// TODO: uTrace is out of service
	std::string sUrl = "http://www.utrace.de/?query=" + sPlayerIp;
	ShellExecute(0, "open", sUrl.c_str(), 0, 0, 1);
}

void OnMainWindowOpenDPLogin(void)
{
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);
	if (iSelectedRow == -1)
	{
		MainWindowWriteConsole("Please select a player first.");
		return;
	}

	std::string sPlayerId = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iId);
	std::string sPlayerName = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iName);

	std::string sUrl;
	if (sPlayerId == "0")
	{
		sUrl = "http://dplogin.com/index.php?action=displaymembers&search=" + sPlayerName;
	}
	else
	{
		sUrl = "http://dplogin.com/index.php?action=viewmember&playerid=" + sPlayerId;
	}

	ShellExecute(0, "open", sUrl.c_str(), 0, 0, 1);
}

void OnMainWindowKickPlayer(void)
{
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);

	if (iSelectedRow == -1)
	{
		MainWindowWriteConsole("Please select a player first.");
		return;
	}

	std::string sOldName = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iName);
	std::string sOldNumber = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iNumber);

	int * piKey;
	
	SignalAllThreads(&g_mRefreshThreads);
	
	piKey = new int; //will be deleted as first operation in thread
	*piKey = iGetFirstUnusedMapIntKey(g_mRefreshThreads);
	
	g_mRefreshThreads.insert(std::pair<int, HANDLE>(*piKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
	MainWindowRefreshThread(piKey);
			
	for (unsigned int i = 0; i < g_vPlayers.size(); i++)
	{		
		if (atoi(sOldNumber.c_str()) == g_vPlayers[i].iNumber)
		{
            if (sOldName == g_vPlayers[i].sName)
			{
				auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
				if (selectedServerIndex == CB_ERR) return;
				std::string sMsg = "kick " + std::to_string(g_vPlayers[i].iNumber);
				std::string sAnswer;
					
				int iRetVal = iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort,
									sMsg, &sAnswer,
									g_vSavedServers[selectedServerIndex].sRconPassword, 0,
									gSettings.fTimeoutSecs);

				if (iRetVal > 0)
				{
					MainWindowWriteConsole("Player was kicked successfully.");
					MainWindowWriteConsole("The server answered: " + sAnswer);
					return;
				}
				else
				{
					MainWindowWriteConsole("Kicking failed.");
					return;
				}
			}
			else
				MainWindowWriteConsole("It seems like the player changed his name or another player"
							"joined his slot. The player was not kicked.");
				MainWindowWriteConsole("The player list was reloaded.");
			return;
		}
	}
	MainWindowWriteConsole("It seems like the player disconnected. He was not kicked.");
	MainWindowWriteConsole("The player list was reloaded.");
}

void OnMainWindowBanIP(void)
{
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);
	if (iSelectedRow == -1)
	{
		MainWindowWriteConsole("Please select a player first.");
		return;
	}

	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
		return;

	std::string sIp = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iIp);
	std::string sMsg = "sv addip " + sIp;

	std::string sAnswer;
	int iRetVal = iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, sMsg, &sAnswer,
						g_vSavedServers[selectedServerIndex].sRconPassword, 0, gSettings.fTimeoutSecs);
	
	if (iRetVal > 0) {
		MainWindowWriteConsole("The IP " + sIp + " was successfully added to servers ban list.");
	}
	else {
		MainWindowWriteConsole("An error occurred. The IP was not banned.");
	}
}

void OnMainWindowBanID(void)
{
	auto iSelectedRow = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1,LVNI_SELECTED);

	if (iSelectedRow == -1)
	{
		MainWindowWriteConsole("Please select a player first.");
		return;
	}

	std::string sPlayerId = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iId);
	if (sPlayerId == "0")
	{
		std::string sPlayerName = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedRow), SUBITEMS::iName);
		g_vBannedPlayers.emplace_back(Ban::Type::NAME, sPlayerName);
		MainWindowWriteConsole("Name " + std::string(sPlayerName) + " was banned");
	}
	else
	{
		g_vBannedPlayers.emplace_back(Ban::Type::ID, sPlayerId);
		MainWindowWriteConsole("ID " + std::string(sPlayerId) + " was banned");
	}
}

void OnMainWindowDestroy(HWND hwnd)
{
	if (!g_bDontWriteConfig) // save configuration first so change of variables is possible afterwards
		SaveConfig();
	
	gSettings.bRunAutoReloadThread = 0;
	gSettings.bRunBanThread = 0;
	
	HANDLE rHandles[3] = {g_hBanThread, g_hAutoReloadThread, g_hSendRconThread};
	WaitForMultipleObjects(3, rHandles, TRUE, 10000);
	
	if (g_hBanThread != INVALID_HANDLE_VALUE)
	{
		TerminateThread(g_hBanThread, 0);
		CloseHandle(g_hBanThread);
	}
	
	if (g_hAutoReloadThread != INVALID_HANDLE_VALUE)
	{
		TerminateThread(g_hAutoReloadThread, 0);
		CloseHandle(g_hAutoReloadThread);
	}
	
	if (g_hSendRconThread != INVALID_HANDLE_VALUE)
	{
		TerminateThread(g_hSendRconThread, 0);
		CloseHandle(g_hSendRconThread);
	}
	
	ShutdownWinsock();

	DeleteObject(g_hConsoleBackgroundBrush);
	DeleteObject(g_hFont);
	DeleteObject(g_hConsoleFont);

	OleUninitialize();
	
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
					MainWindowWriteConsole ("Please wait for the server to answer to your last request");
			}
			if (hwndCtl == gWindows.hButtonReload)
			{
				int * piKey;
				HANDLE hThread;
				EnumWindows(MainWindowEnumChildProc, 0); //Send reload message to all child windows
				
				SignalAllThreads(&g_mRefreshThreads);
				
				piKey = new int; //will be deleted as first operation in thread
				*piKey = iGetFirstUnusedMapIntKey(g_mRefreshThreads);
				
				g_mRefreshThreads.insert(std::pair<int, HANDLE>(*piKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
				hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) MainWindowRefreshThread, piKey, 0, NULL);
				CloseHandle(hThread);
			}
			if (hwndCtl == gWindows.hButtonKick) OnMainWindowKickPlayer();
			if (hwndCtl == gWindows.hButtonBanID) OnMainWindowBanID();
			if (hwndCtl == gWindows.hButtonBanIP) OnMainWindowBanIP();
			if (hwndCtl == gWindows.hButtonDPLoginProfile) OnMainWindowOpenDPLogin();
			if (hwndCtl == gWindows.hButtonUtrace) OnMainWindowOpenUtrace();
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
			
			else if (hwndCtl == gWindows.hComboServer)
			{
				int * piKey;
				HANDLE hThread;
				EnumWindows(MainWindowEnumChildProc, 0); //Send reload message to all child windows
				
				SignalAllThreads(&g_mRefreshThreads);
				
				piKey = new int; //will be deleted as first operation in thread
				*piKey = iGetFirstUnusedMapIntKey(g_mRefreshThreads);
				
				g_mRefreshThreads.insert(std::pair<int, HANDLE>(*piKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
				hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) MainWindowRefreshThread, piKey, 0, NULL);
				CloseHandle(hThread);
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
										"passwords as well as ID bans.\n"
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
		/*case IDM_SERVER_CVARS:
			CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGECVARS), hwnd, (DLGPROC)ManageCvarsDlgProc);
			break;*/
		case IDM_BANS_ENABLE:
		{
			if (GetMenuState(GetMenu(hwnd), IDM_BANS_ENABLE, MF_BYCOMMAND) == SW_SHOWNA) {
				gSettings.bRunBanThread = 0;
				CheckMenuItem(GetMenu(hwnd), IDM_BANS_ENABLE, MF_UNCHECKED);
			} else {
				gSettings.bRunBanThread = 1;
				CheckMenuItem(GetMenu(hwnd), IDM_BANS_ENABLE, MF_CHECKED);
	
				if (g_hBanThread == INVALID_HANDLE_VALUE || WaitForSingleObject(g_hBanThread, 0) != WAIT_TIMEOUT) {
					if (g_hBanThread != NULL) {
						CloseHandle(g_hBanThread);
					}
					g_hBanThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) BanThreadFunction, NULL, 0, NULL);
				}
			}
			break;
		}
		case IDM_BANS_SETPING:
			DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETPING), hwnd, (DLGPROC) SetPingDlgProc);
			break;
		case IDM_BANS_MANAGEIDS:
			if (!gWindows.hDlgManageIds)
				gWindows.hDlgManageIds = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGEIDS), hwnd, (DLGPROC) ManageIDsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgManageIds);
			break;
		case IDM_BANS_MANAGEIPS:
			if (!gWindows.hDlgManageIps)
				gWindows.hDlgManageIps = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MANAGEIPS), hwnd, (DLGPROC) ManageIPsDlgProc);
			else
				SetForegroundWindow(gWindows.hDlgManageIps);
			break;
		case IDM_HELP_DPLOGIN:
			ShellExecute(NULL, "open", "http://www.DPLogin.com\0", NULL, NULL, SW_SHOWNORMAL);
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
    if (nmh->hwndFrom == gWindows.hListPlayers)
	{
		switch (nmh->code)
		{
			case NM_CLICK:
			{
				auto iSelectedItem = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
				std::string sPlayerId = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedItem), SUBITEMS::iId);
				if (sPlayerId == "0")
					SetWindowText(gWindows.hButtonBanID, "Ban Name");
				else
					SetWindowText(gWindows.hButtonBanID, "Ban ID");

				break;
			}
			
			case NM_DBLCLK:
			{
				ShowPlayerInfo(hwnd);
				break;
			}
			
			case LVN_COLUMNCLICK:
			{
				NMLISTVIEW* pNmListview = (NMLISTVIEW*)nmh;
				ListView_SortItems (gWindows.hListPlayers, OnMainWindowListViewSort, pNmListview->iSubItem);
				FORWARD_WM_NOTIFY(hwnd, id, nmh, DefWindowProc);
				break;
			}
			
			case NM_RCLICK:
			{
				auto iSelectedItem = SendMessage(gWindows.hListPlayers, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
				if (iSelectedItem == -1)
					break;
				
				std::string sIp = ListView_CustomGetItemText(gWindows.hListPlayers, static_cast<int>(iSelectedItem), SUBITEMS::iIp);
				
				HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sIp.size() + 1);
				if (hMem == NULL) {
					return 0;
				}

				LPVOID pLocked = GlobalLock(hMem);
				if (pLocked == NULL) {
					return 0;
				}

				memcpy(pLocked, sIp.c_str(), sIp.size() + 1);
				GlobalUnlock(hMem);
				OpenClipboard(gWindows.hWinMain);
				EmptyClipboard();
				SetClipboardData(CF_TEXT, hMem);
				CloseClipboard();
				
				MainWindowWriteConsole ("IP was copied to clipboard.");
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

	MoveWindow(gWindows.hButtonBanID		 , cx - 45*iMW, 54 *iMH, 43*iMW, 12*iMH, FALSE); //Move all buttons to left / right
	MoveWindow(gWindows.hButtonBanIP		 , cx - 45*iMW, 67 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonDPLoginProfile, cx - 45*iMW, 83 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonForcejoin	 , cx - 45*iMW, 115*iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonJoin		     , cx - 45*iMW, 2  *iMH + 1, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonKick		     , cx - 45*iMW, 41 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonReload		 , cx - 45*iMW, 25 *iMH, 43*iMW, 12*iMH, FALSE);
	MoveWindow(gWindows.hButtonUtrace		 , cx - 45*iMW, 96 *iMH, 43*iMW, 12*iMH, FALSE);

#pragma warning(push)
#pragma warning(disable:26451)  // We're not computing large numbers here
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iNumber, 17*iMW);                   //num
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iName,   cx - 220*iMW);             //name
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iBuild,  18*iMW);                   //build
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iId,     25*iMW);                   //ID
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iOp,     15*iMW);                   //OP
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iIp,     47*iMW);                   //IP
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iPing,   17*iMW);                   //Ping
	ListView_SetColumnWidth(gWindows.hListPlayers, SUBITEMS::iScore,  20*iMW);                   //Score
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
	if (hwndChild == gWindows.hEditConsole) //paint the background of the console white
	{
		SetTextColor(hdc, RGB(0, 0, 0) );
		SetBkColor  (hdc, RGB(255, 255, 255) );
		return g_hConsoleBackgroundBrush;
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
		
		default:
			return DefWindowProc (hwnd, message, wParam, lParam);
	}
}

//}-------------------------------------------------------------------------------------------------
// Callback Set Ping Dialog                                                                        |
//{-------------------------------------------------------------------------------------------------

BOOL OnSetPingInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	std::string sMaxPing = std::to_string(gSettings.iMaxPingMsecs);
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
			gSettings.iMaxPingMsecs = atoi(maxPingBuffer.data());
			
			if (gSettings.iMaxPingMsecs == 0)
				CheckMenuItem(GetMenu(gWindows.hWinMain), IDM_BANS_SETPING, MF_UNCHECKED);
			else
				CheckMenuItem(GetMenu(gWindows.hWinMain), IDM_BANS_SETPING, MF_CHECKED);

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
	
	sBuffer.assign(std::to_string(gSettings.iBanCheckDelaySecs));
	SetDlgItemText(hwnd, IDC_PS_EDITBANINTERVAL, sBuffer.c_str());
	
	sBuffer.assign(std::to_string(gSettings.iAutoReloadDelaySecs));
	SetDlgItemText(hwnd, IDC_PS_EDITAUTORELOAD, sBuffer.c_str());
	if (gSettings.bRunAutoReloadThread)
		CheckDlgButton(hwnd, IDC_PS_CHECKAUTORELOAD, BST_CHECKED);
	else
		EnableWindow(GetDlgItem(hwnd, IDC_PS_EDITAUTORELOAD), FALSE);

	sBuffer.assign(std::to_string(gSettings.iMaxConsoleLineCount));
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
	case IDC_PS_CHECKAUTORELOAD:
		{
			if (codeNotify == BN_CLICKED)
			{
				if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKAUTORELOAD) == BST_CHECKED)
					EnableWindow(GetDlgItem(hwnd, IDC_PS_EDITAUTORELOAD), true);
				else
					EnableWindow(GetDlgItem(hwnd, IDC_PS_EDITAUTORELOAD), false);
			}					
			break;
		}
				
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
			
			auto iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_PS_EDITBANINTERVAL)) + 1;
			iBufferSize = max(iBufferSize, GetWindowTextLength(GetDlgItem(hwnd, IDC_PS_EDITAUTORELOAD)) + 1);
			iBufferSize = max(iBufferSize, GetWindowTextLength(GetDlgItem(hwnd, IDC_PS_EDITLINECOUNT)) + 1);
			std::vector<char> buffer(iBufferSize);
			
			GetDlgItemText(hwnd, IDC_PS_EDITBANINTERVAL, buffer.data(), static_cast<int>(buffer.size()));
			gSettings.iBanCheckDelaySecs = atoi (buffer.data());
			
			GetDlgItemText(hwnd, IDC_PS_EDITAUTORELOAD, buffer.data(), static_cast<int>(buffer.size()));
			gSettings.iAutoReloadDelaySecs = atoi (buffer.data());
				
			if (IsDlgButtonChecked(hwnd, IDC_PS_CHECKAUTORELOAD) == BST_CHECKED)
			{
				gSettings.bRunAutoReloadThread = 1;
				
				if (g_hAutoReloadThread == INVALID_HANDLE_VALUE || WaitForSingleObject(g_hAutoReloadThread, 0) != WAIT_TIMEOUT)
				{
					if(g_hAutoReloadThread != NULL)
						CloseHandle(g_hAutoReloadThread);

					g_hAutoReloadThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) AutoReloadThreadFunction, NULL, 0, NULL);
				}
			}
			else
			{
				gSettings.bRunAutoReloadThread = 0;
				CloseHandle(g_hAutoReloadThread);
				g_hAutoReloadThread = INVALID_HANDLE_VALUE;
			}
			
			
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
	SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
		return;

	std::string sAnswer;
	
	iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, "sv maplist", &sAnswer,
							g_vSavedServers[selectedServerIndex].sRconPassword, 0, gSettings.fTimeoutSecs);

	std::string::const_iterator start = sAnswer.begin();
	std::string::const_iterator end = sAnswer.end();
	std::regex rx ("^\\d+ (.*?)$");
	std::smatch MatchResults;
	while (std::regex_search(start, end, MatchResults, rx))
	{
		std::string sMap (MatchResults[1]);
		SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM) sMap.c_str());
		start = MatchResults[0].second;
	}
}

BOOL OnManageRotationInitDialog(HWND hwnd, HWND hwndFocux, LPARAM lParam)
{
	std::string sAnswer;
	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);

	LoadRotationToListbox(GetDlgItem(hwnd, IDC_MROT_LIST));

	if (selectedServerIndex == CB_ERR)
		return TRUE;

	iVarContentFromName(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, g_vSavedServers[selectedServerIndex].sRconPassword,
						"rot_file", &sAnswer, 0, gSettings.fTimeoutSecs);

	SetDlgItemText(hwnd, IDC_MROT_EDITFILE, sAnswer.c_str());

	std::string sMapshot;
	std::wstring sWideMapshot;
	RECT rc;
	
	if (not GetPb2InstallPath(&sMapshot))
		return TRUE;
	
	GetClientRect(GetDlgItem(hwnd, IDC_MROT_MAPSHOT), &rc);
	
	sMapshot.append("\\pball\\pics\\mapshots\\-no-preview-.jpg");
	sWideMapshot = std::wstring(sMapshot.begin(), sMapshot.end());
	
	g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());
	g_pMapshotBitmapResized = CreateResizedBitmapClone(g_pMapshotBitmap.get(),
								rc.right - rc.left, rc.bottom - rc.top);

	return TRUE;
}


void OnManageRotationPaint(HWND hwnd)
{
	HDC hdc;
	PAINTSTRUCT ps;
	
	hdc = BeginPaint(GetDlgItem(hwnd, IDC_MROT_MAPSHOT), &ps);
	
	FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
	
	if (g_pMapshotBitmapResized)
	{
		Gdiplus::Graphics graphics(hdc);
		graphics.DrawImage(g_pMapshotBitmapResized.get(), 0, 0);
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
		auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
		if (selectedServerIndex == CB_ERR)
			return std::string();

		std::string sMsg{ "sv rotation " };
		sMsg += subcommand;

		std::string sAnswer;
		iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, sMsg, &sAnswer, g_vSavedServers[selectedServerIndex].sRconPassword, 0, gSettings.fTimeoutSecs);
		LoadRotationToListbox(GetDlgItem(hwnd, IDC_MROT_LIST));

		return sAnswer;
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
				std::string sContent ("An error occured. The server answered: ");
				sContent.append(sAnswer);
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
				RECT rc;
				std::string sMapshot;
				std::wstring sWideMapshot;
				
				if (not GetPb2InstallPath(&sMapshot))
					return;
				
				GetClientRect(GetDlgItem(hwnd, IDC_MROT_MAPSHOT), &rc);
				
				sMapshot.append("\\pball\\pics\\mapshots\\");
				int iBufferSize = GetWindowTextLength(GetDlgItem(hwnd, IDC_MROT_EDITMAP)) + 1;
				std::vector<char> mapnameBuffer(iBufferSize);
				GetDlgItemText(hwnd, IDC_MROT_EDITMAP, mapnameBuffer.data(), iBufferSize);
				sMapshot.append(mapnameBuffer.data());
				sMapshot.append(".jpg");
				
				DWORD dwAttributes = GetFileAttributes(sMapshot.c_str());
				if (dwAttributes != INVALID_FILE_ATTRIBUTES && !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					sWideMapshot = std::wstring(sMapshot.begin(), sMapshot.end());
					
					g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());
					g_pMapshotBitmapResized = CreateResizedBitmapClone(g_pMapshotBitmap.get(),
												rc.right - rc.left, rc.bottom - rc.top);
												
					RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_INVALIDATE);
				}
				else
				{
					GetPb2InstallPath(&sMapshot);
					sMapshot.append("\\pball\\pics\\mapshots\\-no-preview-.jpg");
					sWideMapshot = std::wstring(sMapshot.begin(), sMapshot.end());
					
					g_pMapshotBitmap = std::make_unique<Gdiplus::Bitmap>(sWideMapshot.c_str());
					g_pMapshotBitmapResized = CreateResizedBitmapClone(g_pMapshotBitmap.get(),
												rc.right - rc.left, rc.bottom - rc.top);
					
					RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_INVALIDATE);
				}
			}
		}
	}
}

void OnManageRotationReloadContent(HWND hwnd)
{
	LoadRotationToListbox(GetDlgItem(hwnd, IDC_MROT_LIST));

	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
		return;

	std::string sRotationFile;
	iVarContentFromName(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, g_vSavedServers[selectedServerIndex].sRconPassword, "rot_file", &sRotationFile, 0, gSettings.fTimeoutSecs);

	SetDlgItemText(hwnd, IDC_MROT_EDITFILE, sRotationFile.c_str());
	return;
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
    
    if (Msg == WM_RELOADCONTENT) //WM_RELOADCONTENT is not static --> has to be checked with "if".
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
	int iKey = static_cast<LoadServersArgs *>(lpArgumentStruct)->iKey;
	HWND hwndListbox = static_cast<LoadServersArgs *>(lpArgumentStruct)->hwnd;
	delete static_cast<LoadServersArgs *>(lpArgumentStruct);
	
	bool bExit = false;
	
	MainWindowWriteConsole("Loading servers, this may take a short time...");
	
	std::string sServerlist ("");
	std::string sUserAgent = "Digital Paint: Paintball 2 RCON Panel V" + std::to_string(AutoVersion::MAJOR)
						+ "." + std::to_string(AutoVersion::MINOR) + "." + std::to_string(AutoVersion::BUILD);
	HINTERNET hInternet = InternetOpen(sUserAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	HINTERNET hFile = InternetOpenUrl(hInternet, gSettings.sServerlistAddress.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
	std::vector<char> buffer(MTU);

	while (true)
	{
		long unsigned int iBytesRead = 0;
		InternetReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &iBytesRead);

		if (iBytesRead == 0) break;
		buffer[iBytesRead] = '\0';
		sServerlist.append(buffer.data());
	}
	InternetCloseHandle(hFile);
	InternetCloseHandle(hInternet);
	
	// TODO: RAII class for socket
	SOCKET hSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (hSocket == INVALID_SOCKET)
		return;

	std::smatch MatchResults;
	std::regex rx ("(\\d+\\.\\d+\\.\\d+\\.\\d+):(\\d{2,5})");

	std::string::const_iterator start = sServerlist.begin();
	std::string::const_iterator end   = sServerlist.end();

	while (std::regex_search(start, end, MatchResults, rx)) //add all servers who answer to the listbox
	{
		std::string sIp(MatchResults[1]);
		std::string sPort(MatchResults[2]);
		start = MatchResults[0].second;

		Server tempserver(sIp, std::stoi(sPort));
		tempserver.retrieveAndSetHostname(hSocket, gSettings.fAllServersTimeoutSecs);
		
		try
		{
			if (WaitForSingleObject(g_mLoadServersThreads.at(iKey), 0) == WAIT_OBJECT_0)
			{
				bExit = true;
				break;
			}
		}
		catch (const std::out_of_range&)
		{
			closesocket(hSocket);
			return;
		}
		
		if (tempserver.sHostname.compare("COULD NOT GET HOSTNAME") != 0)
		{
			g_vAllServers.push_back(tempserver);
			auto index = SendMessage(hwndListbox, LB_ADDSTRING, 0, (LPARAM)tempserver.sHostname.c_str());
			SendMessage(hwndListbox, LB_SETITEMDATA, index, g_vAllServers.size() - 1);
		}
	}

	CloseHandle(g_mLoadServersThreads.at(iKey)); //Delete the Event
	closesocket(hSocket);
	g_mLoadServersThreads.erase(g_mLoadServersThreads.find(iKey));
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
		auto index = SendMessage(gWindows.hComboServer, CB_ADDSTRING, 0, (LPARAM) g_vSavedServers[i].sHostname.c_str());
		SendMessage(gWindows.hComboServer, CB_SETITEMDATA, index, i);
	}
	SendMessage(gWindows.hComboServer, CB_SETCURSEL, 0, 0);
	
	SignalAllThreads(&g_mLoadServersThreads);
	MainWindowWriteConsole("Aborting.");
	
	g_vAllServers.clear();
}

BOOL OnManageServersInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
	for(unsigned int i = 0; i < g_vSavedServers.size(); i++) { //Add servers to the lists
		auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING, 0, (LPARAM)g_vSavedServers[i].sHostname.c_str());
		SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETITEMDATA, index, i);
	}
	
	LoadServersArgs * lpArgumentsStruct = new LoadServersArgs;
	
	lpArgumentsStruct->iKey = iGetFirstUnusedMapIntKey(g_mLoadServersThreads);
	lpArgumentsStruct->hwnd = GetDlgItem(hwnd, IDC_DM_LISTLEFT);
	
	g_mLoadServersThreads.insert(std::pair<int, HANDLE>(lpArgumentsStruct->iKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
	
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) &LoadServersToListbox, lpArgumentsStruct, 0, NULL);
	if (hThread == NULL) {
		MessageBox(NULL, "Failed to create thread to load servers", "Error", MB_OK | MB_ICONERROR);
	}
	else
	{
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
			int iBufferSize = 16; //"255.255.255.255\0" at least
			int iRconPWLength = GetWindowTextLength(GetDlgItem(hwnd, IDC_DM_EDITPW)) + 1;
			iBufferSize = (iRconPWLength > iBufferSize) ? iRconPWLength : iBufferSize;
			std::vector<char> buffer(iBufferSize);

			Server tempserver;

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_GETTEXT, iBufferSize, (LPARAM) buffer.data());
			tempserver.iPort = atoi(buffer.data());

			DWORD dwIP;
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_GETADDRESS, 0, (LPARAM) &dwIP);
			tempserver.sIp = std::to_string(FIRST_IPADDRESS(dwIP)) + "." +
							 std::to_string(SECOND_IPADDRESS(dwIP)) + "." +
							 std::to_string(THIRD_IPADDRESS(dwIP)) + "." +
							 std::to_string(FOURTH_IPADDRESS(dwIP));

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPW), WM_GETTEXT, iBufferSize, (LPARAM) buffer.data());
			tempserver.sRconPassword.assign(buffer.data());
			
			tempserver.retrieveAndSetHostname(0, gSettings.fAllServersTimeoutSecs);

			g_vSavedServers.push_back(tempserver);

			auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING,
									 0, (LPARAM) tempserver.sHostname.c_str());
			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_SETITEMDATA,
						index, g_vSavedServers.size() - 1);
			return;
		}
		case IDC_DM_BUTTONREMOVE:
		{
			//Get position of selected server in g_vSavedServers
			auto iCurSel = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETCURSEL, 0, 0);
			if (iCurSel == LB_ERR) return;
			auto iServerIndex = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_GETITEMDATA, iCurSel, 0);
			if (iServerIndex == LB_ERR) return;
			g_vSavedServers.erase(g_vSavedServers.begin() + iServerIndex);

			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_RESETCONTENT, 0, 0); //Reload right listbox
			for(unsigned int i = 0; i < g_vSavedServers.size(); i++) {
				auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING,
										 0, (LPARAM)g_vSavedServers[i].sHostname.c_str());
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
			g_vSavedServers[iRet].iPort = atoi(buffer.data());

			DWORD dwIP = 0;
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_GETADDRESS, 0, (LPARAM) &dwIP);
			snprintf(buffer.data(), iBufferSize, "%lu.%lu.%lu.%lu", FIRST_IPADDRESS(dwIP), SECOND_IPADDRESS(dwIP),
					THIRD_IPADDRESS(dwIP), FOURTH_IPADDRESS(dwIP));
			g_vSavedServers[iRet].sIp.assign(buffer.data());

			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPW), WM_GETTEXT, iBufferSize, (LPARAM)buffer.data());
			g_vSavedServers[iRet].sRconPassword = buffer.data();

			g_vSavedServers[iRet].retrieveAndSetHostname(0, gSettings.fAllServersTimeoutSecs);

			SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_RESETCONTENT, 0, 0); //reload right listbox
			for(size_t i = 0; i < g_vSavedServers.size(); i++)
			{
				auto index = SendMessage(GetDlgItem(hwnd, IDC_DM_LISTRIGHT), LB_ADDSTRING, 0, (LPARAM)g_vSavedServers[i].sHostname.c_str());
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

			strcpy (szIpBuffer, g_vAllServers[selectedServerIndex].sIp.c_str());

			BYTE b0, b1, b2, b3;
			SplitIpAddressToBytes(szIpBuffer, &b0, &b1, &b2, &b3);

#pragma warning (suppress : 26451)
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_SETTEXT,
						0, (LPARAM) (std::to_string(g_vAllServers[selectedServerIndex].iPort)).c_str());

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
			strcpy (szIpBuffer, g_vSavedServers[selectedSavedServerIndex].sIp.c_str());
			BYTE b0, b1, b2, b3;
			SplitIpAddressToBytes(szIpBuffer, &b0, &b1, &b2, &b3);

#pragma warning (suppress : 26451)
			SendMessage(GetDlgItem(hwnd, IDC_DM_IP), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPORT), WM_SETTEXT,
						0, (LPARAM) (std::to_string(g_vSavedServers[selectedSavedServerIndex].iPort)).c_str());
			SendMessage(GetDlgItem(hwnd, IDC_DM_EDITPW), WM_SETTEXT,
						0, (LPARAM) g_vSavedServers[selectedSavedServerIndex].sRconPassword.c_str());
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
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[r] Red\0");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[b] Blue\0");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[p] Purple\0");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[y] Yellow\0");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[a] Automatic\0");
	SendMessage(GetDlgItem(hwnd, IDC_FJ_COLORLIST), LB_ADDSTRING, 0, (LPARAM) "[o] Observer\0");
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
	for (size_t i = 0; i<g_vBannedPlayers.size(); i++) {
		auto index = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_ADDSTRING, 0, (LPARAM) g_vBannedPlayers[i].sText.c_str());
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
		for (size_t i = 0; i < g_vBannedPlayers.size(); i++) {
			auto index = SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_ADDSTRING, 0, (LPARAM)g_vBannedPlayers[i].sText.c_str());
			SendMessage(GetDlgItem(hwnd, IDC_MIDS_LIST), LB_SETITEMDATA, index, i);
		}
	};

	switch(id)
	{
		case IDC_MIDS_BUTTONADD:
		{
			Ban tempban;
			std::vector<char> buffer(static_cast<size_t>(GetWindowTextLength(GetDlgItem(hwnd, IDC_MIDS_EDIT))) + 1);

			SendMessage (GetDlgItem(hwnd, IDC_MIDS_EDIT), WM_GETTEXT, buffer.size(), (LPARAM) buffer.data()); //set text
			tempban.sText = buffer.data();

			if (IsDlgButtonChecked(hwnd, IDC_MIDS_RADIOID)) //Set ID / NAME flag
			{
				std::vector<char> compareBuffer(buffer.size());
				sprintf(compareBuffer.data(), "%d", atoi(buffer.data())); //check if it's a valid ID
				
				if (strcmp (buffer.data(), compareBuffer.data()) != 0) {
					MessageBox(gWindows.hWinMain, "The ID you have entered is not valid.", "Error: Invalid ID", MB_OK | MB_ICONERROR);
					return ;
				}
				tempban.tType = Ban::Type::ID;
			}
			else {
				assert(IsDlgButtonChecked(hwnd, IDC_MIDS_RADIONAME));
				tempban.tType = Ban::Type::NAME;
			}

			g_vBannedPlayers.push_back(tempban);

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

			g_vBannedPlayers.erase(g_vBannedPlayers.begin() + selectedPlayerIndex); //delete the ban

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
			g_vBannedPlayers[selectedPlayerIndex].sText = buffer.data();

			if (IsDlgButtonChecked(hwnd, IDC_MIDS_RADIOID))
			{
				std::vector<char> comparisonBuffer(buffer.size());
				sprintf(comparisonBuffer.data(), "%d", atoi(buffer.data())); //check if it's a valid ID
				if (strcmp (buffer.data(), comparisonBuffer.data()) != 0) {
					MessageBoxA(gWindows.hWinMain, "The ID you have entered is not valid.", "Error: Invalid ID", MB_OK | MB_ICONERROR);
					return;
				}
				g_vBannedPlayers[selectedPlayerIndex].tType = Ban::Type::ID;
			}
			else {
				assert(IsDlgButtonChecked(hwnd, IDC_MIDS_RADIONAME));
				g_vBannedPlayers[selectedPlayerIndex].tType = Ban::Type::NAME;
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
				SendMessage(GetDlgItem(hwnd, IDC_MIDS_EDIT), WM_SETTEXT,  0, (LPARAM) g_vBannedPlayers[selectedPlayerIndex].sText.c_str());
				
				if (g_vBannedPlayers[selectedPlayerIndex].tType == Ban::Type::ID)
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
	SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
	auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
	if (selectedServerIndex == CB_ERR)
		return;

	std::string sAnswer;

	iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, "sv listip", &sAnswer,
							g_vSavedServers[selectedServerIndex].sRconPassword, 0, gSettings.fTimeoutSecs);

	std::string::const_iterator start = sAnswer.begin();
	std::string::const_iterator end = sAnswer.end();
	std::regex rx ("\\s*\\d+\\.\\s*\\d+\\.\\s*\\d+\\.\\s*\\d+");
	std::smatch MatchResults;
	while (std::regex_search(start, end, MatchResults, rx))
	{
		std::string sIp (MatchResults[0]);
		SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM) sIp.c_str());
		start = MatchResults[0].second;
	}
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

	switch(id)
	{
		case IDC_MIPS_BUTTONADD:
		{
			auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
			if (selectedServerIndex == CB_ERR)
				return;
	
			DWORD dwIP = 0;
			SendMessage(GetDlgItem(hwnd, IDC_MIPS_IPCONTROL), IPM_GETADDRESS, 0, (LPARAM) &dwIP);
			std::string sMsg ("sv addip ");
			sMsg += std::to_string(FIRST_IPADDRESS(dwIP)) + "." + std::to_string(SECOND_IPADDRESS(dwIP))
					+ "." + std::to_string(THIRD_IPADDRESS(dwIP)) + "." + std::to_string(FOURTH_IPADDRESS(dwIP));

			std::string sAnswer;
			iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, sMsg,
									&sAnswer, g_vSavedServers[selectedServerIndex].sRconPassword, 0, gSettings.fTimeoutSecs);

			LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_MIPS_LIST));
			return;
		}
	
		case IDC_MIPS_BUTTONREMOVE:
		{
			auto selectedServerIndex = SendMessage(gWindows.hComboServer, CB_GETITEMDATA, SendMessage(gWindows.hComboServer, CB_GETCURSEL, 0, 0), 0);
			if (selectedServerIndex == CB_ERR)
				return;
	
			std::string sMsg ("sv removeip ");

			DWORD dwIP = 0;
			SendMessage(GetDlgItem(hwnd, IDC_MIPS_IPCONTROL), IPM_GETADDRESS, 0, (LPARAM) &dwIP);
			sMsg.append(std::to_string(FIRST_IPADDRESS(dwIP)) + "." + std::to_string(SECOND_IPADDRESS(dwIP))
					+ "." + std::to_string(THIRD_IPADDRESS(dwIP)) + "." + std::to_string(FOURTH_IPADDRESS(dwIP)));

			std::string sAnswer;
			iSendMessageToServer(g_vSavedServers[selectedServerIndex].sIp, g_vSavedServers[selectedServerIndex].iPort, sMsg,
									&sAnswer, g_vSavedServers[selectedServerIndex].sRconPassword, 0, gSettings.fTimeoutSecs);

			LoadBannedIPsToListbox(GetDlgItem(hwnd, IDC_MIPS_LIST));
			return;
		}
	
		case IDC_MIPS_BUTTONOK:
		{
			SendMessage(hwnd, WM_CLOSE, 0, 0); //Make sure to clear the handle so a new one is opened next time
			return;
		}
		
		case IDC_MIPS_LIST:
		{
			if (codeNotify == LBN_SELCHANGE) // set content of ip control
			{
				char szBuffer[16];
				BYTE b0, b1, b2, b3;
				SendMessage(GetDlgItem(hwnd, IDC_MIPS_LIST), LB_GETTEXT,
							SendMessage(GetDlgItem(hwnd, IDC_MIPS_LIST), LB_GETCURSEL, 0, 0),
							(LPARAM) szBuffer);
				SplitIpAddressToBytes(szBuffer, &b0, &b1, &b2, &b3);
#pragma warning (suppress : 26451)
				SendMessage(GetDlgItem(hwnd, IDC_MIPS_IPCONTROL), IPM_SETADDRESS, 0, MAKEIPADDRESS(b0, b1, b2, b3));
			}
			return;
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

	if (Msg == WM_RELOADCONTENT) //has to be checked in an if statement because WM_RELOADCONTENT is dynamic
	{
		OnManageIPsReloadContent(hWndDlg);
		return TRUE;
	}
	
	return FALSE;
}

//}-------------------------------------------------------------------------------------------------
// Other functions                                                                                 |
//{-------------------------------------------------------------------------------------------------

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
	int iMaxVert, iMaxHorz;
	GetScrollRange(hEdit, SB_VERT, &iMaxHorz, &iMaxVert);
	SetScrollPos(hEdit, SB_VERT, iMaxVert, TRUE);
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

int GetPb2InstallPath(std::string * sPath)
{
	HKEY key;
	char szPbPath[MAX_PATH] = { 0 };
	long unsigned int iPathSize = sizeof(szPbPath) - 1;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Digital Paint\\Paintball2", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
	{
		if (RegQueryValueEx(key, "INSTDIR", NULL, NULL, (LPBYTE) &szPbPath, &iPathSize) == ERROR_SUCCESS)
		{
			szPbPath[iPathSize] = '\0';
			sPath->assign(szPbPath);
			RegCloseKey(key);
			return 1;
		}
		RegCloseKey(key);
	}
	else if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Digital Paint\\Paintball2\0", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
	{
		if (RegQueryValueEx(key, "INSTDIR", NULL, NULL, (LPBYTE) &szPbPath, &iPathSize) == ERROR_SUCCESS)
		{
			szPbPath[iPathSize] = '\0';
			sPath->assign(szPbPath);
			RegCloseKey(key);
			return 1;
		}
		RegCloseKey(key);
	}
	return 0;
}

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

std::string ListView_CustomGetItemText(HWND hListView, int iItemIndex, int iSubItem) {
	std::vector<char> buffer(MAX_PATH / 2);

	LVITEM LvItem = { 0 };
	LvItem.iSubItem = iSubItem;
	LvItem.pszText = buffer.data();

	LRESULT iRet;
	do {
		buffer.resize(buffer.size() * 2);
		LvItem.pszText = buffer.data();
		LvItem.cchTextMax = static_cast<int>(buffer.size());
		iRet = SendMessage(gWindows.hListPlayers, LVM_GETITEMTEXT, iItemIndex, (LPARAM)&LvItem);
	} while (iRet >= static_cast<LRESULT>(buffer.size() - 1));

	return std::string{ buffer.data() };
}

void StartServerbrowser(void)
{
	std::string sSbPath;
	if (GetPb2InstallPath(&sSbPath))
	{
		sSbPath.append("\\serverbrowser.exe");
		auto iRet = (INT_PTR) ShellExecute(0, "open", sSbPath.c_str(), "", 0, 1); //start it
		if (iRet <= 32)
		{
			MainWindowWriteConsole("Error while starting:\r\n" + sSbPath + "\r\nShellExecute returned: " + std::to_string(iRet));
		}
	}
	else
	{
		MainWindowWriteConsole("Could not find the path of you DP:PB2 install directory in the registry.");
	}
}

void BanThreadFunction()  // function that's started as thread to regularly check servers for banned players / players with a too high ping and to kick them
{
	std::vector <Server> servers;
	std::vector <Ban> bans;
	std::vector <Player> players;
	std::string sMsgBuffer;
	std::string sReturnBuffer;

	while (true)
	{
		if (!gSettings.bRunBanThread)
		{
            g_hBanThread = INVALID_HANDLE_VALUE;
            return;
		}

		// todo: mutex -- currently race condition
        servers = g_vSavedServers;
        bans = g_vBannedPlayers;

        for (const auto& server : servers)
		{
            iPlayerStructVectorFromAddress(server.sIp, server.iPort, server.sRconPassword, &players, 0, gSettings.fTimeoutSecs);
			
            for (const auto& player : players)
			{
				if (gSettings.iMaxPingMsecs != 0 && player.iPing > gSettings.iMaxPingMsecs)
				{
					sMsgBuffer.assign("kick ");
					sMsgBuffer.append(std::to_string(player.iNumber));
						
					iSendMessageToServer(server.sIp, server.iPort, sMsgBuffer, &sReturnBuffer,
										server.sRconPassword, 0, gSettings.fTimeoutSecs);
					
					MainWindowWriteConsole("Player " + player.sName + " on server " + server.sHostname + "had a too high ping.");
					MainWindowWriteConsole("Server answered to kick message: " + sReturnBuffer);
				}

                for (auto& ban : bans)
				{
                    if ((ban.tType == Ban::Type::NAME && strcasecmp (player.sName.c_str(), ban.sText.c_str()) == 0)
						|| (ban.tType == Ban::Type::ID && player.iId == std::stoi(ban.sText)))
					{
						sMsgBuffer.assign("kick ");
						sMsgBuffer.append(std::to_string(player.iNumber));
							
						iSendMessageToServer(server.sIp, server.iPort,
											sMsgBuffer, &sReturnBuffer,
											server.sRconPassword, 0, gSettings.fTimeoutSecs);
						
						MainWindowWriteConsole("Found banned player " + player.sName + " on server " + server.sHostname);
						MainWindowWriteConsole("Server answered to kick message: " + sReturnBuffer);
					}
				}
			}
		}
		MainWindowWriteConsole("Checked servers for banned players.");
		
		int iBanThreadTimeWaited = 0;
		while (iBanThreadTimeWaited <= gSettings.iBanCheckDelaySecs  * 1000)
		{
			Sleep(100);
			iBanThreadTimeWaited += 100;
			if (!gSettings.bRunBanThread)
			{
				g_hBanThread = INVALID_HANDLE_VALUE;
				return;
			}
		}
	}
}

void AutoReloadThreadFunction(void)
{
	while (true)
	{
		if (!gSettings.bRunAutoReloadThread)
		{
			g_hAutoReloadThread = INVALID_HANDLE_VALUE;
			return;
		}
			
		Sleep(100);
		g_iAutoReloadThreadTimeWaitedMsecs += 100;
		
		if (g_iAutoReloadThreadTimeWaitedMsecs >= gSettings.iAutoReloadDelaySecs * 1000)
		{
			int * piKey;
			HANDLE hThread;
			
			SignalAllThreads(&g_mRefreshThreads);
			
			piKey = new int; //will be deleted as first operation in thread
			*piKey = iGetFirstUnusedMapIntKey(g_mRefreshThreads);
			
			g_mRefreshThreads.insert(std::pair<int, HANDLE>(*piKey, CreateEvent(NULL, TRUE, FALSE, NULL)));
			hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) MainWindowRefreshThread, piKey, 0, NULL);
			if (hThread == NULL) {
				MessageBox(NULL, "Failed to create thread to refresh main window", "Error", MB_OK | MB_ICONERROR);
			}
			else
			{
				CloseHandle(hThread);
			}
		}
	}
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

	GetPrivateProfileString("general", "timeout", std::to_string(DEFAULTSETTINGS::fTimeoutSecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.fTimeoutSecs = (float) atof(szReadBuffer);
	
	GetPrivateProfileString("general", "timeoutForNonRconServers", std::to_string(DEFAULTSETTINGS::fAllServersTimeoutSecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.fAllServersTimeoutSecs = (float) atof(szReadBuffer);
	
	GetPrivateProfileString("general", "maxConsoleLineCount", std::to_string(DEFAULTSETTINGS::iMaxConsoleLineCount).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.iMaxConsoleLineCount = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "limitConsoleLineCount", std::to_string(DEFAULTSETTINGS::iMaxConsoleLineCount).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bLimitConsoleLineCount = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "colorPlayers", std::to_string(DEFAULTSETTINGS::bColorPlayers).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bColorPlayers = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "colorPings", std::to_string(DEFAULTSETTINGS::bColorPings).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bColorPings = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "disableConsole", std::to_string(DEFAULTSETTINGS::bDisableConsole).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bDisableConsole = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "autoReloadDelay", std::to_string(DEFAULTSETTINGS::iAutoReloadDelaySecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.iAutoReloadDelaySecs = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "runAutoReloadThread", std::to_string(DEFAULTSETTINGS::bRunAutoReloadThread).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bRunAutoReloadThread = atoi(szReadBuffer);
	
	GetPrivateProfileString("general", "serverlistAddress", DEFAULTSETTINGS::sServerlistAddress.c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.sServerlistAddress = szReadBuffer;
	
	if (gSettings.bRunAutoReloadThread && WaitForSingleObject(g_hAutoReloadThread, 0) != WAIT_TIMEOUT)
	{
		CloseHandle(g_hAutoReloadThread);
		g_hAutoReloadThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) AutoReloadThreadFunction, NULL, 0, NULL);
	}
	
	GetPrivateProfileString("bans", "runBanThread", std::to_string(DEFAULTSETTINGS::bRunBanThread).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.bRunBanThread  = atoi(szReadBuffer);
	if (gSettings.bRunBanThread)
	{
		CheckMenuItem(GetMenu(gWindows.hWinMain), IDM_BANS_ENABLE, MF_CHECKED);
		if (WaitForSingleObject(g_hBanThread, 0) != WAIT_TIMEOUT)
		{
			CloseHandle(g_hBanThread);
			g_hBanThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) BanThreadFunction, NULL, 0, NULL);
		}
	}
	GetPrivateProfileString("bans", "delay", std::to_string(DEFAULTSETTINGS::iBanCheckDelaySecs).c_str(), szReadBuffer, sizeof(szReadBuffer), path.c_str());
	gSettings.iBanCheckDelaySecs = atoi(szReadBuffer);

	char szCount[10];
	GetPrivateProfileString("server", "count", "-1\0", szCount, sizeof(szCount), path.c_str());
	if (strcmp (szCount, "-1") == 0 && GetLastError() == 0x2) return -1; //File not Found
	for (int i = 0; i < atoi(szCount); i++) //load servers
	{
		char szKeyBuffer[512] = { 0 };
		char szPortBuffer[6] = { 0 };
		sprintf(szKeyBuffer, "%d", i);
		GetPrivateProfileString("ip", szKeyBuffer, "0.0.0.0\0", szReadBuffer, sizeof(szReadBuffer), path.c_str());
		GetPrivateProfileString("port", szKeyBuffer, "00000\0", szPortBuffer, 6, path.c_str());
		Server tempServer(szReadBuffer, atoi(szPortBuffer));

		tempServer.retrieveAndSetHostname(0, gSettings.fTimeoutSecs);

		GetPrivateProfileString("pw", szKeyBuffer, "\0", szReadBuffer, sizeof(szReadBuffer), path.c_str());
		tempServer.sRconPassword = szReadBuffer;

		g_vSavedServers.push_back(tempServer);
		auto index = SendMessage(gWindows.hComboServer, CB_ADDSTRING, 0, (LPARAM)tempServer.sHostname.c_str());
		SendMessage (gWindows.hComboServer, CB_SETITEMDATA, index, g_vSavedServers.size() - 1);
	}
	SendMessage(gWindows.hComboServer, CB_SETCURSEL, 0, 0);

	GetPrivateProfileString("bans", "count", "0\0", szCount, sizeof(szCount), path.c_str());
	for (int i = 0; i < atoi(szCount); i++) //load bans
	{
		Ban tempban;
		char szKeyBuffer[512];
		sprintf(szKeyBuffer, "%d", i);
		GetPrivateProfileString("bans", szKeyBuffer, "\0", szReadBuffer, sizeof(szReadBuffer), path.c_str());
		if (strcmp(szReadBuffer, "") == 0)
		{
			return -2;
		}
		tempban.sText.assign(szReadBuffer);

		sprintf(szKeyBuffer, "%dtype", i);
		GetPrivateProfileString("bans", szKeyBuffer, "-1", szReadBuffer, 6, path.c_str());
		if (strcmp(szReadBuffer, "-1") == 0)
		{
			return -2;
		}
		tempban.tType = (Ban::Type)atoi(szReadBuffer);
		g_vBannedPlayers.push_back(tempban);
	}
	return 1;
}

void SaveConfig() // Saves all servers and settings in the config file
{
	MainWindowWriteConsole("Saving configuration file...");
	auto path = ConfigLocation();

	//clear old config so servers & bans that are not used anymore are not occupying any disk space:
	WritePrivateProfileString("ip\0", NULL, NULL, path.c_str());
	WritePrivateProfileString("pw\0", NULL, NULL, path.c_str());
	WritePrivateProfileString("port\0", NULL, NULL, path.c_str());
	WritePrivateProfileString("bans\0", NULL, NULL, path.c_str());
	
	std::string sWriteBuffer = std::to_string(g_vSavedServers.size());
	if (!WritePrivateProfileString("server\0", "count\0", sWriteBuffer.c_str(), path.c_str()))
		return;

	for (unsigned int i = 0; i< g_vSavedServers.size(); i++) //write servers to it
	{
		std::string sKeyBuffer = std::to_string(i);
		std::string sPortBuffer = std::to_string(g_vSavedServers[i].iPort);
		WritePrivateProfileString("ip", sKeyBuffer.c_str(), g_vSavedServers[i].sIp.c_str(), path.c_str());
		WritePrivateProfileString("pw", sKeyBuffer.c_str(), g_vSavedServers[i].sRconPassword.c_str(), path.c_str());
		WritePrivateProfileString("port", sKeyBuffer.c_str(), sPortBuffer.c_str(), path.c_str());
	}

	sWriteBuffer = std::to_string(gSettings.fTimeoutSecs);
	WritePrivateProfileString("general\0", "timeout\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.fAllServersTimeoutSecs);
	WritePrivateProfileString("general\0", "timeoutForNonRconServers\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.iMaxConsoleLineCount);
	WritePrivateProfileString("general\0", "maxConsoleLineCount\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bLimitConsoleLineCount);
	WritePrivateProfileString("general\0", "limitConsoleLineCount\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bRunAutoReloadThread);
	WritePrivateProfileString("general\0", "runAutoReloadThread\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.iAutoReloadDelaySecs);
	WritePrivateProfileString("general\0", "autoReloadDelay\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bColorPlayers);
	WritePrivateProfileString("general\0", "colorPlayers\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bColorPings);
	WritePrivateProfileString("general\0", "colorPings\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.bDisableConsole);
	WritePrivateProfileString("general\0", "disableConsole\0", sWriteBuffer.c_str(), path.c_str());
	
	WritePrivateProfileString("general\0", "serverlistAddress\0", gSettings.sServerlistAddress.c_str(), path.c_str());
	
	sWriteBuffer = std::to_string(gSettings.bRunBanThread);
	WritePrivateProfileString("bans\0", "runBanThread\0", sWriteBuffer.c_str(), path.c_str());
	sWriteBuffer = std::to_string(gSettings.iBanCheckDelaySecs);
	WritePrivateProfileString("bans\0", "delay\0", sWriteBuffer.c_str(), path.c_str());

	std::string sKeyBuffer = std::to_string(g_vBannedPlayers.size());
	WritePrivateProfileString("bans", "count", sKeyBuffer.c_str(), path.c_str());

	for (unsigned int i = 0; i< g_vBannedPlayers.size(); i++)
	{
		sKeyBuffer = std::to_string(i);
		WritePrivateProfileString("bans", sKeyBuffer.c_str(), g_vBannedPlayers[i].sText.c_str(), path.c_str());

		sKeyBuffer = std::to_string(i);
		sKeyBuffer.append("type");
		sWriteBuffer = std::to_string(static_cast<int>(g_vBannedPlayers[i].tType));
		WritePrivateProfileString("bans", sKeyBuffer.c_str(), sWriteBuffer.c_str(), path.c_str());
	}
}

int iGetFirstUnusedMapIntKey (const std::map<int, HANDLE>& m)
{	
	for (int iKey = 0; iKey <= m.size() + 1; iKey++)
	{
		if (!m.contains(iKey))
			return iKey;
	}
	assert(false);
	return -1;
}

std::unique_ptr<Gdiplus::Bitmap> CreateResizedBitmapClone(Gdiplus::Bitmap *source, unsigned int width, unsigned int height)
{
    unsigned int originalHeight = source->GetHeight();
    unsigned int originalWidth = source->GetWidth();
    unsigned int newWidth = width;
    unsigned int newHeight = height;
    
    double ratio = (static_cast<double>(originalWidth)) / (static_cast<double>(originalHeight));
    if (originalWidth > originalHeight) {
	    newHeight = static_cast<unsigned int>((static_cast<double>(newWidth)) / ratio);
    } else {
	    newWidth = static_cast<unsigned int>(newHeight * ratio);
    }
    
    auto newBitmap = std::make_unique<Gdiplus::Bitmap>(newWidth, newHeight, source->GetPixelFormat());
    Gdiplus::Graphics graphics(newBitmap.get());
    graphics.DrawImage(source, 0, 0, newWidth, newHeight);
    return newBitmap;
}