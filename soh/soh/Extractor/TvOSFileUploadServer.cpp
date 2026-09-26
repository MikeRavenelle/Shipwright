#ifdef __TVOS__

#include "TvOSFileUploadServer.h"
#include "TvOSFileHelper.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <vector>

#include <spdlog/spdlog.h>

static constexpr int kPort = 8080;
static constexpr size_t kMaxUpload = 512 * 1024 * 1024;

static std::string GetLocalIPAddress() {
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return "127.0.0.1";

    std::string best;
    for (auto* ifa = addrs; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        auto* s = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &s->sin_addr, buf, sizeof(buf));
        std::string ip(buf);
        if (ip == "127.0.0.1") continue;
        best = ip;
    }
    freeifaddrs(addrs);
    return best.empty() ? "127.0.0.1" : best;
}

static const char* kUploadPage = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ship of Harkinian – ROM Upload</title>
<style>
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;display:flex;
     flex-direction:column;align-items:center;justify-content:center;min-height:100vh;margin:0}
h1{color:#f7c948}
.box{background:#16213e;border-radius:12px;padding:2rem 3rem;text-align:center;
     box-shadow:0 4px 24px #0006}
label{display:block;margin:1rem 0 0.4rem;font-size:0.9rem;color:#aaa}
input[type=file]{padding:0.5rem;background:#0f3460;border:1px solid #444;
                 border-radius:6px;color:#eee;width:100%}
button{margin-top:1.2rem;padding:0.8rem 2.5rem;background:#f7c948;color:#1a1a2e;
       border:none;border-radius:8px;font-size:1rem;font-weight:700;cursor:pointer}
button:hover{background:#ffd966}
.note{margin-top:1rem;font-size:0.8rem;color:#888}
progress{width:100%;margin-top:1rem;height:1rem;display:none}
</style>
</head>
<body>
<div class="box">
  <h1>&#9812; Ship of Harkinian</h1>
  <p>Upload your <strong>oot.o2r</strong> or <strong>oot-mq.o2r</strong> ROM archive.</p>
  <form id="f" method="POST" action="/upload" enctype="multipart/form-data">
    <label for="rom">Select file</label>
    <input type="file" id="rom" name="romfile" accept=".o2r" required>
    <button type="submit">Upload</button>
  </form>
  <progress id="p"></progress>
  <p class="note">File is transferred over your local Wi-Fi network.<br>
  Only oot.o2r and oot-mq.o2r are accepted.</p>
</div>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  var fd=new FormData(this);
  var p=document.getElementById('p');
  p.style.display='block';
  var x=new XMLHttpRequest();
  x.open('POST','/upload');
  x.upload.onprogress=function(ev){if(ev.lengthComputable)p.value=ev.loaded/ev.total;};
  x.onload=function(){document.body.innerHTML='<div class="box"><h1>&#10003; Done!</h1><p>'+
    x.responseText+'</p><p>You can close this page.</p></div>';};
  x.send(fd);
});
</script>
</body>
</html>
)html";

static bool ReadHeaders(int fd, std::string& out) {
    char tmp[4096];
    while (out.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        out.append(tmp, n);
        if (out.size() > 256 * 1024) return false;
    }
    return true;
}

static bool ReadExact(int fd, std::string& buf, size_t needed) {
    char tmp[65536];
    while (needed > 0) {
        size_t chunk = std::min(needed, sizeof(tmp));
        ssize_t n = recv(fd, tmp, chunk, 0);
        if (n <= 0) return false;
        buf.append(tmp, n);
        needed -= static_cast<size_t>(n);
    }
    return true;
}

static std::string HeaderValue(const std::string& headers, const std::string& key) {
    auto pos = headers.find(key + ":");
    if (pos == std::string::npos) return {};
    pos += key.size() + 1;
    while (pos < headers.size() && headers[pos] == ' ') ++pos;
    auto end = headers.find("\r\n", pos);
    return headers.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

static void SendResponse(int fd, int code, const char* codeStr,
                         const std::string& body, const char* ct = "text/plain") {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << code << " " << codeStr << "\r\n"
       << "Content-Type: " << ct << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    auto s = ss.str();
    send(fd, s.c_str(), s.size(), 0);
}

TvOSFileUploadServer::TvOSFileUploadServer(std::string destDir)
    : mDestDir(std::move(destDir)) {}

TvOSFileUploadServer::~TvOSFileUploadServer() { Stop(); }

void TvOSFileUploadServer::Start() {
    if (mRunning.load()) return;

    mListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mListenFd < 0) {
        SPDLOG_ERROR("[TvOSUpload] socket() failed: {}", strerror(errno));
        return;
    }

    int yes = 1;
    setsockopt(mListenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kPort);

    if (bind(mListenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("[TvOSUpload] bind() failed: {}", strerror(errno));
        close(mListenFd);
        mListenFd = -1;
        return;
    }
    listen(mListenFd, 4);

    std::string ip = GetLocalIPAddress();
    mServerURL = "http://" + ip + ":" + std::to_string(kPort);
    SPDLOG_INFO("[TvOSUpload] Listening on {}", mServerURL);

    mRunning.store(true);
    mThread = std::thread(&TvOSFileUploadServer::AcceptLoop, this);
}

void TvOSFileUploadServer::Stop() {
    if (!mRunning.load()) return;
    mRunning.store(false);
    if (mListenFd >= 0) {
        shutdown(mListenFd, SHUT_RDWR);
        close(mListenFd);
        mListenFd = -1;
    }
    if (mThread.joinable()) mThread.join();
}

void TvOSFileUploadServer::AcceptLoop() {
    while (mRunning.load()) {
        struct sockaddr_in client{};
        socklen_t len = sizeof(client);
        int cfd = accept(mListenFd, reinterpret_cast<struct sockaddr*>(&client), &len);
        if (cfd < 0) break;
        HandleClient(cfd);
        close(cfd);
    }
}

void TvOSFileUploadServer::HandleClient(int clientFd) {
    struct timeval tv{ 300, 0 };
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string raw;
    raw.reserve(4096);
    if (!ReadHeaders(clientFd, raw)) {
        SendResponse(clientFd, 400, "Bad Request", "Malformed request");
        return;
    }
    auto sep = raw.find("\r\n\r\n");
    std::string headers = raw.substr(0, sep);
    std::string body = raw.substr(sep + 4);

    auto lf = headers.find("\r\n");
    std::string requestLine = headers.substr(0, lf);

    if (requestLine.find("GET") == 0) {
        SendResponse(clientFd, 200, "OK", kUploadPage, "text/html; charset=utf-8");
        return;
    }

    if (requestLine.find("POST") != 0 || requestLine.find("/upload") == std::string::npos) {
        SendResponse(clientFd, 404, "Not Found", "Not found");
        return;
    }

    std::string clStr = HeaderValue(headers, "Content-Length");
    if (clStr.empty()) {
        SendResponse(clientFd, 411, "Length Required", "Content-Length required");
        return;
    }
    size_t contentLength = std::stoull(clStr);
    if (contentLength > kMaxUpload) {
        SendResponse(clientFd, 413, "Payload Too Large", "File too large (max 512 MB)");
        return;
    }
    if (body.size() < contentLength) {
        body.reserve(contentLength);
        if (!ReadExact(clientFd, body, contentLength - body.size())) {
            SendResponse(clientFd, 400, "Bad Request", "Incomplete upload");
            return;
        }
    }

    std::string contentType = HeaderValue(headers, "Content-Type");
    auto bpos = contentType.find("boundary=");
    if (bpos == std::string::npos) {
        SendResponse(clientFd, 400, "Bad Request", "No multipart boundary");
        return;
    }
    std::string boundary = "--" + contentType.substr(bpos + 9);

    if (SaveUploadedFile(body, boundary)) {
        SendResponse(clientFd, 200, "OK",
                     "File uploaded successfully! You can now close this page and start playing.");
    } else {
        SendResponse(clientFd, 400, "Bad Request",
                     "Upload failed. Make sure you upload oot.o2r or oot-mq.o2r.");
    }
}

bool TvOSFileUploadServer::SaveUploadedFile(const std::string& body, const std::string& boundary) {
    auto partStart = body.find(boundary);
    if (partStart == std::string::npos) return false;
    partStart += boundary.size() + 2;

    auto partHeaderEnd = body.find("\r\n\r\n", partStart);
    if (partHeaderEnd == std::string::npos) return false;

    std::string partHeaders = body.substr(partStart, partHeaderEnd - partStart);
    std::string fileData = body.substr(partHeaderEnd + 4);

    auto tailBoundary = fileData.rfind(boundary);
    if (tailBoundary != std::string::npos) {
        if (tailBoundary >= 2) tailBoundary -= 2;
        fileData.resize(tailBoundary);
    }

    auto fnPos = partHeaders.find("filename=\"");
    if (fnPos == std::string::npos) return false;
    fnPos += 10;
    auto fnEnd = partHeaders.find("\"", fnPos);
    std::string filename = partHeaders.substr(fnPos, fnEnd - fnPos);

    if (filename != "oot.o2r" && filename != "oot-mq.o2r") {
        SPDLOG_WARN("[TvOSUpload] Rejected file: {}", filename);
        return false;
    }

    SPDLOG_INFO("[TvOSUpload] Writing {} bytes (filename: {})", fileData.size(), filename);

    if (!TvOSWriteFile(mDestDir, filename, fileData.data(), fileData.size())) {
        return false;
    }

    SPDLOG_INFO("[TvOSUpload] Saved {} ({} bytes)", filename, fileData.size());
    mReceivedFilename = filename;
    mFileReceived.store(true);
    return true;
}

#endif
