// Saswat Mishra
// CSCE 612
// Spring 2026

#pragma once
#include "pch.h"

#include <queue>
#include <string>
#include <unordered_set>

struct CrawlerState {

    std::queue<std::string> urlQueue; // Shared URL queue
    CRITICAL_SECTION queueLock;
    HANDLE queueSemaphore;
    LONG producerFinished;

    std::unordered_set<std::string> uniqueHosts; // Unique host tracker
    std::unordered_set<DWORD> uniqueIps;
    CRITICAL_SECTION hostLock;
    CRITICAL_SECTION ipLock;

    HANDLE shutdownEvent; // Global shutdown signal
    int totalThreads;
    LONG activeThreads;
    ULONGLONG startTimeMs;

    LONG64 extractedUrls; // URLs pulled from queue
    LONG64 uniqueHostCount;
    LONG64 dnsLookupCount;
    LONG64 uniqueIpCount;
    LONG64 robotsPassedCount;
    LONG64 pagesCrawledCount;
    LONG64 linksDiscovered;

    LONG64 totalBytesDownloaded;

    LONG64 http2xx;
    LONG64 http3xx;
    LONG64 http4xx;
    LONG64 http5xx;
    LONG64 httpOther;

    LONG64 pages2xx;
    LONG64 bytes2xx;
    LONG64 links2xx;

    LONG64 robotsAttempted;
    LONG64 robotsReturned4xx;

    LONG64 pagesLinkingToTamu;
    LONG64 pagesLinkingToTamuOutsideDomain;

    CrawlerState() { // Initialize all shared state defaults

        queueSemaphore = NULL;
        shutdownEvent = NULL;

        producerFinished = 0;
        totalThreads = 0;
        activeThreads = 0;
        startTimeMs = 0;

        extractedUrls = 0;
        uniqueHostCount = 0;
        dnsLookupCount = 0;
        uniqueIpCount = 0;
        robotsPassedCount = 0;
        pagesCrawledCount = 0;
        linksDiscovered = 0;

        totalBytesDownloaded = 0;

        http2xx = 0;
        http3xx = 0;
        http4xx = 0;
        http5xx = 0;
        httpOther = 0;

        pages2xx = 0;
        bytes2xx = 0;
        links2xx = 0;

        robotsAttempted = 0;
        robotsReturned4xx = 0;

        pagesLinkingToTamu = 0;
        pagesLinkingToTamuOutsideDomain = 0;
    }
};
