///////////////////////////////////////////////////////////////////////////////
//
//	PURPOSE
//		Some general-purpose defines and functions for accessing Wintab
//
//	COPYRIGHT
//		Copyright (c) 2012-2020 Wacom Co., Ltd.
//
//		The text and information contained in this file may be freely used,
//		copied, or distributed without compensation or licensing restrictions.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include	"windows.h"
#include	"stdio.h"
#include	"stdarg.h"

#include "msgpack.h"
#include	"wintab.h"		// NOTE: get from wactab header package

///////////////////////////////////////////////////////////////////////////////

// Function pointers to Wintab functions exported from wintab32.dll. 
using WTINFOA = UINT ( API * ) ( UINT, UINT, LPVOID );
using WTOPENA = HCTX ( API * ) ( HWND, LPLOGCONTEXT, BOOL );
using WTCLOSE = BOOL ( API * ) ( HCTX );
using WTPACKET = BOOL ( API * ) ( HCTX, UINT, LPVOID );

///////////////////////////////////////////////////////////////////////////////

// Loaded Wintab32 API functions.
extern HINSTANCE ghWintab;

extern WTINFOA gpWTInfoA;
extern WTOPENA gpWTOpenA;
extern WTCLOSE gpWTClose;
extern WTPACKET gpWTPacket;

//////////////////////////////////////////////////////////////////////////////

BOOL LoadWintab(void);
void UnloadWintab(void);

void ShowError(const char *pszErrorMessage);

