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
#ifndef __RESOURCE_H_INCLUDED
#define __RESOURCE_H_INCLUDED

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDA_APP_ICON						102

#define IDM									120
#define IDM_FILE_EXIT						121
#define IDM_FILE_SETTINGS					123
#define IDM_FILE_REMOVECONFIG 				124
#define IDM_SERVER_MANAGE					125
#define IDM_SERVER_ROTATION					126
#define IDM_SERVER_CVARS					127
#define IDM_BANS_ENABLE						128
#define IDM_BANS_MANAGEIDS					129
#define IDM_BANS_MANAGEIPS					130
#define IDM_BANS_SETPING					131
#define IDM_HELP_DPLOGIN					132
#define IDM_HELP_RCONCOMMANDS				133
#define IDM_HELP_ABOUT						134
#define IDM_HELP_SERVERBROWSER				135

#define IDD_MANAGESERVERS					140
#define IDC_DM_LISTLEFT						141
#define IDC_DM_LISTRIGHT					142
#define IDC_DM_BUTTONADD					143
#define IDC_DM_BUTTONREMOVE					144
#define IDC_DM_BUTTONSAVE					145
#define IDC_DM_BUTTONOK						146
#define IDC_DM_EDITPW						147
#define IDC_DM_IP							148
#define IDC_DM_EDITPORT						149

#define IDD_FORCEJOIN						150
#define IDC_FJ_OK							151
#define IDC_FJ_CANCEL						152
#define IDC_FJ_COLORLIST					153

#define IDD_MANAGEIPS						160
#define IDC_MIPS_BUTTONOK					161
#define IDC_MIPS_LIST						162
#define IDC_MIPS_IPCONTROL					163
#define IDC_MIPS_BUTTONADD					164
#define IDC_MIPS_BUTTONREMOVE				165

#define IDD_MANAGEIDS						170
#define IDC_MIDS_BUTTONOK					171
#define IDC_MIDS_BUTTONADD					172
#define IDC_MIDS_BUTTONREMOVE				173
#define IDC_MIDS_BUTTONSAVE					174
#define IDC_MIDS_RADIOID					175
#define IDC_MIDS_RADIONAME					176
#define IDC_MIDS_LIST						177
#define IDC_MIDS_EDIT						178

#define IDD_MANAGECVARS						180
#define IDC_MCVARS_BUTTONOK					181
#define IDC_MCVARS_LISTVIEW					182

#define IDD_MANAGEROTATION					190
#define IDC_MROT_BUTTONOK					191
#define IDC_MROT_LIST						192
#define IDC_MROT_BUTTONADD					193
#define IDC_MROT_EDITMAP					194
#define IDC_MROT_BUTTONREMOVE				195
#define IDC_MROT_BUTTONCLEAR				196
#define IDC_MROT_EDITFILE					197
#define IDC_MROT_BUTTONWRITE				198
#define IDC_MROT_BUTTONREAD					199
#define IDC_MROT_MAPSHOT					200

#define IDD_PROGRAMSETTINGS					210
#define IDC_PS_TRACKOWNSERVERS				211
#define IDC_PS_STATICOWNSERVERS				212
#define IDC_PS_TRACKOTHERSERVERS			213
#define IDC_PS_STATICOTHERSERVERS			214
#define IDC_PS_EDITBANINTERVAL				215
#define IDC_PS_CHECKAUTORELOAD				216
#define IDC_PS_EDITAUTORELOAD				217
#define IDC_PS_CHECKLINECOUNT				218
#define IDC_PS_EDITLINECOUNT				219
#define IDC_PS_CHECKCOLORPLAYERS			220
#define IDC_PS_CHECKCOLORPINGS				221
#define IDC_PS_CHECKDISABLECONSOLE			222
#define IDC_PS_BUTTONOK						223
#define IDC_PS_BUTTONCANCEL					224

#define IDD_RCONCOMMANDS					230

#define IDD_SETPING							240
#define IDC_SP_EDIT							241
#define IDC_SP_BUTTONCANCEL					242
#define IDC_SP_BUTTONOK						243

#endif // __RESOURCE_H_INCLUDED
