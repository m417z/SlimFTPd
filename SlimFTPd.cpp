/*
 * Copyright (c) 2006, Matt Whitlock and WhitSoft Development
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the names of Matt Whitlock and WhitSoft Development nor the
 *     names of their contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <process.h>
#include <algorithm>
#include "permdb.h"
#include "synclogger.h"
#include "userdb.h"
#include "vfs.h"
#include "tree.h"

using namespace std;

#define SERVERID L"SlimFTPd 3.181, by WhitSoft Development (www.whitsoftdev.com)"
#define PACKET_SIZE 1452
enum class IpAddressType {
	LAN = 1,
	WAN,
	LOCAL
};
enum class SocketFileIODirection {
	SEND = 1,
	RECEIVE
};
enum class ReceiveStatus {
	OK = 1,
	NETWORK_ERROR,
	TIMEOUT,
	INVALID_DATA,
	INSUFFICIENT_BUFFER
};

// Service functions {
VOID WINAPI ServiceMain(DWORD, LPTSTR);
VOID WINAPI ServiceHandler(DWORD);
bool Startup();
void Cleanup();
// }

// Configuration functions {
void LogConfError(const wchar_t *, DWORD, const wchar_t *);
bool ConfParseScript(const wchar_t *);
bool ConfSetBindInterface(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetBindPort(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetMaxConnections(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetCommandTimeout(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetConnectTimeout(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetAdminPassword(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetLookupHosts(const wchar_t *pszArg, DWORD dwLine);
bool ConfAddUser(const wchar_t *pszArg, DWORD dwLine);
bool ConfSetUserPassword(const wchar_t *pszUser, const wchar_t *pszArg, DWORD dwLine);
bool ConfSetMountPoint(const wchar_t *pszUser, const wchar_t *pszVirtual, const wchar_t *pszLocal, DWORD dwLine);
bool ConfSetPermission(DWORD dwMode, const wchar_t *pszUser, const wchar_t *pszVirtual, const wchar_t *pszPerms, DWORD dwLine);
// }

// Network functions {
void __cdecl ListenThread(void *);
void __cdecl ConnectionThread(void *);
bool SocketSendString(SOCKET, const wchar_t *);
ReceiveStatus SocketReceiveString(SOCKET, wchar_t *, DWORD, DWORD *);
ReceiveStatus SocketReceiveLetter(SOCKET, wchar_t *, DWORD, DWORD *);
ReceiveStatus SocketReceiveData(SOCKET, char *, DWORD, DWORD *);
SOCKET EstablishDataConnection(SOCKADDR_IN *, SOCKET *);
void LookupHost(const SOCKADDR_IN *sai, wchar_t *pszHostName, size_t stHostName);
bool DoSocketFileIO(SOCKET sCmd, SOCKET sData, HANDLE hFile, SocketFileIODirection direction, DWORD *pdwAbortFlag);
// }

// Miscellaneous support functions {
bool FileSkipBOM(HANDLE hFile);
DWORD FileReadLine(HANDLE, wchar_t *, DWORD);
DWORD SplitTokens(wchar_t *);
const wchar_t * GetToken(const wchar_t *, DWORD);
IpAddressType GetIPAddressType(IN_ADDR ia);
bool CanUserLogin(const wchar_t *pszUser, IN_ADDR iaPeer);
// }

// Global Variables {
HINSTANCE hInst;
SERVICE_STATUS_HANDLE hServiceStatus;
SERVICE_STATUS ServiceStatus;
bool isService;
DWORD dwMaxConnections = 20, dwCommandTimeout = 300, dwConnectTimeout = 15;
bool bLookupHosts = true;
DWORD dwActiveConnections = 0;
SOCKET sListen;
SOCKADDR_IN saiListen;
UserDB *pUsers;
SyncLogger *pLog;
// }

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR pszCmdLine, int nShowCmd)
{
	SERVICE_TABLE_ENTRY ste[]={ { L"SlimFTPd", (LPSERVICE_MAIN_FUNCTION)ServiceMain }, { 0, 0 } };
	MSG msg;

	hInst = hInstance;

	// Are we starting as a service?
	if (wcsstr(pszCmdLine, L"-service") != 0) {
		isService = true;
		StartServiceCtrlDispatcher(ste);
		Cleanup();
		return 0;
	} else {
		isService = false;
	}

	if (Startup()) {
		while (GetMessage(&msg,0,0,0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	} else {
		pLog->Log(L"An error occurred while starting SlimFTPd.");
	}

	Cleanup();

	return 0;
}

VOID WINAPI ServiceMain(DWORD dwArgc, LPTSTR lpszArgv)
{
	// Starting up as a Windows NT service
	hServiceStatus=RegisterServiceCtrlHandler(L"SlimFTPd",(LPHANDLER_FUNCTION)ServiceHandler);
	ServiceStatus.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
	ServiceStatus.dwCurrentState=SERVICE_RUNNING;
	ServiceStatus.dwControlsAccepted=SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN;
	ServiceStatus.dwWin32ExitCode=NO_ERROR;
	ServiceStatus.dwServiceSpecificExitCode=0;
	ServiceStatus.dwCheckPoint=0;
	ServiceStatus.dwWaitHint=0;
	SetServiceStatus(hServiceStatus,&ServiceStatus);

	if (!Startup()) {
		pLog->Log(L"An error occurred while starting SlimFTPd.");
		ServiceStatus.dwCurrentState=SERVICE_STOPPED;
		SetServiceStatus(hServiceStatus,&ServiceStatus);
	}
}

VOID WINAPI ServiceHandler(DWORD fdwControl)
{
	switch (fdwControl) {
		case SERVICE_CONTROL_INTERROGATE:
			SetServiceStatus(hServiceStatus,&ServiceStatus);
			break;
		case SERVICE_CONTROL_STOP:
			pLog->Log(L"The SlimFTPd service has received a request to stop.");
			ServiceStatus.dwCurrentState=SERVICE_STOPPED;
			SetServiceStatus(hServiceStatus,&ServiceStatus);
			break;
		case SERVICE_CONTROL_SHUTDOWN:
			pLog->Log(L"The SlimFTPd service has received notification of a system shutdown.");
			ServiceStatus.dwCurrentState=SERVICE_STOPPED;
			SetServiceStatus(hServiceStatus,&ServiceStatus);
			break;
	}
}

bool Startup()
{
	WSADATA wsad;
	wchar_t szLogFile[512], szConfFile[512];

	// Construct log and config filenames
	GetModuleFileName(0,szLogFile,ARRAYSIZE(szLogFile));
	*wcsrchr(szLogFile, L'\\') = 0;
	wcscpy_s(szConfFile,szLogFile);
	wcscat_s(szLogFile, L"\\SlimFTPd.log");
	wcscat_s(szConfFile, L"\\SlimFTPd.conf");

	// Start logger thread
	pLog=new SyncLogger(szLogFile);

	// Allocate user database
	pUsers = new UserDB;

	// Log some startup info
	pLog->Log(L"-------------------------------------------------------------------------------");
	pLog->Log(SERVERID);
	if (isService) pLog->Log(L"The SlimFTPd service is starting.");
	else pLog->Log(L"SlimFTPd is starting.");

	// Init listen socket to defaults
	ZeroMemory(&saiListen,sizeof(SOCKADDR_IN));
	saiListen.sin_family=AF_INET;
	saiListen.sin_addr.S_un.S_addr=INADDR_ANY;
	saiListen.sin_port=htons(21);

	// Start Winsock
	WSAStartup(MAKEWORD(2,2),&wsad);

	// Exec config script
	if (!ConfParseScript(szConfFile)) return false;

	// Create and bind the listen socket
	sListen=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (bind(sListen,(SOCKADDR *)&saiListen,sizeof(SOCKADDR_IN))) {
		pLog->Log(L"Unable to bind socket. Specified port may already be in use.");
		closesocket(sListen);
		return false;
	}
	listen(sListen,SOMAXCONN);

	// Launch the listen thread
	_beginthread(ListenThread,0,NULL);

	return true;
}

void Cleanup()
{
	// Cleanup Winsock
	WSACleanup();

	// Log the stop of the service
	if (isService) pLog->Log(L"The SlimFTPd service has stopped.");
	else pLog->Log(L"SlimFTPd has stopped.");

	// Deallocate the user database
	delete pUsers;

	// Shut down the logger thread
	delete pLog;
}

void LogConfError(const wchar_t *pszError, DWORD dwLine, const wchar_t *pszArg)
{
	wchar_t sz[1024];
	swprintf_s(sz, (wstring(L"Error on line %u: ") + pszError).c_str(), dwLine, pszArg);
	pLog->Log(sz);
}

bool ConfParseScript(const wchar_t *pszFileName)
{
// Opens and parses a SlimFTPd configuration script file.
// Returns false on error, or true on success.

	wchar_t sz[512], *psz, *psz2;
	wstring strUser;
	DWORD dwLen, dwLine, dwTokens;
	HANDLE hFile;

	swprintf_s(sz,L"Executing \"%s\"...",wcsrchr(pszFileName,L'\\')+1);
	pLog->Log(sz);

	// Open config file
	hFile=CreateFile(pszFileName,GENERIC_READ,0,0,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,0);
	if (hFile==INVALID_HANDLE_VALUE) {
		pLog->Log(L"Unable to open \"SlimFTPd.conf\".");
		return false;
	}

	FileSkipBOM(hFile);

	for (dwLine=1;;dwLine++) {
		dwLen=FileReadLine(hFile, sz, ARRAYSIZE(sz));
		if (dwLen==-1) {
			CloseHandle(hFile);
			if (!strUser.empty()) {
				LogConfError(L"Premature end of script encountered: unterminated User block.",dwLine,0);
				return false;
			} else {
				pLog->Log(L"Configuration script parsed successfully.");
				return true;
			}
		} else if (dwLen>=512) {
			LogConfError(L"Line is too long to parse.",dwLine,0);
			break;
		}
		psz=sz;
		while (*psz==L' ' || *psz==L'\t') psz++;
		if (!*psz || *psz==L'#') continue;

		if (*psz==L'<') {
			psz2=wcschr(psz,L'>');
			if (psz2) {
				*(psz2++)=0;
				while (*psz2==L' ' || *psz2==L'\t') psz2++;
				if (*psz2) {
					LogConfError(L"Syntax error. Expected end of line after '>'.",dwLine,0);
					break;
				}
				psz++;
			} else {
				LogConfError(L"Syntax error. Expected '>' before end of line.",dwLine,0);
				break;
			}
		}

		dwTokens=SplitTokens(psz);

		if (!_wcsicmp(psz,L"BindInterface")) {
			if (dwTokens==2) {
				if (!ConfSetBindInterface(GetToken(psz,2),dwLine)) break;
			} else {
				LogConfError(L"BindInterface directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"BindPort")) {
			if (dwTokens==2) {
				if (!ConfSetBindPort(GetToken(psz,2),dwLine)) break;
			} else {
				LogConfError(L"BindPort directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"MaxConnections")) {
			if (dwTokens==2) {
				if (!ConfSetMaxConnections(GetToken(psz,2),dwLine)) break;
			} else {
				LogConfError(L"MaxConnections directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"CommandTimeout")) {
			if (dwTokens==2) {
				if (!ConfSetCommandTimeout(GetToken(psz,2),dwLine)) break;
			} else {
				LogConfError(L"CommandTimeout directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"ConnectTimeout")) {
			if (dwTokens==2) {
				if (!ConfSetConnectTimeout(GetToken(psz,2),dwLine)) break;
			} else {
				LogConfError(L"ConnectTimeout directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"LookupHosts")) {
			if (dwTokens==2) {
				if (!ConfSetLookupHosts(GetToken(psz,2),dwLine)) break;
			} else {
				LogConfError(L"LookupHosts directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"User")) {
			if (!strUser.empty()) {
				LogConfError(L"<User> directive invalid inside User block.",dwLine,0);
				break;
			} else if (dwTokens==2) {
				if (ConfAddUser(GetToken(psz,2),dwLine)) {
					strUser = GetToken(psz, 2);
				} else {
					break;
				}
			} else {
				LogConfError(L"<User> directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}
		
		else if (!_wcsicmp(psz,L"/User")) {
			if (strUser.empty()) {
				LogConfError(L"</User> directive invalid outside of User block.",dwLine,0);
				break;
			} else if (dwTokens==1) {
				strUser.clear();
			} else {
				LogConfError(L"</User> directive should not have any arguments.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"Password")) {
			if (strUser.empty()) {
				LogConfError(L"Password directive invalid outside of User block.",dwLine,0);
				break;
			} else if (dwTokens==2) {
				if (!ConfSetUserPassword(strUser.c_str(), GetToken(psz, 2), dwLine)) break;
			} else {
				LogConfError(L"Password directive should have exactly 1 argument.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"Mount")) {
			if (strUser.empty()) {
				LogConfError(L"Mount directive invalid outside of User block.",dwLine,0);
				break;
			} else if (dwTokens==3) {
				if (!ConfSetMountPoint(strUser.c_str(), GetToken(psz, 2), GetToken(psz, 3), dwLine)) break;
			} else {
				LogConfError(L"Mount directive should have exactly 2 arguments.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"Allow")) {
			if (strUser.empty()) {
				LogConfError(L"Allow directive invalid outside of User block.",dwLine,0);
				break;
			} else if (dwTokens>=3) {
				if (!ConfSetPermission(1, strUser.c_str(), GetToken(psz, 2), GetToken(psz, 3), dwLine)) break;
			} else {
				LogConfError(L"Allow directive should have at least 2 arguments.",dwLine,0);
				break;
			}
		}

		else if (!_wcsicmp(psz,L"Deny")) {
			if (strUser.empty()) {
				LogConfError(L"Deny directive invalid outside of User block.",dwLine,0);
				break;
			} else if (dwTokens>=3) {
				if (!ConfSetPermission(0, strUser.c_str(), GetToken(psz, 2), GetToken(psz, 3), dwLine)) break;
			} else {
				LogConfError(L"Deny directive should have at least 2 arguments.",dwLine,0);
				break;
			}
		}

		else {
			LogConfError(L"Directive \"%s\" not recognized.",dwLine,psz);
			break;
		}
	}

	CloseHandle(hFile);
	pLog->Log(L"Failed parsing configuration script.");
	return false;
}

bool ConfSetBindInterface(const wchar_t *pszArg, DWORD dwLine)
{
	char sz[512];
	HOSTENT *phe;
	DWORD dw;

	if (!_wcsicmp(pszArg,L"All")) {
		saiListen.sin_addr.S_un.S_addr=INADDR_ANY;
	} else if (!_wcsicmp(pszArg,L"Local")) {
		saiListen.sin_addr.S_un.S_addr=htonl(INADDR_LOOPBACK);
	} else if (!_wcsicmp(pszArg,L"LAN")) {
		saiListen.sin_addr.S_un.S_addr=INADDR_NONE;
		gethostname(sz,ARRAYSIZE(sz));
		phe=gethostbyname(sz);
		if (phe) {
			for (dw=0;phe->h_addr_list[dw];dw++) {
				if (GetIPAddressType(*(IN_ADDR*)phe->h_addr_list[dw]) == IpAddressType::LAN) {
					saiListen.sin_addr.S_un.S_addr=((IN_ADDR*)phe->h_addr_list[dw])->S_un.S_addr;
					break;
				}
			}
		}
		if (saiListen.sin_addr.S_un.S_addr==INADDR_NONE) {
			LogConfError(L"BindInterface directive could not find a LAN interface.",dwLine,0);
			return false;
		}
	} else if (!_wcsicmp(pszArg,L"WAN")) {
		saiListen.sin_addr.S_un.S_addr=INADDR_NONE;
		gethostname(sz,ARRAYSIZE(sz));
		phe=gethostbyname(sz);
		if (phe) {
			for (dw=0;phe->h_addr_list[dw];dw++) {
				if (GetIPAddressType(*(IN_ADDR*)phe->h_addr_list[dw]) == IpAddressType::WAN) {
					saiListen.sin_addr.S_un.S_addr=((IN_ADDR*)phe->h_addr_list[dw])->S_un.S_addr;
					break;
				}
			}
		}
		if (saiListen.sin_addr.S_un.S_addr==INADDR_NONE) {
			LogConfError(L"BindInterface directive could not find a WAN interface.",dwLine,0);
			return false;
		}
	} else {
		if (InetPton(AF_INET, pszArg, &saiListen.sin_addr.S_un.S_addr)!=1) {
			LogConfError(L"BindInterface directive does not recognize argument \"%s\".",dwLine,pszArg);
			return false;
		}
	}
	return true;
}

bool ConfSetBindPort(const wchar_t *pszArg, DWORD dwLine)
{
	WORD wPort;

	wPort = (WORD)StrToInt(pszArg);
	if (wPort) {
		saiListen.sin_port=htons(wPort);
		return true;
	} else {
		LogConfError(L"BindPort directive does not recognize argument \"%s\".",dwLine,pszArg);
		return false;
	}
}

bool ConfSetMaxConnections(const wchar_t *pszArg, DWORD dwLine)
{
	DWORD dw;

	if (!_wcsicmp(pszArg,L"Off")) {
		dwMaxConnections=-1;
		return true;
	} else {
		dw = StrToInt(pszArg);
		if (dw) {
			dwMaxConnections=dw;
			return true;
		} else {
			LogConfError(L"MaxConnections directive does not recognize argument \"%s\".",dwLine,pszArg);
			return false;
		}
	}
}

bool ConfSetCommandTimeout(const wchar_t *pszArg, DWORD dwLine)
{
	DWORD dw;

	dw = StrToInt(pszArg);
	if (dw) {
		dwCommandTimeout=dw;
		return true;
	} else {
		LogConfError(L"CommandTimeout directive does not recognize argument \"%s\".",dwLine,pszArg);
		return false;
	}
}

bool ConfSetConnectTimeout(const wchar_t *pszArg, DWORD dwLine)
{
	DWORD dw;

	dw = StrToInt(pszArg);
	if (dw) {
		dwConnectTimeout=dw;
		return true;
	} else {
		LogConfError(L"ConnectTimeout directive does not recognize argument \"%s\".",dwLine,pszArg);
		return false;
	}
}

bool ConfSetLookupHosts(const wchar_t *pszArg, DWORD dwLine)
{
	if (!_wcsicmp(pszArg,L"Off")) {
		bLookupHosts = false;
		return true;
	} else if (!_wcsicmp(pszArg,L"On")) {
		bLookupHosts = true;
		return true;
	} else {
		LogConfError(L"LookupHosts directive does not recognize argument \"%s\".",dwLine,pszArg);
		return false;
	}
}

bool ConfAddUser(const wchar_t *pszArg, DWORD dwLine)
{
	if (wcslen(pszArg)<32) {
		if (pUsers->Add(pszArg)) {
			return true;
		} else {
			LogConfError(L"User \"%s\" already defined.",dwLine,pszArg);
			return false;
		}
	} else {
		LogConfError(L"Argument to User directive must be less than 32 characters long.",dwLine,0);
		return false;
	}
}

bool ConfSetUserPassword(const wchar_t *pszUser, const wchar_t *pszArg, DWORD dwLine)
{
	if (wcslen(pszArg)<32) {
		pUsers->SetPassword(pszUser,pszArg);
		return true;
	} else {
		LogConfError(L"Argument to Password directive must be less than 32 characters long.",dwLine,0);
		return false;
	}
}

bool ConfSetMountPoint(const wchar_t *pszUser, const wchar_t *pszVirtual, const wchar_t *pszLocal, DWORD dwLine)
{
	VFS *pvfs;
	wstring strVirtual, strLocal;

	VFS::CleanVirtualPath(pszVirtual, strVirtual);

	if (strVirtual.at(0) != L'/') {
		LogConfError(L"Mount directive cannot parse invalid virtual path \"%s\". Virtual paths must begin with a slash.", dwLine, strVirtual.c_str());
		return false;
	}
	if (pszLocal) {
		strLocal = pszLocal;
		replace(strLocal.begin(), strLocal.end(), L'/', L'\\');
		if (*strLocal.rbegin() == L'\\') {
			strLocal = strLocal.substr(0, strLocal.length() - 1);
		}
		if (GetFileAttributes(strLocal.c_str()) == INVALID_FILE_ATTRIBUTES) {
			LogConfError(L"Mount directive cannot find local path \"%s\".", dwLine, strLocal.c_str());
			return false;
		}
	}
	pvfs=pUsers->GetVFS(pszUser);
	if (pvfs) pvfs->Mount(pszVirtual, pszLocal);
	return true;
}

bool ConfSetPermission(DWORD dwMode, const wchar_t *pszUser, const wchar_t *pszVirtual, const wchar_t *pszPerms, DWORD dwLine)
{
	PermDB *pperms;

	wstring strVirtual;
	VFS::CleanVirtualPath(pszVirtual, strVirtual);

	if (strVirtual.at(0) != L'/') {
		if (dwMode) {
			LogConfError(L"Allow directive cannot parse invalid virtual path \"%s\". Virtual paths must begin with a slash.", dwLine, strVirtual.c_str());
		} else {
			LogConfError(L"Deny directive cannot parse invalid virtual path \"%s\". Virtual paths must begin with a slash.", dwLine, strVirtual.c_str());
		}
		return false;
	}

	pperms=pUsers->GetPermDB(pszUser);
	if (!pperms) return false;

	while (*pszPerms) {
		if (!_wcsicmp(pszPerms,L"Read")) {
			pperms->SetPerm(strVirtual.c_str(), PERM_READ, dwMode);
		} else if (!_wcsicmp(pszPerms,L"Write")) {
			pperms->SetPerm(strVirtual.c_str(), PERM_WRITE, dwMode);
		} else if (!_wcsicmp(pszPerms,L"List")) {
			pperms->SetPerm(strVirtual.c_str(), PERM_LIST, dwMode);
		} else if (!_wcsicmp(pszPerms,L"Admin")) {
			pperms->SetPerm(strVirtual.c_str(), PERM_ADMIN, dwMode);
		} else if (!_wcsicmp(pszPerms,L"All")) {
			pperms->SetPerm(strVirtual.c_str(), PERM_READ, dwMode);
			pperms->SetPerm(strVirtual.c_str(), PERM_WRITE, dwMode);
			pperms->SetPerm(strVirtual.c_str(), PERM_LIST, dwMode);
			pperms->SetPerm(strVirtual.c_str(), PERM_ADMIN, dwMode);
		} else {
			if (dwMode) {
				LogConfError(L"Allow directive does not recognize argument \"%s\".",dwLine,pszPerms);
			} else {
				LogConfError(L"Deny directive does not recognize argument \"%s\".",dwLine,pszPerms);
			}
			return false;
		}
		pszPerms=GetToken(pszPerms,2);
	}
	return true;
}

void __cdecl ListenThread(void *)
{
	SOCKET sIncoming;

	pLog->Log(L"Waiting for incoming connections...");

	// Accept incoming connections and pass them to connection threads
	while ((sIncoming=accept(sListen,0,0))!=INVALID_SOCKET) {
		_beginthread(ConnectionThread,0,(void *)sIncoming);
	}

	closesocket(sListen);
}

void __cdecl ConnectionThread(void *pParam)
{
	SOCKET sCmd = (SOCKET)pParam;
	SOCKET sData=0, sPasv=0;
	SOCKADDR_IN saiCmd, saiCmdPeer, saiData, saiPasv;
	wchar_t szPeerName[64], szOutput[1024], szCmd[512], *pszParam;
	wstring strUser, strCurrentVirtual, strNewVirtual, strRnFr;
	DWORD dw, dwRestOffset=0;
	ReceiveStatus status;
	bool isLoggedIn = false;
	HANDLE hFile;
	SYSTEMTIME st;
	FILETIME ft;
	VFS *pVFS = NULL;
	PermDB *pPerms = NULL;
	VFS::listing_type listing;
	UINT_PTR i;

	ZeroMemory(&saiData, sizeof(SOCKADDR_IN));

	// Get peer address
	dw=sizeof(SOCKADDR_IN);
	getpeername(sCmd, (SOCKADDR *)&saiCmdPeer, (int *)&dw);
	LookupHost(&saiCmdPeer, szPeerName, ARRAYSIZE(szPeerName));

	// Log incoming connection
	swprintf_s(szOutput, L"[%u] Incoming connection from %s:%u.", sCmd, szPeerName, ntohs(saiCmdPeer.sin_port));
	pLog->Log(szOutput);

	// Send greeting
	swprintf_s(szOutput, L"220-%s\r\n220-You are connecting from %s:%u.\r\n220 Proceed with login.\r\n", SERVERID, szPeerName, ntohs(saiCmdPeer.sin_port));
	SocketSendString(sCmd, szOutput);

	// Get host address
	dw=sizeof(SOCKADDR_IN);
	getsockname(sCmd, (SOCKADDR *)&saiCmd, (int *)&dw);

	// Command processing loop
	for (;;) {

		status=SocketReceiveString(sCmd,szCmd,ARRAYSIZE(szCmd),&dw);

		if (status==ReceiveStatus::NETWORK_ERROR) {
			SocketSendString(sCmd,L"421 Network error.\r\n");
			break;
		} else if (status==ReceiveStatus::TIMEOUT) {
			SocketSendString(sCmd,L"421 Connection timed out.\r\n");
			break;
		} else if (status==ReceiveStatus::INVALID_DATA) {
			SocketSendString(sCmd,L"500 Malformed request.\r\n");
			continue;
		} else if (status==ReceiveStatus::INSUFFICIENT_BUFFER) {
			SocketSendString(sCmd,L"500 Command line too long.\r\n");
			continue;
		}

		if (pszParam = wcschr(szCmd, L' ')) *(pszParam++) = 0;
		else pszParam = szCmd+wcslen(szCmd);

		if (!_wcsicmp(szCmd, L"USER")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
				continue;
			} else if (isLoggedIn) {
				SocketSendString(sCmd, L"503 Already logged in. Use REIN to change users.\r\n");
				continue;
			} else {
				strUser = pszParam;
				if (pUsers->CheckPassword(strUser.c_str(), L"")) {
					wcscpy_s(szCmd, L"PASS");
					szCmd[5] = 0;
				} else {
					swprintf_s(szOutput, L"331 Need password for user \"%s\".\r\n", strUser.c_str());
					SocketSendString(sCmd, szOutput);
					continue;
				}
			}
		}

		if (!_wcsicmp(szCmd, L"PASS")) {
			if (strUser.empty()) {
				SocketSendString(sCmd, L"503 Bad sequence of commands. Send USER first.\r\n");
			} else if (isLoggedIn) {
				SocketSendString(sCmd, L"503 Already logged in. Use REIN to change users.\r\n");
			} else {
				if (pUsers->CheckPassword(strUser.c_str(), pszParam)) {
					if (CanUserLogin(strUser.c_str(), saiCmdPeer.sin_addr)) {
						isLoggedIn = true;
						dwActiveConnections++;
						strCurrentVirtual = L"/";
						swprintf_s(szOutput, L"230 User \"%s\" logged in.\r\n", strUser.c_str());
						SocketSendString(sCmd, szOutput);
						swprintf_s(szOutput, L"[%u] User \"%s\" logged in.", sCmd, strUser.c_str());
						pLog->Log(szOutput);
						pVFS = pUsers->GetVFS(strUser.c_str());
						pPerms = pUsers->GetPermDB(strUser.c_str());
					} else {
						SocketSendString(sCmd, L"421 Your login was refused due to a server connection limit.\r\n");
						swprintf_s(szOutput, L"[%u] Login for user \"%s\" refused due to connection limit.", sCmd, strUser.c_str());
						pLog->Log(szOutput);
						break;
					}
				} else {
					SocketSendString(sCmd,L"530 Incorrect password.\r\n");
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"REIN")) {
			if (isLoggedIn) {
				isLoggedIn = false;
				dwActiveConnections--;
				swprintf_s(szOutput, L"220-User \"%s\" logged out.\r\n", strUser.c_str());
				SocketSendString(sCmd, szOutput);
				swprintf_s(szOutput, L"[%u] User \"%s\" logged out.", sCmd, strUser.c_str());
				pLog->Log(szOutput);
				strUser.clear();
			}
			SocketSendString(sCmd, L"220 REIN command successful.\r\n");
		}

		else if (!_wcsicmp(szCmd, L"HELP")) {
			SocketSendString(sCmd, L"214 For help, please visit www.whitsoftdev.com.\r\n");
		}

		else if (!_wcsicmp(szCmd, L"FEAT")) {
			SocketSendString(sCmd, L"211-Extensions supported:\r\n SIZE\r\n REST STREAM\r\n MDTM\r\n TVFS\r\n UTF8\r\n211 END\r\n");
		}

		else if (!_wcsicmp(szCmd, L"SYST")) {
			swprintf_s(szOutput, L"215 WIN32 Type: L8 Version: %s\r\n", SERVERID);
			SocketSendString(sCmd, szOutput);
		}

		else if (!_wcsicmp(szCmd, L"QUIT")) {
			if (isLoggedIn) {
				isLoggedIn = false;
				dwActiveConnections--;
				swprintf_s(szOutput, L"221-User \"%s\" logged out.\r\n", strUser.c_str());
				SocketSendString(sCmd, szOutput);
				swprintf_s(szOutput, L"[%u] User \"%s\" logged out.", sCmd, strUser.c_str());
				pLog->Log(szOutput);
			}
			SocketSendString(sCmd, L"221 Goodbye!\r\n");
			break;
		}

		else if (!_wcsicmp(szCmd, L"NOOP")) {
			SocketSendString(sCmd, L"200 NOOP command successful.\r\n");
		}

		else if (!_wcsicmp(szCmd, L"PWD") || !_wcsicmp(szCmd, L"XPWD")) {
			if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				swprintf_s(szOutput, L"257 \"%s\" is current directory.\r\n", strCurrentVirtual.c_str());
				SocketSendString(sCmd, szOutput);
			}
		}

		else if (!_wcsicmp(szCmd, L"CWD") || !_wcsicmp(szCmd, L"XCWD")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pVFS->IsFolder(strNewVirtual.c_str())) {
					strCurrentVirtual = strNewVirtual;
					swprintf_s(szOutput, L"250 \"%s\" is now current directory.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Path not found.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"CDUP") || !_wcsicmp(szCmd, L"XCUP")) {
			if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), L"..", strNewVirtual);
				strCurrentVirtual = strNewVirtual;
				swprintf_s(szOutput,L"250 \"%s\" is now current directory.\r\n", strCurrentVirtual.c_str());
				SocketSendString(sCmd, szOutput);
			}
		}

		else if (!_wcsicmp(szCmd,L"TYPE")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				SocketSendString(sCmd, L"200 TYPE command successful.\r\n");
			}
		}

		else if (!_wcsicmp(szCmd, L"REST")) {
			if (!*pszParam || (!(dw = StrToInt(pszParam)) && (*pszParam!=L'0'))) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				dwRestOffset = dw;
				swprintf_s(szOutput, L"350 Ready to resume transfer at %u bytes.\r\n", dwRestOffset);
				SocketSendString(sCmd, szOutput);
			}
		}

		else if (!_wcsicmp(szCmd, L"PORT")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				ZeroMemory(&saiData, sizeof(SOCKADDR_IN));
				saiData.sin_family = AF_INET;
				for (dw = 0; dw < 6; dw++) {
					if (dw < 4) ((unsigned char *)&saiData.sin_addr)[dw] = (unsigned char)StrToInt(pszParam);
					else ((unsigned char *)&saiData.sin_port)[dw-4] = (unsigned char)StrToInt(pszParam);
					if (!(pszParam = wcschr(pszParam, L','))) break;
					pszParam++;
				}
				if (dw == 5) {
					if (sPasv) {
						closesocket(sPasv);
						sPasv = 0;
					}
					SocketSendString(sCmd, L"200 PORT command successful.\r\n");
				} else {
					SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
					ZeroMemory(&saiData, sizeof(SOCKADDR_IN));
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"PASV")) {
			if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				if (sPasv) closesocket(sPasv);
				ZeroMemory(&saiPasv, sizeof(SOCKADDR_IN));
				saiPasv.sin_family = AF_INET;
				saiPasv.sin_addr.S_un.S_addr = INADDR_ANY;
				saiPasv.sin_port = 0;
				sPasv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				bind(sPasv, (SOCKADDR *)&saiPasv, sizeof(SOCKADDR_IN));
				listen(sPasv, 1);
				dw = sizeof(SOCKADDR_IN);
				getsockname(sPasv, (SOCKADDR *)&saiPasv, (int *)&dw);
				swprintf_s(szOutput, L"227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n", saiCmd.sin_addr.S_un.S_un_b.s_b1, saiCmd.sin_addr.S_un.S_un_b.s_b2, saiCmd.sin_addr.S_un.S_un_b.s_b3, saiCmd.sin_addr.S_un.S_un_b.s_b4, ((unsigned char *)&saiPasv.sin_port)[0], ((unsigned char *)&saiPasv.sin_port)[1]);
				SocketSendString(sCmd, szOutput);
			}
		}

		else if (!_wcsicmp(szCmd, L"LIST") || !_wcsicmp(szCmd, L"NLST")) {
			if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				if (*pszParam == L'-') if (pszParam = wcschr(pszParam, L' ')) pszParam++;
				if (pszParam && *pszParam) {
					pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				}
				else {
					strNewVirtual = strCurrentVirtual;
				}
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_LIST) == 1) {
					if (pVFS->GetDirectoryListing(strNewVirtual.c_str(), _wcsicmp(szCmd, L"LIST"), listing)) {
						swprintf_s(szOutput, L"150 Opening %s mode data connection for listing of \"%s\".\r\n", sPasv ? L"passive" : L"active", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
						sData = EstablishDataConnection(&saiData, &sPasv);
						if (sData!=INVALID_SOCKET) {
							for (VFS::listing_type::const_iterator it = listing.begin(); it != listing.end(); ++it) {
								SocketSendString(sData, it->second.c_str());
							}
							listing.clear();
							closesocket(sData);
							swprintf_s(szOutput, L"226 %s command successful.\r\n", _wcsicmp(szCmd, L"NLST") ? L"LIST" : L"NLST");
							SocketSendString(sCmd, szOutput);
						} else {
							listing.clear();
							SocketSendString(sCmd, L"425 Can't open data connection.\r\n");
						}
					} else {
						swprintf_s(szOutput, L"550 \"%s\": Path not found.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": List permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"STAT")) {
			if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				if (*pszParam == L'-') if (pszParam = wcschr(pszParam, L' ')) pszParam++;
				if (pszParam && *pszParam) {
					pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				}
				else {
					strNewVirtual = strCurrentVirtual;
				}
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_LIST) == 1) {
					if (pVFS->GetDirectoryListing(strNewVirtual.c_str(), 0, listing)) {
						swprintf_s(szOutput, L"212-Sending directory listing of \"%s\".\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd,szOutput);
						for (VFS::listing_type::const_iterator it = listing.begin(); it != listing.end(); ++it) {
							SocketSendString(sCmd, it->second.c_str());
						}
						listing.clear();
						SocketSendString(sCmd, L"212 STAT command successful.\r\n");
					} else {
						swprintf_s(szOutput, L"550 \"%s\": Path not found.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					swprintf_s(szOutput ,L"550 \"%s\": List permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"RETR")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_READ) == 1) {
					hFile = pVFS->CreateFile(strNewVirtual.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
					if (hFile == INVALID_HANDLE_VALUE) {
						swprintf_s(szOutput, L"550 \"%s\": Unable to open file.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					} else {
						if (dwRestOffset) {
							SetFilePointer(hFile, dwRestOffset, 0, FILE_BEGIN);
							dwRestOffset = 0;
						}
						swprintf_s(szOutput, L"150 Opening %s mode data connection for \"%s\".\r\n", sPasv ? L"passive" : L"active", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
						sData = EstablishDataConnection(&saiData, &sPasv);
						if (sData!=INVALID_SOCKET) {
							swprintf_s(szOutput, L"[%u] User \"%s\" began downloading \"%s\".", sCmd, strUser.c_str(), strNewVirtual.c_str());
							pLog->Log(szOutput);
							if (DoSocketFileIO(sCmd, sData, hFile, SocketFileIODirection::SEND, &dw)) {
								swprintf_s(szOutput, L"226 \"%s\" transferred successfully.\r\n", strNewVirtual.c_str());
								SocketSendString(sCmd, szOutput);
								swprintf_s(szOutput, L"[%u] Download completed.", sCmd);
								pLog->Log(szOutput);
							} else {
								SocketSendString(sCmd, L"426 Connection closed; transfer aborted.\r\n");
								if (dw) SocketSendString(sCmd, L"226 ABOR command successful.\r\n");
								swprintf_s(szOutput, L"[%u] Download aborted.", sCmd);
								pLog->Log(szOutput);
							}
							closesocket(sData);
						} else {
							SocketSendString(sCmd,L"425 Can't open data connection.\r\n");
						}
						CloseHandle(hFile);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Read permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"STOR") || !_wcsicmp(szCmd, L"APPE")) {
			if (!*pszParam) {
				SocketSendString(sCmd,L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd,L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_WRITE) == 1) {
					hFile = pVFS->CreateFile(strNewVirtual.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS);
					if (hFile == INVALID_HANDLE_VALUE) {
						swprintf_s(szOutput, L"550 \"%s\": Unable to open file.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					} else {
						if (_wcsicmp(szCmd, L"APPE") == 0) {
							SetFilePointer(hFile, 0, 0, FILE_END);
						}
						else {
							SetFilePointer(hFile, dwRestOffset, 0, FILE_BEGIN);
							SetEndOfFile(hFile);
						}
						dwRestOffset = 0;
						swprintf_s(szOutput, L"150 Opening %s mode data connection for \"%s\".\r\n", sPasv ? L"passive" : L"active", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
						sData = EstablishDataConnection(&saiData, &sPasv);
						if (sData!=INVALID_SOCKET) {
							swprintf_s(szOutput, L"[%u] User \"%s\" began uploading \"%s\".", sCmd, strUser.c_str(), strNewVirtual.c_str());
							pLog->Log(szOutput);
							if (DoSocketFileIO(sCmd, sData, hFile, SocketFileIODirection::RECEIVE, 0)) {
								swprintf_s(szOutput, L"226 \"%s\" transferred successfully.\r\n", strNewVirtual.c_str());
								SocketSendString(sCmd, szOutput);
								swprintf_s(szOutput, L"[%u] Upload completed.", sCmd);
								pLog->Log(szOutput);
							} else {
								SocketSendString(sCmd, L"426 Connection closed; transfer aborted.\r\n");
								swprintf_s(szOutput, L"[%u] Upload aborted.", sCmd);
								pLog->Log(szOutput);
							}
							closesocket(sData);
						} else {
							SocketSendString(sCmd,L"425 Can't open data connection.\r\n");
						}
						CloseHandle(hFile);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Write permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"ABOR")) {
			if (sPasv) {
				closesocket(sPasv);
				sPasv = 0;
			}
			dwRestOffset = 0;
			SocketSendString(sCmd,L"200 ABOR command successful.\r\n");
		}

		else if (!_wcsicmp(szCmd, L"SIZE")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_READ) == 1) {
					hFile = pVFS->CreateFile(strNewVirtual.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
					if (hFile == INVALID_HANDLE_VALUE) {
						swprintf_s(szOutput, L"550 \"%s\": File not found.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					} else {
						swprintf_s(szOutput, L"213 %u\r\n", GetFileSize(hFile, 0));
						SocketSendString(sCmd, szOutput);
						CloseHandle(hFile);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Read permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"MDTM")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				for (i = 0; i < 14; i++) {
					if ((pszParam[i] < L'0') || (pszParam[i] > L'9')) {
						break;
					}
				}
				if ((i == 14) && (pszParam[14] == L' ')) {
					wcsncpy_s(szOutput, pszParam, 4);
					szOutput[4] = 0;
					st.wYear = StrToInt(szOutput);
					wcsncpy_s(szOutput, pszParam + 4, 2);
					szOutput[2] = 0;
					st.wMonth = StrToInt(szOutput);
					wcsncpy_s(szOutput, pszParam + 6, 2);
					st.wDay = StrToInt(szOutput);
					wcsncpy_s(szOutput, pszParam + 8, 2);
					st.wHour = StrToInt(szOutput);
					wcsncpy_s(szOutput, pszParam + 10, 2);
					st.wMinute = StrToInt(szOutput);
					wcsncpy_s(szOutput, pszParam + 12, 2);
					st.wSecond = StrToInt(szOutput);
					pszParam += 15;
					dw = 1;
				} else {
					dw = 0;
				}
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (dw) {
					if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_WRITE) == 1) {
						hFile = pVFS->CreateFile(strNewVirtual.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
						if (hFile == INVALID_HANDLE_VALUE) {
							swprintf_s(szOutput, L"550 \"%s\": File not found.\r\n", strNewVirtual.c_str());
							SocketSendString(sCmd, szOutput);
						} else {
							SystemTimeToFileTime(&st, &ft);
							SetFileTime(hFile, 0, 0, &ft);
							CloseHandle(hFile);
							SocketSendString(sCmd, L"250 MDTM command successful.\r\n");
						}
					} else {
						swprintf_s(szOutput, L"550 \"%s\": Write permission denied.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_READ) == 1) {
						hFile = pVFS->CreateFile(strNewVirtual.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
						if (hFile == INVALID_HANDLE_VALUE) {
							swprintf_s(szOutput, L"550 \"%s\": File not found.\r\n", strNewVirtual.c_str());
							SocketSendString(sCmd, szOutput);
						} else {
							GetFileTime(hFile, 0, 0, &ft);
							CloseHandle(hFile);
							FileTimeToSystemTime(&ft, &st);
							swprintf_s(szOutput, L"213 %04u%02u%02u%02u%02u%02u\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
							SocketSendString(sCmd, szOutput);
						}
					} else {
						swprintf_s(szOutput, L"550 \"%s\": Read permission denied.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"DELE")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_ADMIN) == 1) {
					if (pVFS->FileExists(strNewVirtual.c_str())) {
						if (pVFS->DeleteFile(strNewVirtual.c_str())) {
							swprintf_s(szOutput, L"250 \"%s\" deleted successfully.\r\n", strNewVirtual.c_str());
							SocketSendString(sCmd, szOutput);
							swprintf_s(szOutput, L"[%u] User \"%s\" deleted \"%s\".", sCmd, strUser.c_str(), strNewVirtual.c_str());
							pLog->Log(szOutput);
						} else {
							swprintf_s(szOutput, L"550 \"%s\": Unable to delete file.\r\n", strNewVirtual.c_str());
							SocketSendString(sCmd, szOutput);
						}
					} else {
						swprintf_s(szOutput, L"550 \"%s\": File not found.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Admin permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd,szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"RNFR")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_ADMIN) == 1) {
					if (pVFS->FileExists(strNewVirtual.c_str())) {
						strRnFr = strNewVirtual;
						swprintf_s(szOutput, L"350 \"%s\": File exists; proceed with RNTO.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					} else {
						swprintf_s(szOutput, L"550 \"%s\": File not found.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Admin permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"RNTO")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else if (strRnFr.length() == 0) {
				SocketSendString(sCmd, L"503 Bad sequence of commands. Send RNFR first.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_ADMIN) == 1) {
					if (pVFS->MoveFile(strRnFr.c_str(), strNewVirtual.c_str())) {
						SocketSendString(sCmd, L"250 RNTO command successful.\r\n");
						swprintf_s(szOutput, L"[%u] User \"%s\" renamed \"%s\" to \"%s\".", sCmd, strUser.c_str(), strRnFr.c_str(), strNewVirtual.c_str());
						pLog->Log(szOutput);
						strRnFr.clear();
					} else {
						swprintf_s(szOutput, L"553 \"%s\": Unable to rename file.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					SocketSendString(sCmd, L"550 Admin permission denied.\r\n");
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"MKD") || !_wcsicmp(szCmd, L"XMKD")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_WRITE) == 1) {
					if (pVFS->CreateDirectory(strNewVirtual.c_str())) {
						swprintf_s(szOutput, L"250 \"%s\" created successfully.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
						swprintf_s(szOutput, L"[%u] User \"%s\" created directory \"%s\".", sCmd, strUser.c_str(), strNewVirtual.c_str());
						pLog->Log(szOutput);
					} else {
						swprintf_s(szOutput, L"550 \"%s\": Unable to create directory.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Write permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}

		else if (!_wcsicmp(szCmd, L"RMD") || !_wcsicmp(szCmd, L"XRMD")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!isLoggedIn) {
				SocketSendString(sCmd, L"530 Not logged in.\r\n");
			} else {
				pVFS->ResolveRelative(strCurrentVirtual.c_str(), pszParam, strNewVirtual);
				if (pPerms->GetPerm(strNewVirtual.c_str(), PERM_ADMIN) == 1) {
					if (pVFS->RemoveDirectory(strNewVirtual.c_str())) {
						swprintf_s(szOutput, L"250 \"%s\" removed successfully.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
						swprintf_s(szOutput, L"[%u] User \"%s\" removed directory \"%s\".", sCmd, strUser.c_str(), strNewVirtual.c_str());
						pLog->Log(szOutput);
					} else {
						swprintf_s(szOutput, L"550 \"%s\": Unable to remove directory.\r\n", strNewVirtual.c_str());
						SocketSendString(sCmd, szOutput);
					}
				} else {
					swprintf_s(szOutput, L"550 \"%s\": Admin permission denied.\r\n", strNewVirtual.c_str());
					SocketSendString(sCmd, szOutput);
				}
			}
		}
		
		else if (!_wcsicmp(szCmd, L"OPTS")) {
			if (!*pszParam) {
				SocketSendString(sCmd, L"501 Syntax error in parameters or arguments.\r\n");
			} else if (!_wcsicmp(pszParam, L"UTF8 On")) {
				SocketSendString(sCmd, L"200 Always in UTF8 mode.\r\n");
			} else {
				SocketSendString(sCmd, L"501 Option not understood.\r\n");
			}
		}

		else {
			swprintf_s(szOutput,L"500 Syntax error, command \"%s\" unrecognized.\r\n",szCmd);
			SocketSendString(sCmd,szOutput);
		}

	}

	if (sPasv) closesocket(sPasv);
	closesocket(sCmd);

	if (isLoggedIn) {
		dwActiveConnections--;
	}

	swprintf_s(szOutput,L"[%u] Connection closed.",sCmd);
	pLog->Log(szOutput);
}

bool SocketSendString(SOCKET s, const wchar_t *psz)
{
	int nWideSize = wcslen(psz);
	int nUtf8Size;
	char *buf;
	bool bSuccess = false;

	nUtf8Size = WideCharToMultiByte(CP_UTF8, 0, psz, nWideSize, NULL, 0, NULL, NULL);
	if (nUtf8Size==0) return false;

	buf = new char[nUtf8Size];
	nUtf8Size = WideCharToMultiByte(CP_UTF8, 0, psz, nWideSize, buf, nUtf8Size, NULL, NULL);
	if (nUtf8Size != 0) {
		bSuccess = send(s, buf, nUtf8Size, 0)!=SOCKET_ERROR;
	}
	delete[] buf;
	return bSuccess;
}

ReceiveStatus SocketReceiveString(SOCKET s, wchar_t *psz, DWORD dwMaxChars, DWORD *pdwCharsReceived)
{
	DWORD dwChars = 0;
	ReceiveStatus status, statusError;
	wchar_t buf[2];
	DWORD dw;

	for (;;) {
		if (dwChars==dwMaxChars) {
			statusError = ReceiveStatus::INSUFFICIENT_BUFFER;
			break;
		}

		status = SocketReceiveLetter(s, psz, dwMaxChars-dwChars, &dw);
		if (status == ReceiveStatus::OK) {
			dwChars += dw;
			if (*psz==L'\r') *psz=0;
			else if (*psz==L'\n') {
				*psz=0;
				*pdwCharsReceived=dwChars;
				return ReceiveStatus::OK;
			}
			psz += dw;
		} else if (status == ReceiveStatus::INVALID_DATA || status == ReceiveStatus::INSUFFICIENT_BUFFER) {
			statusError = status;
			break;
		} else {
			return status;
		}
	}

	// A non-critical error occurred, read until end of line
	for (;;) {
		status = SocketReceiveLetter(s, buf, sizeof(buf)/sizeof(wchar_t), &dw);
		if (status == ReceiveStatus::OK) {
			if (*buf==L'\n') {
				return statusError;
			}
		} else if (status == ReceiveStatus::INVALID_DATA || status == ReceiveStatus::INSUFFICIENT_BUFFER) {
			// Go on...
		} else {
			return status;
		}
	}
}

ReceiveStatus SocketReceiveLetter(SOCKET s, wchar_t *pch, DWORD dwMaxChars, DWORD *pdwCharsReceived)
{
	char buf[4];
	DWORD dwCharLength;
	DWORD dw;
	TIMEVAL tv;
	fd_set fds;

	tv.tv_sec = dwCommandTimeout;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(s, &fds);
	dw = select(0, &fds, 0, 0, &tv);
	if (dw == SOCKET_ERROR || dw == 0) return ReceiveStatus::TIMEOUT;
	dw = recv(s, &buf[0], 1, 0);
	if (dw == SOCKET_ERROR || dw == 0) return ReceiveStatus::NETWORK_ERROR;

	if ((buf[0] & 0x80) == 0x00) { // 0xxxxxxx
		dwCharLength = 1;
	} else if ((buf[0] & 0xE0) == 0xC0) { // 110xxxxx
		dwCharLength = 2;
	} else if ((buf[0] & 0xF0) == 0xE0) { // 1110xxxx
		dwCharLength = 3;
	} else if ((buf[0] & 0xF8) == 0xF0) { // 11110xxx
		dwCharLength = 4;
	} else {
		return ReceiveStatus::INVALID_DATA;
	}

	if (dwCharLength > 1) {
		dw = recv(s, &buf[1], dwCharLength-1, 0);
		if (dw == SOCKET_ERROR || dw == 0) return ReceiveStatus::NETWORK_ERROR;
	}

	if (dwMaxChars == 0) {
		return ReceiveStatus::INSUFFICIENT_BUFFER;
	}

	dw = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, dwCharLength, pch, dwMaxChars);
	if (dw == 0) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			return ReceiveStatus::INSUFFICIENT_BUFFER;
		} else {
			return ReceiveStatus::INVALID_DATA;
		}
	}

	*pdwCharsReceived = dw;
	return ReceiveStatus::OK;
}

ReceiveStatus SocketReceiveData(SOCKET s, char *psz, DWORD dwBytesToRead, DWORD *pdwBytesRead)
{
	DWORD dw;
	TIMEVAL tv;
	fd_set fds;

	tv.tv_sec=dwConnectTimeout;
	tv.tv_usec=0;
	FD_ZERO(&fds);
	FD_SET(s,&fds);
	dw=select(0,&fds,0,0,&tv);
	if (dw==SOCKET_ERROR || dw==0) return ReceiveStatus::TIMEOUT;
	dw=recv(s,psz,dwBytesToRead,0);
	if (dw==SOCKET_ERROR) return ReceiveStatus::NETWORK_ERROR;
	*pdwBytesRead=dw;
	return ReceiveStatus::OK;
}

SOCKET EstablishDataConnection(SOCKADDR_IN *psaiData, SOCKET *psPasv)
{
	SOCKET sData;
	DWORD dw;
	TIMEVAL tv;
	fd_set fds;

	if (psPasv && *psPasv) {
		tv.tv_sec=dwConnectTimeout;
		tv.tv_usec=0;
		FD_ZERO(&fds);
		FD_SET(*psPasv,&fds);
		dw=select(0,&fds,0,0,&tv);
		if (dw && dw!=SOCKET_ERROR) {
			dw=sizeof(SOCKADDR_IN);
			sData=accept(*psPasv,(SOCKADDR *)psaiData,(int *)&dw);
		} else {
			sData=0;
		}
		closesocket(*psPasv);
		*psPasv=0;
		return sData;
	} else {
		sData=socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
		if (connect(sData,(SOCKADDR *)psaiData,sizeof(SOCKADDR_IN))) {
			closesocket(sData);
			return INVALID_SOCKET;
		} else {
			return sData;
		}
	}
}

void LookupHost(const SOCKADDR_IN *sai, wchar_t *pszHostName, size_t stHostName)
// Performs a reverse DNS lookup on ia. If no host name could be resolved, or
// if LookupHosts is Off, pszHostName will contain a string representation of
// the given IP address.
{
	DWORD dw;

	if (bLookupHosts) {
		if (GetNameInfo((SOCKADDR *)sai, sizeof(SOCKADDR_IN), pszHostName, stHostName, NULL, 0, 0) == 0) {
			return;
		}
	}

	dw = stHostName;
	if (WSAAddressToString((SOCKADDR *)sai, sizeof(SOCKADDR_IN), NULL, pszHostName, &dw) == 0) {
		return;
	}

	wcscpy_s(pszHostName, stHostName, L"???");
}

bool DoSocketFileIO(SOCKET sCmd, SOCKET sData, HANDLE hFile, SocketFileIODirection direction, DWORD *pdwAbortFlag)
{
	char szBuffer[PACKET_SIZE];
	wchar_t szCmd[512];
	DWORD dw;

	if (pdwAbortFlag) *pdwAbortFlag = 0;
	switch (direction) {
	case SocketFileIODirection::SEND:
		for (;;) {
			if (!ReadFile(hFile, szBuffer, PACKET_SIZE, &dw, 0)) return false;
			if (!dw) return true;
			if (send(sData, szBuffer, dw, 0) == SOCKET_ERROR) return false;
			ioctlsocket(sCmd, FIONREAD, &dw);
			if (dw) {
				if (SocketReceiveString(sCmd, szCmd, ARRAYSIZE(szCmd), &dw) == ReceiveStatus::OK) {
					if (!_wcsicmp(szCmd, L"ABOR")) {
						*pdwAbortFlag = 1;
						return false;
					} else {
						SocketSendString(sCmd, L"500 Only command allowed at this time is ABOR.\r\n");
					}
				}
			}
		}
		break;
	case SocketFileIODirection::RECEIVE:
		for (;;) {
			if (SocketReceiveData(sData, szBuffer, PACKET_SIZE, &dw) != ReceiveStatus::OK) return false;
			if (dw == 0) return true;
			if (!WriteFile(hFile, szBuffer, dw, &dw, 0)) return false;
		}
		break;
	default:
		return false;
	}
}

bool FileSkipBOM(HANDLE hFile)
{
	DWORD dw, dwBytesRead;
	wchar_t chBOM;

	if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
		return false;
	}

	dw = ReadFile(hFile, &chBOM, sizeof(wchar_t), &dwBytesRead, 0);
	if (!dw) {
		return false;
	}

	if (dwBytesRead > 0) {
		if (dwBytesRead < sizeof(wchar_t) || chBOM!=0xFEFF) {
			SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
		}
	}

	return true;
}

DWORD FileReadLine(HANDLE hFile, wchar_t *pszBuf, DWORD dwBufLen)
{
// Reads a line from an open text file into a character buffer, discarding the
// trailing CR/LF, up to dwBufLen bytes. Any additional bytes are discarded.
// Returns the number of characters in the line, excluding the CR/LF, or -1 if
// the end of the file was reached. May be greater than dwBufLen to indicate
// that bytes were discarded. Note that a return value of 0 does not
// necessarily indicate an error; it could mean a blank line was read.

	DWORD dw, dwBytesRead, dwCount;

	for (dwCount=0;;) {
		dw=ReadFile(hFile,pszBuf,sizeof(wchar_t),&dwBytesRead,0);
		if (!dw || (dw && !dwBytesRead && !dwCount)) return -1;
		if (dwBytesRead!=sizeof(wchar_t) || *pszBuf==L'\n') break;
		if (*pszBuf!=L'\r') {
			dwCount++;
			if (dwCount<dwBufLen) pszBuf++;
		}
	}
	*pszBuf=0;

	return dwCount;
}

DWORD SplitTokens(wchar_t *pszIn)
{
// Processes a string into a null-separated list of its tokens. A quoted
// substring will be treated as a single token. Backslashes may be used to
// escape special characters within quoted substrings.
// Returns the number of tokens split from the string.

	DWORD dwCount=0;
	wchar_t *pszOut;

	pszOut=pszIn;
	while (*pszIn==L' ' || *pszIn==L'\t') pszIn++;
	while (*pszIn) {
		dwCount++;
		if (*pszIn==L'"') {
			pszIn++;
			for (;;pszOut++,pszIn++) {
				if (*pszIn==L'\\') {
					pszIn++;
					if (*pszIn==L'n') *pszIn=L'\n';
					else if (*pszIn==L'r') *pszIn=L'\r';
					else if (*pszIn==L't') *pszIn=L'\t';
					*pszOut=*pszIn;
					continue;
				} else if (!*pszIn || *pszIn==L'"') {
					break;
				} else {
					*pszOut=*pszIn;
				}
			}
			pszIn++;
		} else {
			do {
				*pszOut=*pszIn;
				pszOut++;
				pszIn++;
			} while (*pszIn && *pszIn!=L' ' && *pszIn!=L'\t');
		}
		while (*pszIn==L' ' || *pszIn==L'\t') pszIn++;
		*pszOut=0;
		pszOut++;
	}
	*pszOut=0;
	return dwCount;
}

const wchar_t *GetToken(const wchar_t *pszTokens, DWORD dwToken) {
// Returns a pointer to the one-based dwToken'th null-separated token in
// pszTokens.

	DWORD dw;

	for (dw=1;dw<dwToken;dw++,pszTokens++) {
		pszTokens+=wcslen(pszTokens);
	}
	return pszTokens;
}

IpAddressType GetIPAddressType(IN_ADDR ia)
// Returns one of the predefined IP address types, according to ia.
{
	if (((ia.S_un.S_un_b.s_b1 == 192) && (ia.S_un.S_un_b.s_b2 == 168)) || ((ia.S_un.S_un_b.s_b1 == 169) && (ia.S_un.S_un_b.s_b2 == 254)) || (ia.S_un.S_un_b.s_b1 == 10)) {
		return IpAddressType::LAN;
	} else if ((ia.S_un.S_un_b.s_b1 == 127) && (ia.S_un.S_un_b.s_b2 == 0) && (ia.S_un.S_un_b.s_b3 == 0) && (ia.S_un.S_un_b.s_b4 == 1)) {
		return IpAddressType::LOCAL;
	} else {
		return IpAddressType::WAN;
	}
}

inline bool CanUserLogin(const wchar_t *pszUser, IN_ADDR iaPeer)
{
	return (dwActiveConnections < dwMaxConnections);
}