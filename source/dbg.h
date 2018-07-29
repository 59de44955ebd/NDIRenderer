#pragma once

#include <windows.h>
#include <stdio.h>

#define DBGS(x) OutputDebugStringA(x);
#define DBGW(x) OutputDebugStringW(x);
#define DBGI(x) {char dbg[32];sprintf_s(dbg,32,"%ld",x);OutputDebugStringA(dbg);}

//void OutputDebugStringf(const char * format, ...){
//	va_list args;
//	va_start (args, format);
//	char buffer[512];
//	vsnprintf_s(buffer, 512, format, args);
//	va_end(args);
//	OutputDebugStringA(buffer);
//}
//#define DBGF(...) OutputDebugStringf(__VA_ARGS__);

#define CHECK_GL(x)\
	{int _err=glGetError();if(_err){char buf[128];sprintf_s(buf, 128, "Error in %s: %ld", x, _err);OutputDebugStringA(buf);}}
