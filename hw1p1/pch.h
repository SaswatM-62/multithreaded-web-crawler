// Saswat Mishra
// CSCE 612
// Spring 2026

// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
// Windows + Winsock headers used in Socket.h and Socket.cpp.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// C/C++ standard headers used throughout the project (printf, clock, strlen, etc.)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <string>

// HTML parser header provided in the course home page
#include "HTMLParserBase.h"

#endif //PCH_H
