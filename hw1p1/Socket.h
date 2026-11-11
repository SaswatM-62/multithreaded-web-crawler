// Saswat Mishra
// CSCE 612
// Spring 2026

#pragma once
#include "pch.h"

class Socket {
public:
    SOCKET sock;                 // Winsock socket handle

    char* buf;                   // dynamic receive buffer
    int allocatedSize;           // total buffer size
    int curPos;                  // current write position

    bool timedOut;               // true if global timeout occurred
    int lastError;               // last Winsock error
    int bytesDownloaded;         // total bytes received

    DWORD ip;                    // resolved IPv4 address
    char ipStr[64];              // printable IP string

    void* sslCtx;                
    void* ssl;                  

    Socket(); // Constructor
    ~Socket(); // Destructor

    bool Open(); // Open TCP socket
    bool ResolveDNS(const char* host); // Resolve host to IPv4
    bool Connect(int port); // Connect to remote endpoint

    bool SendAll(const char* data, int len); // Send all HTTP bytes
    bool ReadWithLimits(
        int maxBytes,
        int totalTimeoutMs,
        bool& exceededMax,
        bool& slowDownload,
        bool& nonHttp
    ); // Receive HTTP response with limits

    bool InitTLS(); // Initialize OpenSSL context/session
    bool DoTLSHandshake(); // Perform TLS handshake
    bool SendAllHTTPS(const char* data, int len); // Send all HTTPS bytes
    bool ReadWithLimitsHTTPS(
        int maxBytes,
        int totalTimeoutMs,
        bool& exceededMax,
        bool& slowDownload,
        bool& nonHttp
    ); // Receive HTTPS response with limits

    void ResetForReuse(); // Reset per-request state
    void Close(); // Close TLS/socket handles
};
