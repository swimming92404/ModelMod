// ModelMod: 3d data snapshotting & substitution program.
// Copyright(C) 2015,2016 John Quigley

// This program is free software : you can redistribute it and / or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with this program.If not, see <http://www.gnu.org/licenses/>.

#include "Log.h"

#include <windows.h> // for OutputDebugStringA

#include <cassert>

ModelMod::Log* ModelMod::Log::_sInstance = NULL;
namespace ModelMod {

Log::Log(void) :
	_level(Log::LOG_INFO), 
	_outputDebug(true),
	_outputFile(true),
	_fileReopen(true),
	_firstOpen(true),
	_fout(NULL) {
	memset(&_critSection, 0, sizeof(_critSection));
	InitializeCriticalSection(&_critSection);
}

Log::~Log(void) {
}

Log& Log::get() {
	// Note, we don't have the critical section yet, so we can't protect the lazy 
	// initialization from multiple threads.  However, this is
	// called very early from a single thread (dllmain Init(); aka the hook thread).  
	// Game threads can't even get in here until the hook thread completes at least part of its work, 
	// and that happens after log init.  So, this should be ok.
	if (_sInstance == NULL)
		_sInstance = new Log();
	return *_sInstance;
}

class CriticalSectionHandler {
	LPCRITICAL_SECTION _section;

public:
	CriticalSectionHandler(LPCRITICAL_SECTION section) {
		_section = section;
		EnterCriticalSection(_section);
	}
	virtual ~CriticalSectionHandler() {
		LeaveCriticalSection(_section);
	}
};

void Log::init(HMODULE callingDll) {
	CriticalSectionHandler cSect(&_critSection);

	// init log file in module directory
	wstring sBaseDir = L"";
	{
		wchar_t baseModDirectory[8192];
		GetModuleFileNameW(callingDll, baseModDirectory, sizeof(baseModDirectory));
		wchar_t* lastBS = wcsrchr(baseModDirectory, '\\');
		if (lastBS != NULL) {
			*lastBS = 0;
		}
		sBaseDir = baseModDirectory;
	}

	// include the name of the executable in the log file name
	wstring sExeName = L"unknownexe";
	{
		wchar_t exeName[8192];
		GetModuleFileNameW(NULL, exeName, sizeof(exeName));
		wchar_t* lastBS = wcsrchr(exeName, '\\');
		if (lastBS != NULL) {
			lastBS = lastBS++;
			if (*lastBS != NULL) {
				sExeName = wstring(lastBS);
			}
		}
	}

	// setup log directory: we hardcode it to be <dllpath>\..\Logs
	wstring sLogDir = sBaseDir + L"\\..\\Logs";
	DWORD dirAttr = GetFileAttributesW(sLogDir.c_str());
	if (dirAttr == INVALID_FILE_ATTRIBUTES) {
		_wmkdir(sLogDir.c_str());
	}
	
	_logFilePath = sLogDir + L"\\modelmod." + sExeName + L".log";
}

void Log::info(string message, string category, int cap) {
	_do_log(Log::LOG_INFO, message,category, cap);
}

void Log::setCategoryLevel(string category, int level) {
	CriticalSectionHandler cSect(&_critSection);

	_categoryLevel[category] = level;
}

int Log::getCategoryLevel(string category) {
	CriticalSectionHandler cSect(&_critSection); // this function is read-only, but map not guaranteed to be thread-safe

	map<string,int>::iterator iter = _categoryLevel.find(category);

	if (iter != _categoryLevel.end())
		return iter->second;
	else
		return -1;
}

void Log::_do_log(int level, const string& message, const string& category, int limit) {
	CriticalSectionHandler cSect(&_critSection);

	string messageSuffix = "";
	if (limit > 0) {
		int currCount = _limitedMessages[message];
		currCount++;
		// lets handle (unlikely) rollover gracefully 
		if (currCount < 0) {
			currCount = limit+1;
		}

		_limitedMessages[message] = currCount;

		if (currCount == limit) {
			messageSuffix = message + " (Final message; log limit hit)";
		}
		if (currCount > limit) {
			return;
		}
	}

	int catLevel = getCategoryLevel(category);
	if (catLevel != -1 && level < catLevel) 
		return;

	if (level < _level) 
		return;

	string lMsg = "[" + category + "]: " + message + messageSuffix;
	if (_outputDebug)
		_output_debug_string(lMsg);
	if (_outputFile)
		_output_file_string(lMsg);
}
	
void Log::_output_debug_string(const string& msg) {
	OutputDebugStringA(msg.c_str());
	OutputDebugStringA("\r\n");
}

void Log::_output_file_string(const string& msg) {
	CriticalSectionHandler cSect(&_critSection);

	if (_logFilePath.empty()) {
		// init with default settings
		init(NULL);
	}
	if (!_fout || _fileReopen) {
		// about to (re)open, handle should be null
		assert(_fout == NULL);

		if (_firstOpen)
			_wfopen_s(&_fout, _logFilePath.c_str(), L"w");
		else
			_wfopen_s(&_fout, _logFilePath.c_str(), L"a");
		_firstOpen = false;
	}

	if (_fout) {
		fputs(msg.c_str(), _fout);
		fputs("\n", _fout);
		if (_fileReopen) {
			fclose(_fout);
			_fout = NULL;
		}
		else {
			// leave file open
			//fflush(_fout); // SLOWWWW
		}
	}
}

};