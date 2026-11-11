
#include "pch.h"
#include "CrawlerThread.h"
#include "URLParser.h"
#include "Socket.h"

#include <cstdio>
#include <string>

// Builds a complete HTTP request line and headers
// Supports GET and HEAD with optional query string
// Uses Host and User-agent headers required by homework
static int BuildHttpRequest(
    char* out,
    int capacity,
    const char* method,
    const char* host,
    int port,
    const char* path,
    const char* query)
{
    char requestTarget[MAX_REQUEST_LEN + 2];

    if (query && query[0] != '\0') {
        snprintf(requestTarget, sizeof(requestTarget), "%s?%s", path, query);
    }
    else {
        snprintf(requestTarget, sizeof(requestTarget), "%s", path);
    }

    if (port == 80) {
        return snprintf(out, capacity,
            "%s %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-agent: saswatmTAMUCrawler/1.3\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, requestTarget, host);
    }

    return snprintf(out, capacity,
        "%s %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "User-agent: saswatmTAMUCrawler/1.3\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, requestTarget, host, port);
}

// Parses numeric status code from HTTP response bytes
// Expects response to start with HTTP version token
// Returns -1 when code cannot be parsed safely
static int ParseStatusCode(const char* buf, int len)
{
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
        ++p;
    }

    int code = 0;
    while ((p - buf) < len && *p >= '0' && *p <= '9') {
        code = code * 10 + (*p - '0');
        ++p;
    }

    return code;
}

/* Disabled TAMU helper kept for report-only analysis
static bool IsTamuHost(const char* link)
{
    if (!link) {
        return false;
    }

    const char* scheme = strstr(link, "://");
    const char* hostStart = scheme ? (scheme + 3) : link;

    int hostLen = 0;
    while (hostStart[hostLen] != '\0' &&
        hostStart[hostLen] != '/' &&
        hostStart[hostLen] != ':' &&
        hostStart[hostLen] != '?') {
        hostLen++;
    }

    if (hostLen <= 0) {
        return false;
    }

    std::string host(hostStart, hostLen);
    for (char& ch : host) {
        ch = (char)tolower((unsigned char)ch);
    }

    if (host == "tamu.edu") {
        return true;
    }

    const std::string suffix = ".tamu.edu";
    if (host.size() > suffix.size() &&
        host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return true;
    }

    return false;
}*/

// Waits on queue semaphore and pops one URL
// Also listens for shutdown event to stop cleanly
// Returns false when thread should exit
static bool PopUrl(CrawlerState* state, std::string& urlOut)
{
    urlOut.clear();

    HANDLE waitHandles[2] = { state->queueSemaphore, state->shutdownEvent };

    while (true) {
        DWORD waitRc = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitRc == WAIT_OBJECT_0 + 1) {
            return false;
        }
        if (waitRc != WAIT_OBJECT_0) {
            return false;
        }

        EnterCriticalSection(&state->queueLock);

        if (!state->urlQueue.empty()) {
            urlOut = state->urlQueue.front();
            state->urlQueue.pop();
            LeaveCriticalSection(&state->queueLock);
            return true;
        }

        bool finished = (state->producerFinished != 0);
        LeaveCriticalSection(&state->queueLock);

        if (finished) {
            return false;
        }
    }
}

// Main crawler worker thread function
// Runs URL checks, robots checks, and page download
// Updates shared counters used by stats reporting thread
DWORD WINAPI crawlerThreadMain(LPVOID param)
{
    // Per-thread reusable objects
    CrawlerState* state = (CrawlerState*)param;

    Socket sock;
    HTMLParserBase parser;

    // Process URLs until queue is drained
    while (true) {
        std::string url;
        if (!PopUrl(state, url)) {
            break;
        }

        if (url.empty()) {
            continue;
        }

        InterlockedIncrement64(&state->extractedUrls);

        // Parse URL and skip invalid entries
        URLParser parsed = parseURL(url.c_str());
        if (!parsed.valid) {
            continue;
        }

        if (parsed.isHttps || parsed.port == 443) {
            continue;
        }

        // Host uniqueness filter
        bool isNewHost = false;
        EnterCriticalSection(&state->hostLock);
        if (state->uniqueHosts.find(parsed.host) == state->uniqueHosts.end()) {
            state->uniqueHosts.insert(parsed.host);
            isNewHost = true;
        }
        LeaveCriticalSection(&state->hostLock);

        if (!isNewHost) {
            continue;
        }
        InterlockedIncrement64(&state->uniqueHostCount);

        // DNS lookup and IP uniqueness filter
        if (!sock.ResolveDNS(parsed.host)) {
            continue;
        }
        InterlockedIncrement64(&state->dnsLookupCount);

        bool isNewIp = false;
        EnterCriticalSection(&state->ipLock);
        if (state->uniqueIps.find(sock.ip) == state->uniqueIps.end()) {
            state->uniqueIps.insert(sock.ip);
            isNewIp = true;
        }
        LeaveCriticalSection(&state->ipLock);

        if (!isNewIp) {
            continue;
        }
        InterlockedIncrement64(&state->uniqueIpCount);

        // Robots request phase
        sock.Close();
        if (!sock.Open()) {
            continue;
        }
        if (!sock.Connect(parsed.port)) {
            sock.Close();
            continue;
        }

        InterlockedIncrement64(&state->robotsAttempted);

        char robotsRequest[2048];
        BuildHttpRequest(
            robotsRequest,
            (int)sizeof(robotsRequest),
            "HEAD",
            parsed.host,
            parsed.port,
            "/robots.txt",
            "");

        if (!sock.SendAll(robotsRequest, (int)strlen(robotsRequest))) {
            sock.Close();
            continue;
        }

        bool exceeded = false;
        bool slow = false;
        bool nonHttp = false;

        if (!sock.ReadWithLimits(16 * 1024, 10000, exceeded, slow, nonHttp)) {
            sock.Close();
            continue;
        }

        InterlockedAdd64(&state->totalBytesDownloaded, sock.bytesDownloaded);

        // Only 4xx robots code means crawl is allowed
        int robotsCode = ParseStatusCode(sock.buf, sock.curPos);

        if (!(robotsCode >= 400 && robotsCode < 500)) {
            sock.Close();
            continue;
        }

        InterlockedIncrement64(&state->robotsReturned4xx);
        InterlockedIncrement64(&state->robotsPassedCount);

        sock.Close();

        // Page request phase
        if (!sock.Open()) {
            continue;
        }
        if (!sock.Connect(parsed.port)) {
            sock.Close();
            continue;
        }

        char pageRequest[4096];
        BuildHttpRequest(
            pageRequest,
            (int)sizeof(pageRequest),
            "GET",
            parsed.host,
            parsed.port,
            parsed.path,
            parsed.query);

        if (!sock.SendAll(pageRequest, (int)strlen(pageRequest))) {
            sock.Close();
            continue;
        }

        exceeded = false;
        slow = false;
        nonHttp = false;

        if (!sock.ReadWithLimits(2 * 1024 * 1024, 15000, exceeded, slow, nonHttp)) {
            sock.Close();
            continue;
        }

        InterlockedIncrement64(&state->pagesCrawledCount);
        InterlockedAdd64(&state->totalBytesDownloaded, sock.bytesDownloaded);

        // Parse status and update counters
        int status = ParseStatusCode(sock.buf, sock.curPos);

        if (status >= 200 && status < 300) {
            InterlockedIncrement64(&state->http2xx);
            InterlockedIncrement64(&state->pages2xx);
            InterlockedAdd64(&state->bytes2xx, sock.bytesDownloaded);

            char baseUrl[MAX_URL_LEN];
            snprintf(baseUrl, sizeof(baseUrl), "http://%s", parsed.host);

            int linkCount = 0;
            char* linkBuffer = parser.Parse(
                sock.buf,
                sock.bytesDownloaded,
                baseUrl,
                (int)strlen(baseUrl),
                &linkCount);

            if (linkCount < 0) {
                sock.Close();
                continue;
            }
            InterlockedAdd64(&state->linksDiscovered, linkCount);
            InterlockedAdd64(&state->links2xx, linkCount);

            /*bool pageLinksToTamu = false;
            if (linkBuffer) {
                char* linkPtr = linkBuffer;
                for (int i = 0; i < linkCount; ++i) {
                    if (IsTamuHost(linkPtr)) {
                        pageLinksToTamu = true;
                        break;
                    }
                    linkPtr += strlen(linkPtr) + 1;
                }
            }

            if (pageLinksToTamu) {
                InterlockedIncrement64(&state->pagesLinkingToTamu);
                if (!IsTamuHost(parsed.host)) {
                    InterlockedIncrement64(&state->pagesLinkingToTamuOutsideDomain);
                }
            }*/

        }
        else if (status >= 300 && status < 400) {
            InterlockedIncrement64(&state->http3xx);
        }
        else if (status >= 400 && status < 500) {
            InterlockedIncrement64(&state->http4xx);
        }
        else if (status >= 500 && status < 600) {
            InterlockedIncrement64(&state->http5xx);
        }
        else {
            InterlockedIncrement64(&state->httpOther);
        }

        sock.Close();
        sock.ResetForReuse();
    }

    InterlockedDecrement(&state->activeThreads);
    return 0;
}
