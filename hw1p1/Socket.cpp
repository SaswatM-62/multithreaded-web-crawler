
#include "pch.h"
#include "Socket.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma comment(lib, "ws2_32.lib")

constexpr int INITIAL_BUF_SIZE = 8192;
constexpr int THRESHOLD = 2048;

// Constructor for socket helper
// Allocates initial receive buffer
// Sets socket and TLS fields to defaults
Socket::Socket() {
    buf = (char*)malloc(INITIAL_BUF_SIZE);
    allocatedSize = INITIAL_BUF_SIZE;
    curPos = 0;

    sock = INVALID_SOCKET;
    timedOut = false;
    lastError = 0;
    bytesDownloaded = 0;

    ip = 0;
    ipStr[0] = '\0';

    sslCtx = NULL;
    ssl = NULL;

    if (buf) {
        buf[0] = '\0';
    }
}

// Destructor for socket helper
// Closes open socket and TLS handles
// Frees receive buffer memory
Socket::~Socket() {
    Close();
    if (buf) {
        free(buf);
        buf = nullptr;
    }
    allocatedSize = 0;
}

// Opens a TCP socket
// Uses IPv4 stream mode
// Stores Winsock error on failure
bool Socket::Open() {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        lastError = WSAGetLastError();
        return false;
    }
    return true;
}

// Resolves host into IPv4 address
// Supports numeric IP and DNS host name
// Saves both numeric and printable IP
bool Socket::ResolveDNS(const char* host) {
    DWORD addr = inet_addr(host);

    if (addr == INADDR_NONE) {
        hostent* h = gethostbyname(host);
        if (!h) {
            lastError = WSAGetLastError();
            return false;
        }
        memcpy(&ip, h->h_addr, h->h_length);
    }
    else {
        ip = addr;
    }

    in_addr a;
    a.S_un.S_addr = ip;
    strcpy(ipStr, inet_ntoa(a));

    return true;
}

// Connects socket to resolved IP and port
// Uses blocking connect call
// Saves Winsock error on failure
bool Socket::Connect(int port) {
    sockaddr_in server;
    memset(&server, 0, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.S_un.S_addr = ip;
    server.sin_port = htons((u_short)port);

    if (connect(sock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        lastError = WSAGetLastError();
        return false;
    }

    return true;
}

// Sends full plain HTTP request bytes
// Handles partial send by looping
// Returns false on send failure
bool Socket::SendAll(const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) {
            lastError = WSAGetLastError();
            return false;
        }
        sent += n;
    }
    return true;
}

// Reads plain HTTP response with limits
// Enforces timeout and max size checks
// Validates that reply starts with HTTP header
bool Socket::ReadWithLimits(int maxBytes, int totalTimeoutMs,
    bool& exceededMax, bool& slowDownload, bool& nonHttp)
{
    // Reset status flags and counters
    exceededMax = false;
    slowDownload = false;
    nonHttp = false;

    timedOut = false;
    lastError = 0;
    bytesDownloaded = 0;

    curPos = 0;
    if (buf && allocatedSize > 0) {
        buf[0] = '\0';
    }

    ULONGLONG start = GetTickCount64();
    bool checkedHttpPrefix = false;

    // Keep reading until done or failure
    while (true) {
        ULONGLONG now = GetTickCount64();
        int elapsedMs = (int)(now - start);
        int remainingMs = totalTimeoutMs - elapsedMs;
        if (remainingMs <= 0) {
            slowDownload = true;
            return false;
        }

        // Wait for socket readiness
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);

        timeval tv;
        int waitMs = (remainingMs < 1000) ? remainingMs : 1000;
        tv.tv_sec = waitMs / 1000;
        tv.tv_usec = (waitMs % 1000) * 1000;

        int ret = select(0, &fds, NULL, NULL, &tv);

        // Read available bytes
        if (ret > 0) {
            int space = allocatedSize - curPos;
            if (space <= 0) {
                int newSize = allocatedSize * 2;
                char* nb = (char*)realloc(buf, newSize);
                if (!nb) {
                    lastError = WSAGetLastError();
                    return false;
                }
                buf = nb;
                allocatedSize = newSize;
                space = allocatedSize - curPos;
            }

            int bytes = recv(sock, buf + curPos, space, 0);

            if (bytes == SOCKET_ERROR) {
                lastError = WSAGetLastError();
                return false;
            }

            if (bytes == 0) {
                buf[curPos] = '\0';
                bytesDownloaded = curPos;

                if (bytesDownloaded < 5 || strncmp(buf, "HTTP/", 5) != 0) {
                    nonHttp = true;
                    return false;
                }
                return true;
            }

            curPos += bytes;

            if (curPos > maxBytes) {
                exceededMax = true;
                return false;
            }

            if (!checkedHttpPrefix && curPos >= 5) {
                checkedHttpPrefix = true;
                if (strncmp(buf, "HTTP/", 5) != 0) {
                    nonHttp = true;
                    return false;
                }
            }

            if (allocatedSize - curPos < THRESHOLD) {
                int newSize = allocatedSize * 2;
                char* nb = (char*)realloc(buf, newSize);
                if (!nb) {
                    lastError = WSAGetLastError();
                    return false;
                }
                buf = nb;
                allocatedSize = newSize;
            }
        }
        // Handle timeout and select error
        else if (ret == 0) {
            timedOut = true;
            lastError = WSAETIMEDOUT;
            return false;
        }
        else {
            lastError = WSAGetLastError();
            return false;
        }
    }
}


// Initializes TLS objects
// Creates SSL context and SSL session
// Attaches current socket handle to SSL
bool Socket::InitTLS() {

    SSL_library_init();
    SSL_load_error_strings();

    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);

    if (!ctx) {
        return false;
    }

    SSL* s = SSL_new(ctx);

    if (!s) {
        SSL_CTX_free(ctx);
        return false;
    }

    if (SSL_set_fd(s, (int)sock) != 1) {
        SSL_free(s);
        SSL_CTX_free(ctx);
        lastError = (int)ERR_get_error();
        return false;
    }

    sslCtx = ctx;
    ssl = s;

    return true;
}

// Runs TLS handshake with remote server
// Should be called after InitTLS
// Returns false when handshake fails
bool Socket::DoTLSHandshake() {

    if (!ssl) {
        return false;
    }

    int rc = SSL_connect((SSL*)ssl);
    if (rc != 1) {
        lastError = SSL_get_error((SSL*)ssl, rc);
        return false;
    }

    return true;
}

// Sends full request over TLS
// Uses SSL_write in loop for partial sends
// Saves SSL error on failure
bool Socket::SendAllHTTPS(const char* data, int len) {

    if (!ssl) {
        return false;
    }

    int sent = 0;

    while (sent < len) {
        int n = SSL_write((SSL*)ssl, data + sent, len - sent);
        if (n <= 0) {
            lastError = SSL_get_error((SSL*)ssl, n);
            return false;
        }
        sent += n;

    }

    return true;
}

// Reads HTTPS response with limits
// Uses SSL_read and checks SSL error codes
// Validates response header and size limits
bool Socket::ReadWithLimitsHTTPS(int maxBytes, int totalTimeoutMs,
    bool& exceededMax, bool& slowDownload, bool& nonHttp)
{
    // Reset status flags and counters
    exceededMax = false;
    slowDownload = false;
    nonHttp = false;

    timedOut = false;
    lastError = 0;
    bytesDownloaded = 0;

    curPos = 0;

    if (buf && allocatedSize > 0) {
        buf[0] = '\0';
    }

    ULONGLONG start = GetTickCount64();
    bool checkedHttpPrefix = false;

    // Keep reading until done or failure
    while (true) {

        ULONGLONG now = GetTickCount64();
        int elapsedMs = (int)(now - start);

        if (elapsedMs >= totalTimeoutMs) {
            slowDownload = true;
            timedOut = true;
            lastError = WSAETIMEDOUT;
            return false;
        }

        int space = allocatedSize - curPos;

        if (space <= 0) {
            int newSize = allocatedSize * 2;
            char* nb = (char*)realloc(buf, newSize);

            if (!nb) {
                return false;
            }

            buf = nb;
            allocatedSize = newSize;
            space = allocatedSize - curPos;
        }

        // Read encrypted bytes
        int bytes = SSL_read((SSL*)ssl, buf + curPos, space);
        if (bytes > 0) {
            curPos += bytes;

            if (curPos > maxBytes) {
                exceededMax = true;
                return false;
            }

            if (!checkedHttpPrefix && curPos >= 5) {
                checkedHttpPrefix = true;

                if (strncmp(buf, "HTTP/", 5) != 0) {
                    nonHttp = true;
                    return false;
                }
            }
            continue;
        }

        // Handle TLS read status
        int sslErr = SSL_get_error((SSL*)ssl, bytes);
        if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
            continue;
        }

        if (sslErr == SSL_ERROR_ZERO_RETURN ||
            ((sslErr == SSL_ERROR_SYSCALL || sslErr == SSL_ERROR_SSL) && curPos > 0)) {
            buf[curPos] = '\0';
            bytesDownloaded = curPos;

            if (bytesDownloaded < 5 || strncmp(buf, "HTTP/", 5) != 0) {
                nonHttp = true;
                return false;
            }

            return true;
        }

        lastError = sslErr;
        return false;
    }
}


// Resets object for next request
// Shrinks very large buffer back to default
// Clears counters and error state
void Socket::ResetForReuse() {
    if (allocatedSize > 32768) {
        free(buf);
        buf = (char*)malloc(INITIAL_BUF_SIZE);
        allocatedSize = buf ? INITIAL_BUF_SIZE : 0;
    }
    curPos = 0;
    bytesDownloaded = 0;
    timedOut = false;
    lastError = 0;
    if (buf && allocatedSize > 0) {
        buf[0] = '\0';
    }
}

// Closes socket and TLS resources
// Safe to call even if already closed
// Leaves object in clean state
void Socket::Close() {

    if (ssl) {
        SSL_free((SSL*)ssl);
        ssl = NULL;
    }

    if (sslCtx) {
        SSL_CTX_free((SSL_CTX*)sslCtx);
        sslCtx = NULL;
    }

    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}
