// WriteDisk.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

typedef struct OVERLAPPED_EX
{
	 OVERLAPPED ov;
	 LPVOID	buffer;
};

typedef struct ThreadWorkInfo
{
	HANDLE iocp;
	HANDLE file;
	HANDLE* done;
	volatile LONG* workerCount;
	volatile BOOL* anyError;	
	INT lastError;
	OVERLAPPED_EX overlapped;


	// The crtPage is heavily shared by all threads
	// put it in its own cache line
	//
	__declspec(align(128)) volatile ULONGLONG* crtPage;
};

#ifdef _DEBUG
#define DEBUG_ONLY(x) x
#else
#define DEBUG_ONLY(x)
#endif

void ReportError (DWORD lastError, TCHAR* message)
{
	LPVOID lpMsgBuf;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        lastError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

	_ftprintf_s(stderr, _T("%s: Error: %d: %s"), message, lastError, lpMsgBuf);

    LocalFree(lpMsgBuf);
}

void WorkerThread (void* params)
{
	ThreadWorkInfo* myWork = (ThreadWorkInfo*)params;
	DWORD bytesCopied;
	ULONG key;
	LPOVERLAPPED pOverlapped = &myWork->overlapped.ov;
	VOID* buffer = myWork->overlapped.buffer;

	DEBUG_ONLY(_ftprintf_s(stderr, _T("Thread %d started\n"), GetCurrentThreadId ()));

	do
	{
		ULONGLONG myPage = InterlockedIncrement(myWork->crtPage);
		if (myPage >= PAGE_COUNT)
		{
			break;
		}

		DEBUG_ONLY(_ftprintf_s(stderr, _T("Thread %d is writing page %d\n"), GetCurrentThreadId (), myPage));

		// Fill in our buffer, to simulate some 'work'. 
		// We're going to write our page number
		//
		buffer = ((OVERLAPPED_EX*)pOverlapped)->buffer;

		ULONGLONG* pData = (ULONGLONG*)buffer;
		for (int i=0; i<PAGE_SIZE/sizeof(ULONGLONG); ++i)
		{
			pData[i] = myPage;
		}

		// Calculate the offset to which to write our page into the file
		//
		ULONGLONG offset = (ULONGLONG) myPage * (ULONGLONG) PAGE_SIZE;
		pOverlapped->Offset = offset & 0xFFFFFFFF;
		pOverlapped->OffsetHigh = (offset & 0xFFFFFFFF00000000) >> 32;

		// Submit our page to IO
		// At this moment we're religuishing ownership of buffer and OVERLAPPED
		//
		if (!WriteFile (
			myWork->file, 
			buffer, 
			PAGE_SIZE, 
			NULL, 
			pOverlapped))
		{
			myWork->lastError = GetLastError ();

			// ERROR_IO_PENDING is expected
			//
			if (myWork->lastError != ERROR_IO_PENDING)
			{
				*myWork->anyError = TRUE;
				break;
			}
		}

		// Get a freed OVERLAPPED/buffer
		//
		if (!GetQueuedCompletionStatus (
			myWork->iocp,
			&bytesCopied,
			&key,
			&pOverlapped,
			INFINITE))
		{
			myWork->lastError = GetLastError ();
			*myWork->anyError = TRUE;
			break;
		}
	} while (*myWork->anyError == FALSE);

	if (0 == InterlockedDecrement(myWork->workerCount))
	{
		// Signal completion
		//
		if (!SetEvent (*myWork->done))
		{
			ReportError (GetLastError(), _T("SetEvent"));

			// Must exit, main thead is blocked and we cannot signal it
			//
			exit(1);
		}
		DEBUG_ONLY(_ftprintf_s(stderr, _T("Thread %d has signaled Done, exiting\n"), GetCurrentThreadId ()));

	}
	else
	{
		// Do not exit the thread before all work is done 
		// to avoid ERROR_OPERATION_ABORTED
		//
		DEBUG_ONLY(_ftprintf_s(stderr, _T("Thread %d finished, waiting for exit\n"), GetCurrentThreadId ()));
		WaitForSingleObject (myWork->done, INFINITE);
	}
};

int iocp()
{
	SYSTEM_INFO sysinfo;
	GetSystemInfo (&sysinfo);

	INT threadCount = sysinfo.dwNumberOfProcessors * 2;

	_ftprintf_s(stdout, _T("Expand the file to the desired size. Requires SE_MANAGE_VOLUME_NAME privilege\n"), PAGE_COUNT, threadCount);

	// Adjust the process toke to enable SE_MANAGE_VOLUME_NAME priviledge
	//
	HANDLE hToken;
	if (!::OpenProcessToken(
		::GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, 
		&hToken))
	{
		ReportError (GetLastError (), _T("OpenProcessToken"));
		return 1;
	}
	
	LUID luid;
	if (!::LookupPrivilegeValue(
		NULL, 
		SE_MANAGE_VOLUME_NAME, 
		&luid))
	{
		ReportError (GetLastError (), _T("LookupPrivilegeValue"));
		return 1;
	}

	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!::AdjustTokenPrivileges(
		hToken, 
		FALSE, 
		&tp,
		sizeof(TOKEN_PRIVILEGES), 
		NULL, 
		NULL))
	{
		ReportError (GetLastError (), _T("AdjustTokenPrivileges"));
		return 1;
	}

	if (ERROR_NOT_ALL_ASSIGNED == GetLastError ())
	{
		ReportError (GetLastError (), _T("AdjustTokenPrivileges"));
		return 1;
	}

	CloseHandle(hToken);

	// Expand the file to the desired size. Implies 3 steps: SetFilePointer, SetEndFile and SetFileValidata
	//

	HANDLE file = CreateFile (
		_T("file.binary"), 
		GENERIC_WRITE, 
		0, 
		NULL, 
		CREATE_ALWAYS, 
		FILE_ATTRIBUTE_NORMAL | 
		FILE_FLAG_NO_BUFFERING | 
		FILE_FLAG_WRITE_THROUGH | 
		FILE_FLAG_OVERLAPPED, 
		NULL);
	if (!file)
	{
		ReportError (GetLastError (), _T("CreateFile"));
		return 1;
	}

	LARGE_INTEGER eof;
	eof.QuadPart = TOTAL_SIZE;

	if (!SetFilePointerEx(file, eof, NULL, FILE_BEGIN))
	{
		ReportError (GetLastError (), _T("SetFilePointerEx"));
		return 1;
	}

	if (!SetEndOfFile(file))
	{
		ReportError (GetLastError (), _T("SetEndOfFile"));
		return 1;
	}

	if (!SetFileValidData(file, TOTAL_SIZE))
	{
		ReportError (GetLastError (), _T("SetFileValidData"));
		return 1;
	}

	// Take the start time and perf counters frequency
	//

	_ftprintf_s(stdout, _T("Will write %I64d pages on %d threads\n"), PAGE_COUNT, threadCount);

	HANDLE iocp = CreateIoCompletionPort (
		file,
		NULL,
		NULL,
		0);
	if (!iocp)
	{
		ReportError (GetLastError (), _T("CreateIoCompletionPort"));
		return 1;
	}

	HANDLE done = CreateEvent (
				NULL, 
				TRUE,
				FALSE,
				NULL);
	if (!done)
	{
		ReportError (GetLastError (), _T("CreateEvent"));
		return 1;
	}

	volatile ULONGLONG crtPage = 0;
	volatile BOOL anyError = FALSE;
	volatile LONG workerCount = 0;

	ThreadWorkInfo* workInfo = static_cast<ThreadWorkInfo*>(
		alloca (sizeof(ThreadWorkInfo) * threadCount));

	ZeroMemory (workInfo, sizeof(ThreadWorkInfo) * threadCount);

	// Take an extra count for the launch to prevent premature finish
	//
	InterlockedIncrement (&workerCount);

	for (int i=0; i < threadCount && !anyError; ++i)
	{
		workInfo[i].file = file;
		workInfo[i].iocp = iocp;
		workInfo[i].anyError = &anyError;
		workInfo[i].crtPage = &crtPage;
		workInfo[i].workerCount = &workerCount;
		workInfo[i].done = &done;
		workInfo[i].overlapped.buffer = malloc(PAGE_SIZE);

		InterlockedIncrement (&workerCount);
		if (-1L == _beginthread (WorkerThread, 0, &workInfo[i]))
		{
			ReportError (errno, _T("_beginthread"));
			anyError = TRUE;
			break;
		}
	}

	// Remove our extra count, check for Done flag
	//
	if (0 == InterlockedDecrement (&workerCount))
	{
		DEBUG_ONLY(_ftprintf_s(stderr, _T("Thread %d (Main) has signaled Done\n"), GetCurrentThreadId ()));
		SetEvent (done);
	}

	DEBUG_ONLY(_ftprintf_s(stderr, _T("Main thread is waiting for completion\n")));

	if (WAIT_OBJECT_0 != WaitForSingleObject (done, INFINITE))
	{
		ReportError (GetLastError (), _T("WaitForSingleObject"));
		return 1;
	}

	_ftprintf_s(stderr, _T("Main thread is shuting down...\n"));

	CloseHandle (done);
	CloseHandle (file);
	CloseHandle (iocp);


	for (int i=0; i < threadCount; ++i)
	{
		if (anyError)
		{
			if (workInfo[i].lastError)
			{
				ReportError (workInfo[i].lastError, _T("WorkerThread"));
			}
		}
		if (workInfo[i].overlapped.buffer)
		{
			free (workInfo[i].overlapped.buffer);
			workInfo[i].overlapped.buffer = NULL;
		}
	}

	return anyError ? 1 : 0;
};
