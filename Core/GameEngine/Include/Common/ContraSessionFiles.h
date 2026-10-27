/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// ContraSessionFiles.h
// Contra: the per-match statistics files. PerfStats and NetworkStats used to be appended to one file each that only
// grew; now every match writes its own pair, named with the time the match started, in the Stats folder of the user
// data folder: Stats\PerfStats-<date>-<time>.txt and Stats\NetworkStats-<date>-<time>.txt. The newest files are kept,
// older ones are deleted when a new match starts.

#pragma once

#include "Common/GlobalData.h"
#include "Common/AsciiString.h"

#include <algorithm>
#include <string>
#include <vector>

namespace contra
{
	// Keeps the newest keepCount files matching pattern (e.g. "PerfStats-*.txt") in directory (with a trailing
	// backslash). The file names must start with a sortable date and time after the prefix.
	inline void keepNewestFiles(const char *directory, const char *pattern, size_t keepCount)
	{
		std::vector<std::string> names;
		WIN32_FIND_DATA findData;
		HANDLE hFind = ::FindFirstFile((std::string(directory) + pattern).c_str(), &findData);
		if (hFind == INVALID_HANDLE_VALUE)
			return;
		do
		{
			if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				names.push_back(findData.cFileName);
		} while (::FindNextFile(hFind, &findData));
		::FindClose(hFind);

		std::sort(names.begin(), names.end());
		for (size_t i = 0; i + keepCount < names.size(); ++i)
		{
			::DeleteFile((std::string(directory) + names[i]).c_str());
		}
	}

	inline char *matchStampBuffer()
	{
		static char stamp[32] = "";
		return stamp;
	}

	// Called when a match (not the shell map) starts: the statistics of this match go to new files.
	inline void startStatsSession()
	{
		SYSTEMTIME st;
		::GetLocalTime(&st);
		snprintf(matchStampBuffer(), 32, "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		if (TheGlobalData != nullptr)
		{
			AsciiString dir = TheGlobalData->getPath_UserData();
			dir.concat("Stats\\");
			::CreateDirectory(dir.str(), nullptr);
			keepNewestFiles(dir.str(), "PerfStats-*.txt", 30);
			keepNewestFiles(dir.str(), "NetworkStats-*.txt", 30);
		}
	}

	// Path of the statistics file "name" (PerfStats or NetworkStats) of the current match.
	inline AsciiString statsFilePath(const char *name)
	{
		if (matchStampBuffer()[0] == '\0')
			startStatsSession();

		AsciiString path = TheGlobalData->getPath_UserData();
		path.concat("Stats\\");
		path.concat(name);
		path.concat("-");
		path.concat(matchStampBuffer());
		path.concat(".txt");
		return path;
	}
}
