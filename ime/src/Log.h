// Log.h - optional debug log (HKCU\Software\naggy_win\DebugLog = 1).
#pragma once
namespace naggy_win {
void SetLogEnabled(bool b);
bool LogEnabled();
void Log(const wchar_t* fmt, ...);
}  // namespace naggy_win
