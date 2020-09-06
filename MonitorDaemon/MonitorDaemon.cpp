#include <Windows.h>
#include <Wininet.h>
#include <iostream>
#include <winevt.h>

#pragma comment(lib, "Wininet.lib")
#pragma comment(lib, "Wevtapi.lib")

#define APPLICATION_NAME TEXT("MonitorDaemon")
#define TIMER_MONITOR_ON 1
#define TIMER_MONITOR_ON_TIMEOUT 1
#define TIMER_MONITOR_ON_TIMEOUT2 3000
#define CONTROL_HOSTNAME TEXT("homepi.local")
#define CONTROL_URL TEXT("http://homepi.local")
#define CONTROL_CONNECTIVITY_TIMEOUT 5000
#define FORM_HEADERS TEXT("Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n")
#define EVENT_LOG_NAME L"System"

BOOL enabled = TRUE;
HINTERNET hInternet;

DWORD CheckEvent(EVT_HANDLE hEvent, BOOL* bRestart)
{
	DWORD status = ERROR_SUCCESS;
	DWORD dwBufferSize = 0;
	DWORD dwBufferUsed = 0;
	DWORD dwPropertyCount = 0;
	LPWSTR pRenderedContent = NULL;

	// The EvtRenderEventXml flag tells EvtRender to render the event as an XML string.
	if (!EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, pRenderedContent, &dwBufferUsed, &dwPropertyCount))
	{
		if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError()))
		{
			dwBufferSize = dwBufferUsed;
			pRenderedContent = (LPWSTR)malloc(dwBufferSize);
			if (pRenderedContent)
			{
				EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, pRenderedContent, &dwBufferUsed, &dwPropertyCount);
			}
			else
			{
				wprintf(L"malloc failed\n");
				status = ERROR_OUTOFMEMORY;
				goto cleanup;
			}
		}

		if (ERROR_SUCCESS != (status = GetLastError()))
		{
			wprintf(L"EvtRender failed with %d\n", GetLastError());
			goto cleanup;
		}
	}

	wprintf(L"%s\n", pRenderedContent);
	if (wcsstr(pRenderedContent, L"restart"))
	{
		*bRestart = TRUE;
	}

cleanup:

	if (pRenderedContent)
		free(pRenderedContent);

	return status;
}

DWORD isRestart(BOOL *bRet)
{
	DWORD status = ERROR_SUCCESS;
	EVT_HANDLE hResults = NULL;
	LPCWSTR pwsPath = EVENT_LOG_NAME;
	LPCWSTR pwsQuery = L"Event/System[EventID=1074 and TimeCreated[timediff(@SystemTime) <= 10000]]";
	hResults = EvtQuery(NULL, pwsPath, pwsQuery, EvtQueryChannelPath | EvtQueryReverseDirection);
	if (NULL == hResults)
	{
		status = GetLastError();

		if (ERROR_EVT_CHANNEL_NOT_FOUND == status)
			wprintf(L"The channel was not found.\n");
		else if (ERROR_EVT_INVALID_QUERY == status)
			wprintf(L"The query is not valid.\n");
		else
			wprintf(L"EvtQuery failed with %lu.\n", status);

		return status;
	}
	printf("Event Read OK\n");
	status = ERROR_SUCCESS;
	EVT_HANDLE hEvent = NULL;
	DWORD dwReturned = 0;
	if (!EvtNext(hResults, 1, &hEvent, INFINITE, 0, &dwReturned))
	{
		if (ERROR_NO_MORE_ITEMS != (status = GetLastError()))
		{
			wprintf(L"EvtNext failed with %lu\n", status);
		}
		if (hEvent != NULL)
		{
			EvtClose(hEvent);
			hEvent = NULL;
		}
		return status;
	}
	*bRet = FALSE;
	
	if (ERROR_SUCCESS == (status = CheckEvent(hEvent, bRet)))
	{
		if (hEvent != NULL)
		{
			EvtClose(hEvent);
			hEvent = NULL;
		}
	}
	
	printf("IsRestarting: %d\n", *bRet);
	EvtClose(hResults);
	return ERROR_SUCCESS;
}

void postData(const char* data)
{
	if (hInternet = InternetOpen(
		APPLICATION_NAME,
		INTERNET_OPEN_TYPE_DIRECT,
		NULL,
		NULL,
		NULL
	))
	{
		if (HINTERNET hConnect = InternetConnect(
			hInternet,
			CONTROL_HOSTNAME,
			INTERNET_DEFAULT_HTTP_PORT,
			NULL,
			NULL,
			INTERNET_SERVICE_HTTP,
			NULL,
			NULL
		))
		{
			if (HINTERNET hRequest = HttpOpenRequest(
				hConnect,
				L"POST",
				L"/",
				NULL,
				CONTROL_URL,
				NULL,
				NULL,
				NULL
			))
			{
				TCHAR headers[] = FORM_HEADERS;
				if (HttpSendRequest(
					hRequest,
					headers,
					wcslen(headers),
					reinterpret_cast<LPVOID>(const_cast<char*>(data)),
					strlen(data) * sizeof(char)
				))
				{
					printf("request OK\n");
				}
				else
				{
					wprintf(L"HttpSendRequest failed with 0x%x.\n", GetLastError());
				}
				InternetCloseHandle(hRequest);
			}
			else
			{
				wprintf(L"HttpOpenRequest failed with 0x%x.\n", GetLastError());
			}
			InternetCloseHandle(hConnect);
		}
		else
		{
			wprintf(L"InternetConnect failed with 0x%x.\n", GetLastError());
		}
		InternetCloseHandle(hInternet);
	}
	else
	{
		wprintf(L"InternetOpen failed with 0x%x.\n", GetLastError());
	}
}

void monitorOn(const char* reason)
{
	if (enabled)
	{
		printf("monitor on: %s\n", reason);
		postData("source=15&r2=0");
	}
}

void monitorOff(const char* reason)
{
	if (enabled)
	{
		printf("monitor off: %s\n", reason);
		postData("r2=1");
	}
}

void setEnabled()
{
	SYSTEM_POWER_STATUS powerStatus;
	GetSystemPowerStatus(&powerStatus);
	enabled = powerStatus.ACLineStatus;
}

LRESULT CALLBACK WindowProc(
	_In_ HWND   hwnd,
	_In_ UINT   uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
)
{
	switch (uMsg)
	{
	case WM_TIMER:
	{
		if (wParam == TIMER_MONITOR_ON)
		{
			monitorOn("on: timer");
			KillTimer(
				hwnd,
				TIMER_MONITOR_ON
			);
			return 0;
		}
		break;
	}
	case WM_POWERBROADCAST:
	{
		if (wParam == PBT_APMPOWERSTATUSCHANGE)
		{
			setEnabled();
			if (enabled)
			{
				monitorOn("on: system connected to AC, enabled");
			}
			else
			{
				printf("system running on battery, disabled\n");
			}
			return TRUE;
		}
		else if (wParam == PBT_APMRESUMEAUTOMATIC)
		{
			setEnabled();
			monitorOn("on: resume from sleep/hibernate");
			SetTimer(
				hwnd,
				TIMER_MONITOR_ON,
				TIMER_MONITOR_ON_TIMEOUT,
				NULL
			);
			return TRUE;
		}
		else if (wParam == PBT_APMSUSPEND)
		{
			setEnabled();
			monitorOff("off: about to sleep/hibernate");
			return TRUE;
		}
		break;
	}
	}
	return DefWindowProc(
		hwnd,
		uMsg,
		wParam,
		lParam
	);
}

void waitForInternet()
{
	if (enabled)
	{
		while (!InternetCheckConnection(
			CONTROL_URL,
			FLAG_ICC_FORCE_CONNECTION,
			NULL
		))
		{
			printf("waiting for Internet connectivity\n");
			Sleep(CONTROL_CONNECTIVITY_TIMEOUT);
		}
		printf("connected to Internet\n");
	}
}

int wWinMain(
	_In_     HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_     LPWSTR    lpCmdLine,
	_In_     int       nShowCmd
)
{
#ifdef _DEBUG
	FILE* f;
	AllocConsole();
	freopen_s(&f, "CONOUT$", "w", stdout);
#else
	FILE *f, *g;
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		freopen_s(&f, "CONOUT$", "w", stdout);
		freopen_s(&g, "CONOUT$", "w", stderr);
	}
#endif

	setEnabled();

	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = WindowProc;
	wndClass.hInstance = hInstance;
	wndClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BACKGROUND);
	wndClass.lpszClassName = APPLICATION_NAME;

	HWND hWnd = CreateWindowEx(
		NULL,
		reinterpret_cast<LPCWSTR>(
			MAKEINTATOM(
				RegisterClass(&wndClass)
			)
		),
		APPLICATION_NAME,
		NULL,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	MSG msg;
	BOOL bRet;

	while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
	{
		if (bRet == -1)
		{
			// handle the error and possibly exit
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return 0;
}