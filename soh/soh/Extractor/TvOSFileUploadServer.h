#pragma once
#ifdef __TVOS__
#include <atomic>
#include <functional>
#include <string>
#include <thread>

class TvOSFileUploadServer {
public:
    explicit TvOSFileUploadServer(std::string destDir);
    ~TvOSFileUploadServer();

    void Start();
    void Stop();

    bool IsFileReceived() const { return mFileReceived.load(); }
    std::string ReceivedFilename() const { return mReceivedFilename; }
    std::string GetServerURL() const { return mServerURL; }

private:
    std::string mDestDir;
    std::string mServerURL;
    std::string mReceivedFilename;
    std::atomic<bool> mRunning{ false };
    std::atomic<bool> mFileReceived{ false };
    int mListenFd{ -1 };
    std::thread mThread;

    void AcceptLoop();
    void HandleClient(int clientFd);
    bool SaveUploadedFile(const std::string& body, const std::string& boundary);
};

#endif
