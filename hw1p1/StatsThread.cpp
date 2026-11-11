
#include "pch.h"
#include "StatsThread.h"

// Stats thread entry function
// Prints crawl progress every two seconds
// Stops when main thread signals shutdown event
DWORD WINAPI statsThreadMain(LPVOID param) {

    CrawlerState* state = (CrawlerState*)param;

    LONG64 lastPages = 0;
    LONG64 lastBytes = 0;
    ULONGLONG lastTick = GetTickCount64();

    // Keep printing until shutdown
    while (true) {

        DWORD waitResult =
            WaitForSingleObject(state->shutdownEvent, 2000);

        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        else {
        }

        // Snapshot shared counters and print line
        ULONGLONG now = GetTickCount64();
        int elapsedSeconds =
            (int)((now - state->startTimeMs) / 1000);

        int queueSize = 0;

        EnterCriticalSection(&state->queueLock);
        queueSize =
            (int)state->urlQueue.size();
        LeaveCriticalSection(&state->queueLock);

        printf("[%3d] %4ld Q %7d E %7lld H %6lld D %6lld I %5lld R %5lld C %5lld L %4lldK\n",
            elapsedSeconds,
            state->activeThreads,
            queueSize,
            state->extractedUrls,
            state->uniqueHostCount,
            state->dnsLookupCount,
            state->uniqueIpCount,
            state->robotsPassedCount,
            state->pagesCrawledCount,
            state->linksDiscovered / 1000
        );

        // Compute pages per second and Mbps
        double deltaTime =
            (double)(now - lastTick) / 1000.0;

        double pagesPerSecond = 0.0;
        double megabitsPerSecond = 0.0;

        if (deltaTime > 0.0) {
            pagesPerSecond =
                (state->pagesCrawledCount - lastPages) / deltaTime;

            megabitsPerSecond =
                ((state->totalBytesDownloaded - lastBytes) * 8.0)
                / (deltaTime * 1000000.0);
        }
        else {
        }

        printf("*** crawling %.1f pps @ %.1f Mbps\n",
            pagesPerSecond,
            megabitsPerSecond
        );

        lastPages = state->pagesCrawledCount;
        lastBytes = state->totalBytesDownloaded;
        lastTick = now;
    }

    return 0;
}
