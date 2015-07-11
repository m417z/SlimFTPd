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

#include "vfs.h"
#include <algorithm>
#include "tree.h"
#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>

VFS::VFS()
{
}

void VFS::Mount(const wchar_t *pszVirtual, const wchar_t *pszLocal)
// Creates a new mount point in the virtual file system.
{
	tree<MOUNTPOINT> *ptree, *pparent;

	ptree = &_root;
	size_t i = 0;
	wstring dir;
	while (pszVirtual[i] != 0) {
		++i;
		if (pszVirtual[i] == 0) break;
		size_t j = wcscspn(pszVirtual + i, L"/");
		dir.assign(pszVirtual + i, j);
		pparent = ptree;
		ptree = ptree->_pdown;
		while (ptree && _wcsicmp(ptree->_data.strVirtual.c_str(), dir.c_str())) ptree = ptree->_pright;
		if (!ptree) {
			ptree = new tree<MOUNTPOINT>(pparent);
			ptree->_data.strVirtual = dir;
		}
		i += j;
	}
	ptree->_data.strLocal = pszLocal;
}

DWORD VFS::GetDirectoryListing(const wchar_t *pszVirtual, DWORD dwIsNLST, listing_type &listing)
// Fills a map class with lines comprising an FTP-style directory listing.
// If dwIsNLST is non-zero, will return filenames only.
{
	wchar_t szLine[512];
	const wchar_t *pszMonthAbbr[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};
	LPVOID hFind;
	WIN32_FIND_DATA w32fd;
	SYSTEMTIME stCutoff, stFile;

	if (IsFolder(pszVirtual)) {
		wstring str;
		ResolveRelative(pszVirtual, L"*", str);
		hFind = FindFirstFile(str.c_str(), &w32fd);
	}
	else {
		hFind = FindFirstFile(pszVirtual, &w32fd);
	}
	if (hFind) {
		GetSystemTime(&stCutoff);
		stCutoff.wYear--;
		do {
			if (!wcscmp(w32fd.cFileName, L".") || !wcscmp(w32fd.cFileName, L"..")) continue;
			FileTimeToSystemTime(&w32fd.ftLastWriteTime, &stFile);
			if (dwIsNLST) {
				wcscpy_s(szLine, w32fd.cFileName);
				if (w32fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					wcscat_s(szLine, L"/");
				}
			} else {
				wsprintf(szLine, L"%c--------- 1 ftp ftp %10u %s %2u ", (w32fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L'd' : L'-', w32fd.nFileSizeLow, pszMonthAbbr[stFile.wMonth-1], stFile.wDay);
				if ((stFile.wYear > stCutoff.wYear) || ((stFile.wYear == stCutoff.wYear) && ((stFile.wMonth > stCutoff.wMonth) || ((stFile.wMonth == stCutoff.wMonth) && (stFile.wDay > stCutoff.wDay))))) {
					wsprintf(szLine + wcslen(szLine), L"%.2u:%.2u ", stFile.wHour, stFile.wMinute);
				} else {
					wsprintf(szLine + wcslen(szLine), L"%5u ", stFile.wYear);
				}
				wcscat_s(szLine, w32fd.cFileName);
			}
			wcscat_s(szLine,L"\r\n");
			listing_type::iterator it = listing.find(w32fd.cFileName);
			if (it != listing.end()) {
				it->second = szLine;
			}
			else {
				listing.insert(std::make_pair(w32fd.cFileName, szLine));
			}
		} while (FindNextFile(hFind, &w32fd));
		FindClose(hFind);
		return 1;
	} else {
		return 0;
	}
}

DWORD VFS::Map(const wchar_t *pszVirtual, wstring &strLocal, tree<MOUNTPOINT> *ptree)
// Recursive function to map a virtual path to a local path.
{
	const wchar_t *psz;
	UINT_PTR dwLen;

	psz = wcschr(pszVirtual, L'/');
	if (psz) dwLen = psz - pszVirtual;
	else dwLen = wcslen(pszVirtual);
	while (ptree) {
		if ((ptree->_data.strVirtual.length() == dwLen) && (!dwLen || !_wcsnicmp(pszVirtual, ptree->_data.strVirtual.c_str(), dwLen))) {
			if (psz) {
				if (Map(psz + 1, strLocal, ptree->_pdown)) return 1;
				else {
					if (ptree->_data.strLocal.length() != 0) {
						strLocal = ptree->_data.strLocal;
						strLocal += psz;
						replace(strLocal.begin(), strLocal.end(), L'/', L'\\');
						return 1;
					} else {
						return 0;
					}
				}
			} else {
				strLocal = ptree->_data.strLocal;
				return 1;
			}
		} else {
			ptree = ptree->_pright;
		}
	}
	strLocal.clear();
	return 0;
} 

tree<VFS::MOUNTPOINT> * VFS::FindMountPoint(const wchar_t *pszVirtual, tree<MOUNTPOINT> *ptree)
// Returns a pointer to the tree node described by pszVirtual, or 0.
{
	const wchar_t *psz;
	UINT_PTR dwLen;

	if (!wcscmp(pszVirtual, L"/")) return ptree;
	psz = wcschr(pszVirtual, L'/');
	if (psz) dwLen = psz - pszVirtual;
	else dwLen = wcslen(pszVirtual);
	while (ptree) {
		if ((ptree->_data.strVirtual.length() == dwLen) && (!dwLen || !_wcsnicmp(pszVirtual, ptree->_data.strVirtual.c_str(), dwLen))) {
			if (psz) {
				return FindMountPoint(psz + 1, ptree->_pdown);
			} else {
				return ptree;
			}
		} else {
			ptree = ptree->_pright;
		}
	}
	return 0;
}

void VFS::CleanVirtualPath(const wchar_t *pszVirtual, wstring &strNewVirtual)
// Strips utter rubbish out of a virtual path.
// Ex: /home/./user//...\ftp/  =>  /home/ftp
{
	const wchar_t *in = pszVirtual;
	wchar_t *buf = new wchar_t[wcslen(pszVirtual) + 4];
	buf[0] = L'\0'; buf[1] = L'\0'; buf[2] = L'\0';
	wchar_t *out = buf + 3;
	do {
		*out = *in;
		if (*out == L'\\') *out = L'/'; // convert backslashes to forward slashes
		if ((*out == L'\0') || (*out == L'/')) {
			if (out[-1] == L'.') { // output ends with "."
				if (out[-2] == L'\0') --out; // entire output is "."
				else if (out[-2] == L'/') { // output ends with "/."
					if (out[-3] == L'\0') --out; // entire output is "/."
					else out -= 2;
				}
				else if (out[-2] == L'.') { // output ends with ".."
					if (out[-3] == L'\0') out -= 2; // entire output is ".."
					else if (out[-3] == L'/') { // output ends with "/.."
						if (out[-4] == L'\0') out -= 2; // entire output is "/.."
						else {
							out -= 3;
							while ((out[-1] != L'\0') && (out[-1] != L'/')) --out;
						}
					}
				}
				else ++in;
			}
			else {
				++in;
				if (out[-1] != L'/') ++out;
			}
		}
		else ++in, ++out;
	} while (in[-1] != L'\0');
	strNewVirtual = buf + 3;
	delete[] buf;
}

void VFS::ResolveRelative(const wchar_t *pszCurrentVirtual, const wchar_t *pszRelativeVirtual, wstring &strNewVirtual)
// Concatenates pszRelativeVirtual to pszCurrentVirtual and resolves.
{
	if (*pszRelativeVirtual!=L'/') {
		strNewVirtual = pszCurrentVirtual;
		strNewVirtual += L"/";
		strNewVirtual += pszRelativeVirtual;
		CleanVirtualPath(strNewVirtual.c_str(), strNewVirtual);
	}
	else {
		CleanVirtualPath(pszRelativeVirtual, strNewVirtual);
	}
}

bool VFS::WildcardMatch(const wchar_t *pszFilespec, const wchar_t *pszFilename)
// Returns true iff pszFilename matches wildcard pattern pszFilespec.
{
	if (*pszFilespec == 0) return true;
	while (*pszFilespec) {
		if (*pszFilespec == L'*') {
			pszFilespec++;
			do {
				if (WildcardMatch(pszFilespec, pszFilename)) return true;
			} while (*pszFilename++);
			return false;
		} else if (((*pszFilespec | 0x20) != (*pszFilename | 0x20)) && (*pszFilespec != L'?')) {
			return false;
		}
		pszFilespec++;
		pszFilename++;
	}
	if (!*pszFilespec && !*pszFilename) return true;
	else return false;
}

LPVOID VFS::FindFirstFile(const wchar_t *pszVirtual, WIN32_FIND_DATA *pw32fd)
// Returns a find handle if a match was found. Otherwise returns 0.
// Supports wildcards.
{
	FINDDATA *pfd;
	const wchar_t *psz;
	wstring str;

	psz = wcsrchr(pszVirtual, L'/');
	if (psz == NULL) return NULL;
	str.assign(pszVirtual, psz);
	pfd = new FINDDATA;
	pfd->hFind = 0;
	pfd->strVirtual = pszVirtual;
	pfd->strFilespec = psz + 1;
	pfd->ptree = FindMountPoint(str.c_str(), &_root);
	if (pfd->ptree) pfd->ptree = pfd->ptree->_pdown;

	if (FindNextFile(pfd, pw32fd)) return pfd;
	else {
		delete pfd;
		return 0;
	}
}

bool VFS::FindNextFile(LPVOID lpFindHandle, WIN32_FIND_DATA *pw32fd)
{
	FINDDATA *pfd = (FINDDATA *)lpFindHandle;
	wstring str;

	while (pfd->ptree) {
		str = pfd->ptree->_data.strVirtual;
		if (str.find_first_of(L'.') == wstring::npos) str.push_back(L'.');
		if (WildcardMatch(pfd->strFilespec.c_str(), str.c_str())) {
			GetMountPointFindData(pfd->ptree, pw32fd);
			pfd->ptree = pfd->ptree->_pright;
			return true;
		}
		pfd->ptree = pfd->ptree->_pright;
	}

	if (pfd->hFind) {
		return ::FindNextFile(pfd->hFind, pw32fd) ? true : false;
	} else {
		if (!Map(pfd->strVirtual.c_str(), str, &_root)) return false;
		if (str.length() != 0) {
			pfd->hFind = ::FindFirstFile(str.c_str(), pw32fd);
			return (pfd->hFind != INVALID_HANDLE_VALUE);
		} else {
			return false;
		}
	}
}

void VFS::FindClose(LPVOID lpFindHandle)
{
	FINDDATA *pfd = (FINDDATA *)lpFindHandle;

	if (pfd->hFind) ::FindClose(pfd->hFind);
	delete pfd;
}

bool VFS::FileExists(const wchar_t *pszVirtual)
// Returns true iff pszVirtual denotes an existing file or folder.
// Supports wildcards.
{
	LPVOID hFind;
	WIN32_FIND_DATA w32fd;

	hFind = FindFirstFile(pszVirtual, &w32fd);
	if (hFind) {
		FindClose(hFind);
		return true;
	} else {
		return false;
	}
}

bool VFS::IsFolder(const wchar_t *pszVirtual)
// Returns true iff pszVirtual denotes an existing folder.
// Does NOT support wildcards.
{
	wstring strLocal;
	DWORD dw;

	if (FindMountPoint(pszVirtual, &_root)) return true;
	if (!Map(pszVirtual, strLocal, &_root)) return true;
	dw = GetFileAttributes(strLocal.c_str());
	return ((dw != INVALID_FILE_ATTRIBUTES) && (dw & FILE_ATTRIBUTE_DIRECTORY));
}

void VFS::GetMountPointFindData(tree<MOUNTPOINT> *ptree, WIN32_FIND_DATA *pw32fd)
// Fills in the WIN32_FIND_DATA structure with data about the mount point.
{
	HANDLE hFind;
	SYSTEMTIME st = {1980, 1, 2, 1, 0, 0, 0, 0};

	if ((ptree->_data.strLocal.length() != 0) && ((hFind = ::FindFirstFile(ptree->_data.strLocal.c_str(), pw32fd)) != INVALID_HANDLE_VALUE)) {
		::FindClose(hFind);
	} else {
		memset(pw32fd, 0, sizeof(WIN32_FIND_DATA));
		pw32fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		SystemTimeToFileTime(&st, &pw32fd->ftLastWriteTime);
	}
	wcscpy_s(pw32fd->cFileName, sizeof(pw32fd->cFileName)/sizeof(wchar_t), ptree->_data.strVirtual.c_str());
}

HANDLE VFS::CreateFile(const wchar_t *pszVirtual, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition)
{
	wstring strLocal;

	if (Map(pszVirtual, strLocal, &_root)) {
		return ::CreateFile(strLocal.c_str(), dwDesiredAccess, dwShareMode, 0, dwCreationDisposition, FILE_FLAG_SEQUENTIAL_SCAN, 0);
	} else {
		return INVALID_HANDLE_VALUE;
	}
}

BOOL VFS::DeleteFile(const wchar_t *pszVirtual)
{
	wstring strLocal;

	return (Map(pszVirtual, strLocal, &_root) && ::DeleteFile(strLocal.c_str()));
}

BOOL VFS::MoveFile(const wchar_t *pszOldVirtual, const wchar_t *pszNewVirtual)
{
	wstring strOldLocal, strNewLocal;

	return (Map(pszOldVirtual, strOldLocal, &_root) && Map(pszNewVirtual, strNewLocal, &_root) && ::MoveFile(strOldLocal.c_str(), strNewLocal.c_str()));
}

BOOL VFS::CreateDirectory(const wchar_t *pszVirtual)
{
	wstring strLocal;

	return (Map(pszVirtual, strLocal, &_root) && ::CreateDirectory(strLocal.c_str(), NULL));
}

BOOL VFS::RemoveDirectory(const wchar_t *pszVirtual)
{
	wstring strLocal;

	return (Map(pszVirtual, strLocal, &_root) && ::RemoveDirectory(strLocal.c_str()));
}
