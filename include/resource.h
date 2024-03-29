#ifndef __RESOURCE_H_INCLUDED
#define __RESOURCE_H_INCLUDED

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDA_APP_ICON						102

#define IDM									120
#define IDM_FILE_EXIT						121
#define IDM_FILE_SETTINGS					123
#define IDM_SERVER_EDITSERVERS				125
#define IDM_SERVER_ROTATION					126
#define IDM_SERVER_CVARS					127
#define IDM_SERVER_BANNEDIPS				128
#define IDM_AUTOKICK_ENABLE					129
#define IDM_AUTOKICK_EDITENTRIES			130
#define IDM_AUTOKICK_SETPING				131
#define IDM_HELP_DPLOGIN					132
#define IDM_HELP_RCONCOMMANDS				133
#define IDM_HELP_ABOUT						134
#define IDM_HELP_SERVERBROWSER				135

#define IDD_SERVERS							140
#define IDC_SERVERS_LISTLEFT				141
#define IDC_SERVERS_LISTRIGHT				142
#define IDC_SERVERS_BUTTONADD				143
#define IDC_SERVERS_BUTTONREMOVE			144
#define IDC_SERVERS_BUTTONSAVE				145
#define IDC_SERVERS_BUTTONOK				146
#define IDC_SERVERS_EDITPW					147
#define IDC_SERVERS_EDITIP					148
#define IDC_SERVERS_EDITPORT				149

#define IDD_FORCEJOIN						150
#define IDC_FJ_BUTTONRED					151
#define IDC_FJ_BUTTONBLUE					152
#define IDC_FJ_BUTTONPURPLE					153
#define IDC_FJ_BUTTONYELLOW					154
#define IDC_FJ_BUTTONAUTO					155
#define IDC_FJ_BUTTONOBSERVER				156

#define IDD_BANNEDIPS						160
#define IDC_IPS_LIST						162
#define IDC_IPS_IPCONTROL					163
#define IDC_IPS_BUTTONADD					164
#define IDC_IPS_BUTTONREMOVE				165

#define IDD_AUTOKICK_ENTRIES				170
#define IDC_AUTOKICK_BUTTONADD				171
#define IDC_AUTOKICK_BUTTONREMOVE			172
#define IDC_AUTOKICK_BUTTONOVERWRITE		173
#define IDC_AUTOKICK_RADIOID				174
#define IDC_AUTOKICK_RADIONAME				175
#define IDC_AUTOKICK_LIST					176
#define IDC_AUTOKICK_EDIT					177

#define IDD_ROTATION						190
#define IDC_ROTATION_BUTTONOK				191
#define IDC_ROTATION_LIST					192
#define IDC_ROTATION_BUTTONADD				193
#define IDC_ROTATION_EDITMAP				194
#define IDC_ROTATION_BUTTONREMOVE			195
#define IDC_ROTATION_BUTTONCLEAR			196
#define IDC_ROTATION_EDITFILE				197
#define IDC_ROTATION_BUTTONWRITE			198
#define IDC_ROTATION_BUTTONREAD				199
#define IDC_ROTATION_MAPSHOT				200

#define IDD_SETTINGS						210
#define IDC_SETTINGS_EDITTIMEOUTOWNSERVERS	211
#define IDC_SETTINGS_EDITAUTOKICKINTERVAL	212
#define IDC_SETTINGS_EDITAUTORELOAD			213
#define IDC_SETTINGS_CHECKLINECOUNT			214
#define IDC_SETTINGS_EDITLINECOUNT			215
#define IDC_SETTINGS_CHECKCOLORPLAYERS		216
#define IDC_SETTINGS_CHECKCOLORPINGS		217
#define IDC_SETTINGS_CHECKDISABLECONSOLE	218
#define IDC_SETTINGS_BUTTONOK				219

#define IDD_RCONCOMMANDS					230
#define IDC_RCONCOMMANDS_INFOTEXT			231

#define IDD_SETPING							240
#define IDC_SP_EDIT							241
#define IDC_SP_BUTTONOK						243

#define IDD_DUMMY							250
#define IDC_DUMMY_COMBOBOX					251
#define IDC_DUMMY_BUTTON					252
#define IDC_DUMMY_EDIT						253
#define IDC_DUMMY_STATIC					254
#define IDC_DUMMY_BUTTON_PADDINGLARGE		255
#define IDC_DUMMY_BUTTON_PADDINGSMALL		256
#define IDC_DUMMY_BUTTON_PADDINGTINY		257

#endif // __RESOURCE_H_INCLUDED
