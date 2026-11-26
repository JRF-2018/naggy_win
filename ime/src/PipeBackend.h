// PipeBackend.h - IBackend over the naggy-server named pipe.
#pragma once
#include "Engine.h"
#include "Globals.h"

namespace naggy_win {

class PipeBackend : public naggy::IBackend {
public:
    PipeBackend();
    ~PipeBackend() override;
    bool Command(const std::vector<std::wstring>& args, naggy::Response& r) override;
    void Disconnect();
    // Disables launching naggy-server (e.g. in secure mode).
    void SetAllowLaunch(bool b) { allowLaunch_ = b; }
    static std::wstring PipeName();

private:
    bool Connect();
    bool LaunchServer();
    bool WriteAll(const std::string& s, DWORD timeoutMs);
    bool ReadLine(std::string& line, DWORD timeoutMs);
    bool TryCommand(const std::string& line, naggy::Response& r);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
    std::string rbuf_;
    bool allowLaunch_ = true;
    ULONGLONG lastLaunch_ = 0;
};

}  // namespace naggy_win
