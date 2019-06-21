/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
** QGL_WIN.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake2 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/

#include <float.h>
#include "../renderer/r_local.h"
#include "glw_win.h"

int   (WINAPI *qwglChoosePixelFormat)(HDC, CONST PIXELFORMATDESCRIPTOR *);
int   (WINAPI *qwglDescribePixelFormat)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
int   (WINAPI *qwglGetPixelFormat)(HDC);
BOOL  (WINAPI *qwglSetPixelFormat)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
BOOL  (WINAPI *qwglSwapBuffers)(HDC);

BOOL  (WINAPI *qwglCopyContext)(HGLRC, HGLRC, UINT);
HGLRC (WINAPI *qwglCreateContext)(HDC);
HGLRC (WINAPI *qwglCreateLayerContext)(HDC, int);
BOOL  (WINAPI *qwglDeleteContext)(HGLRC);
HGLRC (WINAPI *qwglGetCurrentContext)(VOID);
HDC   (WINAPI *qwglGetCurrentDC)(VOID);
PROC  (WINAPI *qwglGetProcAddress)(LPCSTR);
BOOL  (WINAPI *qwglMakeCurrent)(HDC, HGLRC);
BOOL  (WINAPI *qwglShareLists)(HGLRC, HGLRC);
BOOL  (WINAPI *qwglUseFontBitmaps)(HDC, DWORD, DWORD, DWORD);

BOOL  (WINAPI *qwglUseFontOutlines)(HDC, DWORD, DWORD, DWORD, FLOAT, FLOAT, int, LPGLYPHMETRICSFLOAT);

BOOL  (WINAPI *qwglDescribeLayerPlane)(HDC, int, int, UINT, LPLAYERPLANEDESCRIPTOR);
int   (WINAPI *qwglSetLayerPaletteEntries)(HDC, int, int, int, CONST COLORREF *);
int   (WINAPI *qwglGetLayerPaletteEntries)(HDC, int, int, int, COLORREF *);
BOOL  (WINAPI *qwglRealizeLayerPalette)(HDC, int, BOOL);
BOOL  (WINAPI *qwglSwapLayerBuffers)(HDC, UINT);
BOOL  (WINAPI *qwglSwapIntervalEXT)(int interval);

// Unloads the specified DLL then nulls out all the proc pointers.
void QGL_Shutdown(void)
{
	if (glw_state.hinstOpenGL)
	{
		FreeLibrary(glw_state.hinstOpenGL);
		glw_state.hinstOpenGL = NULL;
	}

	glw_state.hinstOpenGL = NULL;

	qwglCopyContext              = NULL;
	qwglCreateContext            = NULL;
	qwglCreateLayerContext       = NULL;
	qwglDeleteContext            = NULL;
	qwglDescribeLayerPlane       = NULL;
	qwglGetCurrentContext        = NULL;
	qwglGetCurrentDC             = NULL;
	qwglGetLayerPaletteEntries   = NULL;
	qwglGetProcAddress           = NULL;
	qwglMakeCurrent              = NULL;
	qwglRealizeLayerPalette      = NULL;
	qwglSetLayerPaletteEntries   = NULL;
	qwglShareLists               = NULL;
	qwglSwapLayerBuffers         = NULL;
	qwglUseFontBitmaps           = NULL;
	qwglUseFontOutlines          = NULL;

	qwglChoosePixelFormat        = NULL;
	qwglDescribePixelFormat      = NULL;
	qwglGetPixelFormat           = NULL;
	qwglSetPixelFormat           = NULL;
	qwglSwapBuffers              = NULL;

	qwglSwapIntervalEXT			 = NULL;
}

#define GPA(a) (void *)GetProcAddress(glw_state.hinstOpenGL, a)

//mxd
void *QGL_GetProcAddress(const char *name)
{
	void *result = qwglGetProcAddress(name);

	if (result != NULL)
		return result;

	return GPA(name); // Used only to load qwgl* function pointers.
}

// This is responsible for binding our qgl function pointers to the appropriate GL stuff.
// In Windows this means doing a LoadLibrary and a bunch of calls to GetProcAddress.
// On other operating systems we need to do the right thing, whatever that might be.
qboolean QGL_Init(const char *dllname)
{
	glw_state.hinstOpenGL = LoadLibrary(dllname);
	if (glw_state.hinstOpenGL == 0)
	{
		char *buf = NULL;

		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&buf, 0, NULL);
		VID_Printf(PRINT_ALL, "%s\n", buf);
		return false;
	}

	qwglCopyContext              = GPA("wglCopyContext");
	qwglCreateContext            = GPA("wglCreateContext");
	qwglCreateLayerContext       = GPA("wglCreateLayerContext");
	qwglDeleteContext            = GPA("wglDeleteContext");
	qwglDescribeLayerPlane       = GPA("wglDescribeLayerPlane");
	qwglGetCurrentContext        = GPA("wglGetCurrentContext");
	qwglGetCurrentDC             = GPA("wglGetCurrentDC");
	qwglGetLayerPaletteEntries   = GPA("wglGetLayerPaletteEntries");
	qwglGetProcAddress			 = GPA("wglGetProcAddress");
	qwglMakeCurrent              = GPA("wglMakeCurrent");
	qwglRealizeLayerPalette      = GPA("wglRealizeLayerPalette");
	qwglSetLayerPaletteEntries   = GPA("wglSetLayerPaletteEntries");
	qwglShareLists               = GPA("wglShareLists");
	qwglSwapLayerBuffers         = GPA("wglSwapLayerBuffers");
	qwglUseFontBitmaps           = GPA("wglUseFontBitmapsA");
	qwglUseFontOutlines          = GPA("wglUseFontOutlinesA");

	qwglChoosePixelFormat        = GPA("wglChoosePixelFormat");
	qwglDescribePixelFormat      = GPA("wglDescribePixelFormat");
	qwglGetPixelFormat           = GPA("wglGetPixelFormat");
	qwglSetPixelFormat           = GPA("wglSetPixelFormat");
	qwglSwapBuffers              = GPA("wglSwapBuffers");

	qwglSwapIntervalEXT			 = NULL;

	return true;
}

//TODO: mxd. Remove/replace with GLAD debug logic/replace with GL_ARB_debug_output?
void GLimp_EnableLogging(qboolean enable)
{
	if (enable)
	{
		if (!glw_state.log_fp)
		{
			time_t aclock;
			char buffer[1024];

			time(&aclock);
			struct tm* newtime = localtime(&aclock);

			Com_sprintf(buffer, sizeof(buffer), "%s/gl.log", FS_Gamedir()); 
			glw_state.log_fp = fopen(buffer, "wt");

			fprintf(glw_state.log_fp, "%s\n", asctime(newtime));
		}
	}
}

//TODO: mxd. Remove/replace with GLAD debug logic/replace with GL_ARB_debug_output?
void GLimp_LogNewFrame(void)
{
	fprintf(glw_state.log_fp, "*** R_BeginFrame ***\n");
}