// WriteDisk.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

extern int iocp();
extern int cpp_stream ();

typedef int(*pftest)();

int  measure (pftest pf)
{
	LARGE_INTEGER perfFrequency, perfStart, perfEnd;

	QueryPerformanceFrequency (&perfFrequency);
	QueryPerformanceCounter (&perfStart);

	int ret = pf();

	QueryPerformanceCounter (&perfEnd);
	_ftprintf_s(stdout, _T("Wall clock:  %f sec [ %I64d at %I64d]\n"), 
		(float)(perfEnd.QuadPart - perfStart.QuadPart) / (float)perfFrequency.QuadPart,
		perfEnd.QuadPart - perfStart.QuadPart, 
		perfFrequency.QuadPart);

	return ret;
}


int _tmain(int argc, _TCHAR* argv[])
{
	if (argc==2 &&
		0 == _tcsicmp (_T("cpp"), argv[1]))
	{
		return measure(cpp_stream);
	}
	else if (argc==2 &&
		0 == _tcsicmp (_T("iocp"), argv[1]))
	{
		return measure(iocp);
	}
	else
	{
		_ftprintf_s(stdout, _T("Usage: writedisk [cpp|iocp]\n"));
	}
	return 1;
};

