#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <dbt.h>
#include < string.h >
#include <winuser.h>
#pragma comment(lib, "advapi32.lib")
#define SVCNAME TEXT("SvcName")

SERVICE_STATUS gSvcStatus;
SERVICE_STATUS_HANDLE gSvcStatusHandle;
HANDLE ghSvcStopEvent = NULL;
HDEVNOTIFY m_hDevNotify = NULL;

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD dwOpcode, DWORD evtype, PVOID evdata, PVOID Context);
VOID WINAPI SvcMain(DWORD, LPTSTR*);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR*);
VOID SvcReportEvent(LPTSTR);

//
// Purpose:
// Entry point for the process
//
// Parameters:
// None
//
// Return value:
// None, defaults to 0 (zero)
//
int __cdecl _tmain(int argc, TCHAR* argv[])
{
	// If command-line parameter is "install", install the service.
	// Otherwise, the service is probably being started by the SCM.

	if (lstrcmpi(argv[1], TEXT("install")) == 0)
	{
		SvcInstall();
		return 0;
	}

	// TO_DO: Add any additional services for the process to this table.
	SERVICE_TABLE_ENTRY DispatchTable[] =
	{
	{ SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain },
	{ NULL, NULL }
	};

	// This call returns when the service has stopped.
	// The process should simply terminate when the call returns.

	if (!StartServiceCtrlDispatcher(DispatchTable))
	{
		SvcReportEvent(TEXT("StartServiceCtrlDispatcher"));
	}
}

//
// Purpose:
// Installs a service in the SCM database
//
// Parameters:
// None
//
// Return value:
// None
//
VOID SvcInstall()
{
	SC_HANDLE schSCManager;
	SC_HANDLE schService;
	TCHAR szUnquotedPath[MAX_PATH];

	if (!GetModuleFileName(NULL, szUnquotedPath, MAX_PATH))
	{
		printf("Cannot install service (%d)\n", GetLastError());
		return;
	}

	// In case the path contains a space, it must be quoted so that
	// it is correctly interpreted. For example,
	// "d:\my share\myservice.exe" should be specified as
	// ""d:\my share\myservice.exe"".
	TCHAR szPath[MAX_PATH];
	StringCbPrintf(szPath, MAX_PATH, TEXT("\"%s\""), szUnquotedPath);

	// Get a handle to the SCM database.

	schSCManager = OpenSCManager(
		NULL, // local computer
		NULL, // ServicesActive database
		SC_MANAGER_ALL_ACCESS); // full access rights

	if (NULL == schSCManager)
	{
		printf("OpenSCManager failed (%d)\n", GetLastError());
		return;
	}

	// Create the service

	schService = CreateService(
		schSCManager, // SCM database
		SVCNAME, // name of service
		SVCNAME, // service name to display
		SERVICE_ALL_ACCESS, // desired access
		SERVICE_WIN32_OWN_PROCESS, // service type
		SERVICE_DEMAND_START, // start type
		SERVICE_ERROR_NORMAL, // error control type
		szPath, // path to service's binary
		NULL, // no load ordering group
		NULL, // no tag identifier
		NULL, // no dependencies
		NULL, // LocalSystem account
		NULL); // no password

	if (schService == NULL)
	{
		printf("CreateService failed (%d)\n", GetLastError());
		CloseServiceHandle(schSCManager);
		return;
	}
	else printf("Service installed successfully\n");

	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
}

//
// Purpose:
// Entry point for the service
//
// Parameters:
// dwArgc - Number of arguments in the lpszArgv array
// lpszArgv - Array of strings. The first string is the name of
// the service and subsequent strings are passed by the process
// that called the StartService function to start the service.
//
// Return value:
// None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
	// Register the handler function for the service

	gSvcStatusHandle = RegisterServiceCtrlHandlerEx(SVCNAME,
		(LPHANDLER_FUNCTION_EX)SvcCtrlHandler, 0);

	if (!gSvcStatusHandle)
	{
		SvcReportEvent(TEXT("RegisterServiceCtrlHandler"));
		return;
	}

	DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
	ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
	NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

	m_hDevNotify =
		RegisterDeviceNotification(gSvcStatusHandle,
			&NotificationFilter, DEVICE_NOTIFY_SERVICE_HANDLE |
			DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

	// These SERVICE_STATUS members remain as set here

	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gSvcStatus.dwServiceSpecificExitCode = 0;

	// Report initial status to the SCM

	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

	// Perform service-specific initialization and work.

	SvcInit(dwArgc, lpszArgv);
}

//
// Purpose:
// The service code
//
// Parameters:
// dwArgc - Number of arguments in the lpszArgv array
// lpszArgv - Array of strings. The first string is the name of
// the service and subsequent strings are passed by the process
// that called the StartService function to start the service.
//
// Return value:
// None
//
VOID SvcInit(DWORD dwArgc, LPTSTR* lpszArgv)
{
	// TO_DO: Declare and set any required variables.
	// Be sure to periodically call ReportSvcStatus() with
	// SERVICE_START_PENDING. If initialization fails, call
	// ReportSvcStatus with SERVICE_STOPPED.

	// Create an event. The control handler function, SvcCtrlHandler,
	// signals this event when it receives the stop control code.

	ghSvcStopEvent = CreateEvent(
		NULL, // default security attributes
		TRUE, // manual reset event
		FALSE, // not signaled
		NULL); // no name

	if (ghSvcStopEvent == NULL)
	{
		ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
		return;
	}

	// Report running status when initialization is complete.

	ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

	// TO_DO: Perform work until service stops.

	while (1)
	{
		// Check whether to stop the service.

		WaitForSingleObject(ghSvcStopEvent, INFINITE);

		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}
}

//
// Purpose:
// Sets the current service status and reports it to the SCM.
//
// Parameters:
// dwCurrentState - The current state (see SERVICE_STATUS)
// dwWin32ExitCode - The system error code
// dwWaitHint - Estimated time for pending operation,
// in milliseconds
//
// Return value:
// None
//
VOID ReportSvcStatus(DWORD dwCurrentState,
	DWORD dwWin32ExitCode,
	DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;

	// Fill in the SERVICE_STATUS structure.

	gSvcStatus.dwCurrentState = dwCurrentState;
	gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
	gSvcStatus.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING)
		gSvcStatus.dwControlsAccepted = 0;
	else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

	if ((dwCurrentState == SERVICE_RUNNING) ||
		(dwCurrentState == SERVICE_STOPPED))
		gSvcStatus.dwCheckPoint = 0;
	else gSvcStatus.dwCheckPoint = dwCheckPoint++;

	// Report the status of the service to the SCM.
	SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose:
// Called by SCM whenever a control code is sent to the service
// using the ControlService function.
//
// Parameters:
// dwCtrl - control code
//
// Return value:
// None
//
VOID WINAPI SvcCtrlHandler(DWORD dwOpcode, DWORD evtype, PVOID evdata, PVOID Context)
{
	HANDLE event_log = RegisterEventSourceW(NULL, "SvcName");
	LPWSTR tab = (LPWSTR)"SvcNameID";



	//HDEVNOTIFY dev_notify = RegisterDeviceNotificationW(gSvcStatusHandle, 
	//	&NotificationFilter,
	//	DEVICE_NOTIFY_WINDOW_HANDLE);




	// Handle the requested control code.

	switch (dwOpcode)
	{
	case SERVICE_CONTROL_STOP:
		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
		UnregisterDeviceNotification(m_hDevNotify);
		// Signal the service to stop.

		SetEvent(ghSvcStopEvent);
		ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

		return;

	case SERVICE_CONTROL_INTERROGATE:
		break;
	case SERVICE_CONTROL_DEVICEEVENT:
		OutputDebugString("SERVICE_CONTROL_DEVICEEVENT");
		switch (evtype)
		{
		case DBT_DEVICEREMOVECOMPLETE:
		{
			OutputDebugString("Device Removal - ");
			LPCWSTR messagestr = L"Device Removal - ";
			ReportEventW(event_log, EVENTLOG_SUCCESS, 0, 0, NULL, 1, 0, &messagestr, NULL);
		}
		break;
		case DBT_DEVICEARRIVAL:
		{
			//_Module.LogEvent(TEXT("Device Arrival"));
			OutputDebugString(L"Device Arrival - ");
			LPCWSTR messagestr = L"Device Arrival - ";

			PDEV_BROADCAST_DEVICEINTERFACE b = (PDEV_BROADCAST_DEVICEINTERFACE)evdata;

			//  Запись серийного номера в файл 
			 HANDLE newFile = CreateFileW(L"C:\\Users\\Daniil\\Desktop\\LOG_USB.TXT",
			 	GENERIC_ALL,    // read and write access 
			 	0,              // no sharing 
			 	NULL,           // default security attributes
			 	CREATE_NEW,  // opens existing  
			 	0,              // default attributes 
			 	0);             // no template file
			 
			 DWORD dwWrite = 0;
			 WriteFile(
			 	newFile, // HANDLE OF FILE
				 b->dbcc_name, // BUFFER FOR WRITE
				 wcslen(b->dbcc_name), // 
			 	&dwWrite, // BYTES WRITTEN
			 	NULL); // IVERLOPED

			ReportEvent(event_log, EVENTLOG_SUCCESS, 0, 0, NULL, 1, 0, &messagestr, NULL);
			CloseHandle(newFile);
		}
		break;
		}
		break;
	default:
		break;
	}
}

//
// Purpose:
// Logs messages to the event log
//
// Parameters:
// szFunction - name of function that failed
//
// Return value:
// None
//
// Remarks:
// The service must have an entry in the Application event log.
//
VOID SvcReportEvent(LPTSTR szFunction)
{
	HANDLE hEventSource;
	LPCTSTR lpszStrings[2];
	TCHAR Buffer[80];

	hEventSource = RegisterEventSourceW(NULL, SVCNAME);

	if (NULL != hEventSource)
	{
		StringCchPrintfW(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

		lpszStrings[0] = SVCNAME;
		lpszStrings[1] = Buffer;

		ReportEventW(hEventSource, // event log handle
			EVENTLOG_ERROR_TYPE, // event type
			0, // event category
			1, // event identifier
			NULL, // no security identifier
			2, // size of lpszStrings array
			0, // no binary data
			&lpszStrings, // array of strings
			NULL); // no binary data

		DeregisterEventSource(hEventSource);
	}
}

