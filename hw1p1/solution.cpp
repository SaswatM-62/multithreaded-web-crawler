
#include "pch.h"
#include "CrawlerState.h"
#include "CrawlerThread.h"
#include "StatsThread.h"
#include "URLParser.h"
#include "Socket.h"
#include "RobotsParser.h"

#include <cstdio>
#include <string>
#include <vector>
#include <climits>

// Formats an integer with comma separators
// Used for friendly output in startup and summary
// Writes empty string when output buffer is too small
static void FormatWithCommas(long long value, char* out, int outCap) {
    char digits[32];
    snprintf(digits, sizeof(digits), "%lld", value);

    int len = (int)strlen(digits);
    int commaCount = (len > 0) ? (len - 1) / 3 : 0;
    int newLen = len + commaCount;

    if (newLen + 1 > outCap) {
        if (outCap > 0) {
            out[0] = '\0';
        }
        return;
    }

    out[newLen] = '\0';

    int src = len - 1;
    int dst = newLen - 1;
    int group = 0;

    while (src >= 0) {
        out[dst--] = digits[src--];
        group++;

        if (group == 3 && src >= 0) {
            out[dst--] = ',';
            group = 0;
        }
    }
}

// Reads full file into a null-terminated buffer
// Uses Windows file APIs for fast binary read
// Returns false when open, size, or read fails
static bool ReadFileToBufferBinary(const char* fileName, char*& buffer, int& size) {
    buffer = nullptr;
    size = 0;

    HANDLE fileHandle = CreateFile(
        fileName,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(fileHandle, &fileSize)) {
        CloseHandle(fileHandle);
        return false;
    }

    size = (int)fileSize.QuadPart;
    buffer = new char[size + 1];

    DWORD bytesRead = 0;
    bool ok = ReadFile(fileHandle, buffer, size, &bytesRead, NULL) && bytesRead == (DWORD)size;
    CloseHandle(fileHandle);

    if (!ok) {
        delete[] buffer;
        buffer = nullptr;
        size = 0;
        return false;
    }

    buffer[size] = '\0';
    return true;
}

// Parses HTTP status code from response bytes
// Expects first line to start with HTTP version token
// Returns -1 if the header format is not valid
static int ParseStatusCode(const char* buf, int len) {
    if (!buf || len < 12) {
        return -1;
    }

    if (memcmp(buf, "HTTP/", 5) != 0) {
        return -1;
    }

    const char* p = (const char*)memchr(buf, ' ', len);
    if (!p) {
        return -1;
    }

    while ((p - buf) < len && *p == ' ') {
        p++;
    }

    int code = 0;

    while ((p - buf) < len && *p >= '0' && *p <= '9') {
        code = code * 10 + (*p - '0');
        p++;
    }

    return code;
}


// Runs single URL mode for HTTPS extra credit
// Downloads robots first and checks allow or disallow
// Fetches and parses page only when robots allows it
static void RunHttpsMode(const char* urlString) {
    // Print target URL
    printf("URL: %s\n", urlString);

    ULONGLONG t0 = GetTickCount64();
    (void)t0;

    // Parse URL and print parsed fields
    URLParser parsed = parseURL(urlString);
    if (!parsed.valid) {
        printf("        Parsing URL... failed\n");
        return;
    }

    printf("        Parsing URL... host %s, port %d, request %s\n", parsed.host, parsed.port, parsed.path);

    // Open socket and do DNS lookup
    Socket socket;

    if (!socket.Open()) {
        printf("Socket open failed: %d\n", WSAGetLastError());
        return;
    }

    ULONGLONG dnsStart = GetTickCount64();

    if (!socket.ResolveDNS(parsed.host)) {
        printf("        Doing DNS... failed\n");
        return;
    }

    ULONGLONG dnsEnd = GetTickCount64();

    printf("        Doing DNS... done in %llu ms, found %s\n", dnsEnd - dnsStart, socket.ipStr);

    // Connect for robots request and start TLS
    ULONGLONG connStart = GetTickCount64();

    if (!socket.Connect(parsed.port)) {
        printf("        Connecting on robots... failed\n");
        return;
    }

    ULONGLONG connEnd = GetTickCount64();

    printf("        Connecting on robots... done in %llu ms\n",
        connEnd - connStart
    );

    if (!socket.InitTLS()) {
        printf("        TLS init failed\n");
        return;
    }

    if (!socket.DoTLSHandshake()) {
        printf("        TLS handshake failed\n");
        return;
    }

    // Build and send robots request
    char robotsRequest[2048];
    snprintf(robotsRequest, sizeof(robotsRequest),
        "GET /robots.txt HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-agent: saswatmTAMUcrawler/1.3\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.host
    );

    ULONGLONG loadStart = GetTickCount64();

    if (!socket.SendAllHTTPS(robotsRequest, (int)strlen(robotsRequest))) {
        printf("        Robots send failed (error %d)\n", socket.lastError);
        return;
    }

    // Read robots response and report failures
    bool ex = false;
    bool slow = false;
    bool nonHttp = false;

    if (!socket.ReadWithLimitsHTTPS(2 * 1024 * 1024, 15000, ex, slow, nonHttp)) {
        printf("        Loading... failed with error %d (exceeded=%d, timeout=%d, nonHttp=%d)\n",
            socket.lastError, ex ? 1 : 0, slow ? 1 : 0, nonHttp ? 1 : 0);
        return;
    }

    ULONGLONG loadEnd = GetTickCount64();

    printf("        Loading... done in %llu ms\n", loadEnd - loadStart);

    // Parse robots status and check path permission
    int status = ParseStatusCode(socket.buf, socket.curPos);
    printf("        Verifying header... status code %d\n", status);

    bool allowed = true;
    if (status >= 200 && status < 300) {
        const char* bodyStart = strstr(socket.buf, "\r\n\r\n");
        std::string robotsBody;
        if (bodyStart) {
            robotsBody.assign(bodyStart + 4);
        }
        allowed = robotsAllowed(robotsBody, parsed.path);
    }
    else if (status >= 400 && status < 500) {
        allowed = true;
    }
    else {
        allowed = false;
    }

    printf("        Verifying URL against robots... %s\n", allowed ? "allowed" : "disallowed");
    if (!allowed) {
        return;
    }

    // Open second connection for actual page
    socket.Close();

    if (!socket.Open()) {
        printf(" Page socket open failed\n");
        return;
    }

    ULONGLONG pageConnStart = GetTickCount64();

    if (!socket.Connect(parsed.port)) {
        printf(" Page connect failed\n");
        return;
    }

    ULONGLONG pageConnEnd = GetTickCount64();

    printf("      * Connecting on page... done in %llu ms\n", pageConnEnd - pageConnStart);

    if (!socket.InitTLS()) {
        printf("         TLS init failed\n");
        return;
    }

    if (!socket.DoTLSHandshake()) {
        printf("         TLS handshake failed\n");
        return;
    }

    // Build and send page request
    char pageRequest[4096];
    snprintf(pageRequest, sizeof(pageRequest),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-agent: csce-612-hw1\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path,
        parsed.host
    );

    ULONGLONG pageLoadStart = GetTickCount64();

    if (!socket.SendAllHTTPS(pageRequest, (int)strlen(pageRequest))) {
        printf(" Page send failed (error %d)\n", socket.lastError);
        return;
    }

    if (!socket.ReadWithLimitsHTTPS(2 * 1024 * 1024, 15000, ex, slow, nonHttp)) {
        printf("        Loading... failed with error %d (exceeded=%d, timeout=%d, nonHttp=%d)\n",
            socket.lastError, ex ? 1 : 0, slow ? 1 : 0, nonHttp ? 1 : 0);
        return;
    }

    // Read page response and parse links
    ULONGLONG pageLoadEnd = GetTickCount64();

    printf("        Loading... done in %llu ms with %d bytes\n", pageLoadEnd - pageLoadStart, socket.bytesDownloaded);

    status = ParseStatusCode(socket.buf, socket.curPos);

    printf("        Verifying header... status code %d\n", status);

    HTMLParserBase parser;

    ULONGLONG parseStart = GetTickCount64();

    char baseUrl[MAX_URL_LEN];
    snprintf(baseUrl, sizeof(baseUrl), "https://%s", parsed.host);

    int linkCount = 0;

    char* links = parser.Parse(socket.buf,socket.bytesDownloaded, baseUrl, (int)strlen(baseUrl), &linkCount);
    (void)links;

    ULONGLONG parseEnd = GetTickCount64();

    printf("      + Parsing page... done in %llu ms with %d links\n", parseEnd - parseStart, linkCount);

    printf("----------------------------------------\n");

    // Print only response header section
    char* headerEnd = strstr(socket.buf, "\r\n\r\n");
    if (headerEnd) {
        *headerEnd = '\0';
        printf("%s\n", socket.buf);
    }
}


// Program entry point for both run modes
// One argument means single URL HTTPS mode
// Three arguments means threaded crawl mode
int main(int argc, char** argv) {
    // Handle single URL mode
    if (argc == 2) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return 0;
        }
        RunHttpsMode(argv[1]);
        WSACleanup();
        return 0;
    }

    // Validate crawler mode arguments
    if (argc != 3) {
        printf("Usage:\n");
        printf("  hw1.exe <threads> <URL-input.txt>\n");
        return 1;
    }

    // Parse thread count and input file path
    int threadCount = atoi(argv[1]);
    const char* inputFile = argv[2];

    if (threadCount <= 0) {
        printf("Invalid thread count\n");
        return 1;
    }

    // Read file once and print file size
    char* fileBuffer = nullptr;
    int fileSize = 0;
    if (!ReadFileToBufferBinary(inputFile, fileBuffer, fileSize)) {
        printf("Failed to open input file: %s\n", inputFile);
        return 1;
    }

    char sizeText[64];
    FormatWithCommas((long long)fileSize, sizeText, (int)sizeof(sizeText));

    printf("Opened %s with size %s\n", inputFile, sizeText);

    // Start Winsock and setup shared state
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        delete[] fileBuffer;
        return 1;
    }

    CrawlerState crawlerState;

    crawlerState.totalThreads = threadCount;
    crawlerState.activeThreads = 0;
    crawlerState.startTimeMs = GetTickCount64();
    crawlerState.producerFinished = 0;

    InitializeCriticalSection(&crawlerState.queueLock);
    InitializeCriticalSection(&crawlerState.hostLock);
    InitializeCriticalSection(&crawlerState.ipLock);

    // Create synchronization objects
    crawlerState.queueSemaphore = CreateSemaphoreA(NULL, 0, LONG_MAX, NULL);

    crawlerState.shutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (!crawlerState.queueSemaphore || !crawlerState.shutdownEvent) {
        printf("Synchronization object creation failed\n");

        if (crawlerState.shutdownEvent) {
            CloseHandle(crawlerState.shutdownEvent);
        }

        if (crawlerState.queueSemaphore) {
            CloseHandle(crawlerState.queueSemaphore);
        }

        DeleteCriticalSection(&crawlerState.ipLock);
        DeleteCriticalSection(&crawlerState.hostLock);
        DeleteCriticalSection(&crawlerState.queueLock);

        WSACleanup();
        delete[] fileBuffer;
        return 1;
    }

    // Start stats thread
    HANDLE statsThreadHandle = CreateThread(NULL, 0, statsThreadMain, &crawlerState, 0, NULL);

    if (!statsThreadHandle) {

        printf("Stats thread creation failed with %d\n", GetLastError());

        SetEvent(crawlerState.shutdownEvent);

        CloseHandle(crawlerState.shutdownEvent);
        CloseHandle(crawlerState.queueSemaphore);

        DeleteCriticalSection(&crawlerState.ipLock);
        DeleteCriticalSection(&crawlerState.hostLock);
        DeleteCriticalSection(&crawlerState.queueLock);

        WSACleanup();
        delete[] fileBuffer;
        return 1;
    }

    // Start crawler worker threads
    std::vector<HANDLE> crawlerThreads;
    crawlerThreads.reserve((size_t)threadCount);

    for (int i = 0; i < threadCount; i++) {

        HANDLE threadHandle =
            CreateThread(NULL, 0, crawlerThreadMain, &crawlerState, 0, NULL);

        if (threadHandle) {
            crawlerThreads.push_back(threadHandle);
        }
        else {
            printf("Crawler thread creation failed with %d\n", GetLastError());
        }
    }

    // Fill queue from file lines
    crawlerState.totalThreads = (int)crawlerThreads.size();
    crawlerState.activeThreads = (LONG)crawlerThreads.size();

    char* cursor = fileBuffer;
    while (*cursor) {
        char* lineStart = cursor;
        char* lineEnd = strchr(cursor, '\n');
        if (!lineEnd) {
            lineEnd = cursor + strlen(cursor);
        }

        char saved = *lineEnd;
        *lineEnd = '\0';

        int length = (int)strlen(lineStart);
        if (length > 0 && lineStart[length - 1] == '\r') {
            lineStart[length - 1] = '\0';
        }

        if (lineStart[0] != '\0') {
            EnterCriticalSection(&crawlerState.queueLock);
            crawlerState.urlQueue.push(std::string(lineStart));
            LeaveCriticalSection(&crawlerState.queueLock);
            ReleaseSemaphore(crawlerState.queueSemaphore, 1, NULL);
        }

        if (saved == '\0') {
            break;
        }

        cursor = lineEnd + 1;
    }

    delete[] fileBuffer;
    fileBuffer = nullptr;

    // Signal producer done and wake workers
    crawlerState.producerFinished = 1;
    ReleaseSemaphore(crawlerState.queueSemaphore, (LONG)crawlerThreads.size(), NULL);

    // Wait for workers then stop stats thread
    for (HANDLE threadHandle : crawlerThreads) {
        WaitForSingleObject(threadHandle, INFINITE);
        CloseHandle(threadHandle);
    }

    SetEvent(crawlerState.shutdownEvent);
    WaitForSingleObject(statsThreadHandle, INFINITE);
    CloseHandle(statsThreadHandle);

    // Compute and print final summary rates
    ULONGLONG endTimeMs = GetTickCount64();
    double totalSeconds = (double)(endTimeMs - crawlerState.startTimeMs) / 1000.0;

    if (totalSeconds <= 0.0) {
        totalSeconds = 1.0;
    }

    double extractedRate = crawlerState.extractedUrls / totalSeconds;
    double dnsRate = crawlerState.dnsLookupCount / totalSeconds;
    double robotsRate = crawlerState.robotsAttempted / totalSeconds;
    double crawlRate = crawlerState.pagesCrawledCount / totalSeconds;
    double parseRate = crawlerState.linksDiscovered / totalSeconds;
    double totalMegabytes = crawlerState.totalBytesDownloaded / (1024.0 * 1024.0);

    printf("\nExtracted %lld URLs @ %.0f/s\n", crawlerState.extractedUrls, extractedRate);
    printf("Looked up %lld DNS names @ %.0f/s\n", crawlerState.dnsLookupCount, dnsRate);
    printf("Attempted %lld site robots @ %.0f/s\n", crawlerState.robotsAttempted, robotsRate);
    printf("Crawled %lld pages @ %.0f/s (%.2f MB)\n", crawlerState.pagesCrawledCount, crawlRate, totalMegabytes);
    printf("Parsed %lld links @ %.0f/s\n", crawlerState.linksDiscovered, parseRate);
    printf("HTTP codes: 2xx = %lld, 3xx = %lld, 4xx = %lld, 5xx = %lld, other = %lld\n",
        crawlerState.http2xx,
        crawlerState.http3xx,
        crawlerState.http4xx,
        crawlerState.http5xx,
        crawlerState.httpOther
    );
    /*printf("TAMU-linked pages: %lld, outside TAMU domain: %lld\n",
        crawlerState.pagesLinkingToTamu,
        crawlerState.pagesLinkingToTamuOutsideDomain
    );*/

    // Cleanup handles, locks, and Winsock
    CloseHandle(crawlerState.shutdownEvent);
    CloseHandle(crawlerState.queueSemaphore);

    DeleteCriticalSection(&crawlerState.ipLock);
    DeleteCriticalSection(&crawlerState.hostLock);
    DeleteCriticalSection(&crawlerState.queueLock);

    WSACleanup();

    return 0;
}
