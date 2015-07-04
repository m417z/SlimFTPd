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

#ifndef _INCL_VFS_H
#define _INCL_VFS_H

#include <windows.h>
#include <map>
#include <string>
#include "tree.h"

using namespace std;

class VFS
{
private:
	struct MOUNTPOINT {
		wstring strVirtual;
		wstring strLocal;
	};
	struct FINDDATA {
		wstring strVirtual;
		wstring strFilespec;
		HANDLE hFind;
		tree<MOUNTPOINT> *ptree;
	};

	tree<MOUNTPOINT> _root;

	static DWORD Map(const wchar_t *pszVirtual, wstring &strLocal, tree<MOUNTPOINT> *ptree);
	static tree<MOUNTPOINT> * FindMountPoint(const wchar_t *pszVirtual, tree<MOUNTPOINT> *ptree);
	static bool WildcardMatch(const wchar_t *pszFilespec, const wchar_t *pszFilename);
	static void GetMountPointFindData(tree<MOUNTPOINT> *ptree, WIN32_FIND_DATA *pw32fd);

public:
	typedef map<wstring, wstring> listing_type;
	VFS();
	void Mount(const wchar_t *pszVirtual, const wchar_t *pszLocal);
	DWORD GetDirectoryListing(const wchar_t *pszVirtual, DWORD dwIsNLST, listing_type &listing);
	bool FileExists(const wchar_t *pszVirtual);
	bool IsFolder(const wchar_t *pszVirtual);
	LPVOID FindFirstFile(const wchar_t *pszVirtual, WIN32_FIND_DATA *pw32fd);
	bool FindNextFile(LPVOID lpFindHandle, WIN32_FIND_DATA *pw32fd);
	void FindClose(LPVOID lpFindHandle);
	HANDLE CreateFile(const wchar_t *pszVirtual, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition);
	BOOL DeleteFile(const wchar_t *pszVirtual);
	BOOL MoveFile(const wchar_t *pszOldVirtual, const wchar_t *pszNewVirtual);
	BOOL CreateDirectory(const wchar_t *pszVirtual);
	BOOL RemoveDirectory(const wchar_t *pszVirtual);
	static void CleanVirtualPath(const wchar_t *pszVirtual, wstring &strNewVirtual);
	static void ResolveRelative(const wchar_t *pszCurrentVirtual, const wchar_t *pszRelativeVirtual, wstring &strNewVirtual);
};

#endif