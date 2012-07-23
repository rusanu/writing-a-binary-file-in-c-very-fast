// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <string.h>
#include <tchar.h>
#include <Windows.h>
#include <process.h>
#include <malloc.h>
#include <fstream>

// Page size, 4kb
//
#define PAGE_SIZE (4*1024)

// Test write size, 64Mb
//
#define TOTAL_SIZE (64LL*1024LL*1024LL)

// page count
//
#define PAGE_COUNT (TOTAL_SIZE / PAGE_SIZE)

