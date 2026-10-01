///////////////////////////////////////////////////////////////////////////////
//
//	PURPOSE
//		Some general-purpose functions for accessing Wintab.
//
//	COPYRIGHT
//		Copyright (c) 2012-2020 Wacom Co., Ltd.
//
//		The text and information contained in this file may be freely used,
//		copied, or distributed without compensation or licensing restrictions.
//
///////////////////////////////////////////////////////////////////////////////

#include "WintabUtils.h"

//////////////////////////////////////////////////////////////////////////////

HINSTANCE ghWintab = NULL;

WTINFOA gpWTInfoA = NULL;
WTOPENA gpWTOpenA = NULL;
WTCLOSE gpWTClose = NULL;
WTPACKET gpWTPacket = NULL;

//////////////////////////////////////////////////////////////////////////////
// Purpose
//		Find wintab32.dll and load it.
//		Find the exported functions we need from it.
//
//	Returns
//		TRUE on success.
//		FALSE on failure.
//
BOOL LoadWintab(void)
{
	ghWintab = LoadLibraryA( "Wintab32.dll" );
	if (!ghWintab)
	{
		DWORD err = GetLastError();
		ShowError("Could not load Wintab32.dll");
		return FALSE;
	}

	// Explicitly find the exported Wintab functions in which we are interested.
	// We are using the ASCII, not unicode versions (where applicable).
	gpWTOpenA = (WTOPENA)GetProcAddress(ghWintab, "WTOpenA");
	gpWTInfoA = (WTINFOA)GetProcAddress(ghWintab, "WTInfoA");
	gpWTPacket = (WTPACKET)GetProcAddress(ghWintab, "WTPacket");
	gpWTClose = (WTCLOSE)GetProcAddress(ghWintab, "WTClose");

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// Purpose
//		Uninitializes use of wintab32.dll
//
void UnloadWintab(void)
{
	if (ghWintab)
	{
		FreeLibrary(ghWintab);
		ghWintab = NULL;
	}

	// NULL out pointers
	gpWTInfoA				= NULL;
	gpWTClose				= NULL;
	gpWTOpenA				= NULL;
	gpWTPacket				= NULL;
}

//////////////////////////////////////////////////////////////////////////////
// Purpose
//		Display error to user.
//
void ShowError(const char *pszErrorMessage)
{
	MessageBoxA(NULL, pszErrorMessage, "Scribble", MB_OK | MB_ICONHAND);
}

