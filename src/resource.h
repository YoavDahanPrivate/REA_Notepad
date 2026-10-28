#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC -1
#endif

// Dialogs
#define IDD_GOTO            101
#define IDD_SETTINGS        102

#define IDC_GOTO_LINE       1001

#define IDC_THEME_LIGHT     1101
#define IDC_THEME_DARK      1102
#define IDC_THEME_SYSTEM    1103
#define IDC_OPEN_TAB        1104
#define IDC_OPEN_WINDOW     1105
#define IDC_SET_WORDWRAP    1106
#define IDC_SET_STATUSBAR   1107
#define IDC_FONT_LABEL      1108
#define IDC_FONT_CHANGE     1109

// Child windows
#define IDC_EDITOR          1500
#define IDC_FINDPANEL       1501

// Find panel controls
#define IDC_FIND_TOGGLE     2001
#define IDC_FIND_EDIT       2002
#define IDC_FIND_NEXT       2003
#define IDC_FIND_PREV       2004
#define IDC_FIND_OPTIONS    2005
#define IDC_FIND_CLOSE      2006
#define IDC_REPL_EDIT       2007
#define IDC_REPL_ONE        2008
#define IDC_REPL_ALL        2009

// Commands
#define IDM_NEWTAB          40001
#define IDM_NEWWINDOW       40002
#define IDM_OPEN            40003
#define IDM_SAVE            40004
#define IDM_SAVEAS          40005
#define IDM_SAVEALL         40006
#define IDM_PAGESETUP       40007
#define IDM_PRINT           40008
#define IDM_CLOSETAB        40009
#define IDM_CLOSEWINDOW     40010
#define IDM_EXIT            40011
#define IDM_CLEARRECENT     40012

#define IDM_UNDO            40101
#define IDM_CUT             40102
#define IDM_COPY            40103
#define IDM_PASTE           40104
#define IDM_DELETE          40105
#define IDM_FIND            40106
#define IDM_FINDNEXT        40107
#define IDM_FINDPREV        40108
#define IDM_REPLACE         40109
#define IDM_GOTO            40110
#define IDM_SELECTALL       40111
#define IDM_TIMEDATE        40112
#define IDM_FONT            40113

#define IDM_ZOOMIN          40201
#define IDM_ZOOMOUT         40202
#define IDM_ZOOMRESET       40203
#define IDM_STATUSBAR       40204
#define IDM_WORDWRAP        40205

#define IDM_SETTINGS        40301
#define IDM_NEXTTAB         40302
#define IDM_PREVTAB         40303
#define IDM_MATCHCASE       40304
#define IDM_WRAPAROUND      40305

#define IDM_ENC_UTF8        40401
#define IDM_ENC_UTF8BOM     40402
#define IDM_ENC_UTF16LE     40403
#define IDM_ENC_UTF16BE     40404
#define IDM_ENC_ANSI        40405
#define IDM_EOL_CRLF        40411
#define IDM_EOL_LF          40412
#define IDM_EOL_CR          40413

#define IDM_NEWMDTAB        40014
#define IDM_CLEARFMT        40120
#define IDM_BING_SEARCH     40121
#define IDM_BING_DEFINE     40122
#define IDM_BING_AUTO       40123
#define IDM_MD_FORMATTED    40206
#define IDM_MD_SYNTAX       40207

// Formatting toolbar
#define IDM_BOLD            40501
#define IDM_ITALIC          40502
#define IDM_STRIKE          40503
#define IDM_LINK            40504
#define IDM_LIST_BULLET     40505
#define IDM_LIST_NUMBER     40506
#define IDM_INDENT_INC      40507
#define IDM_INDENT_DEC      40508
#define IDM_TABLE_INSERT    40509
#define IDM_TABLE_ROW_ABOVE 40510
#define IDM_TABLE_ROW_BELOW 40511
#define IDM_TABLE_DEL_ROW   40512
#define IDM_TABLE_DELETE    40513
#define IDM_HEADING_BASE    40520   // +0 body, +1..+6 heading levels
#define IDM_UCC_BASE        40540   // +0..+8 Unicode control characters

#define IDM_TABLE_GRID      40514
#define IDM_TABLE_COL_LEFT  40515
#define IDM_TABLE_COL_RIGHT 40516
#define IDM_TABLE_DEL_COL   40517
#define IDM_CLOSEOTHERS     40015
#define IDM_CLOSERIGHT      40016
#define IDM_WHATSNEW        40306
#define IDM_ACCOUNT         40307
#define IDM_SIGNIN          40308
#define IDM_WRITINGTOOLS    40309
#define IDM_SPELL_TOGGLE    40310
#define IDM_RTL             40311
#define IDM_SHOWUCC         40312
#define IDM_AI_BASE         40600   // +0..+19 writing tools commands

// ##text## ASCII art
#define IDM_ASCII_TOGGLE        40700
#define IDM_ASCII_CONVERT       40701
#define IDM_ASCII_CENTER        40702
#define IDM_ASCII_KEEP          40703
#define IDM_ASCII_REPLACE       40704
#define IDM_ASCII_LEFT          40705
#define IDM_ASCII_REMOVE        40706
#define IDM_ASCII_HEIGHT_BASE   40710   // +0..3
#define IDM_ASCII_STYLE_BASE    40720   // +0..2
#define IDM_ASCII_FONT_BASE     40730   // +0..7

#define IDM_RECENT_BASE     41000

// More dialogs
#define IDD_LINK            103
#define IDD_TABLE           104
#define IDC_LINK_TEXT       1201
#define IDC_LINK_URL        1202
#define IDC_TABLE_ROWS      1203
#define IDC_TABLE_COLS      1204
#define IDC_SET_FORMATTING  1111
#define IDC_SET_SPELL       1112
#define IDC_SET_RECENT      1113
#define IDC_START_CONTINUE  1114
#define IDC_START_NEW       1115
