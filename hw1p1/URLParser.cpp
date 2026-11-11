
#include "pch.h"
#include "URLParser.h"

// Saswat Mishra
// CSCE 612
// Spring 2026

// Parses a full URL string
// Splits host port path and query fields
// Returns invalid result when any check fails
URLParser parseURL(const char* url) {
    URLParser purl;

    // Basic input validation
    if (url == nullptr || url[0] == '\0') {
        strcpy(purl.errorMsg, "empty URL");
        return purl;
    }

    // Parse and validate scheme, and set default port accordingly
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;
        purl.port = 80;
        purl.isHttps = false;
    }
    else if (strncmp(url, "https://", 8) == 0) {
        url += 8;
        purl.port = 443;
        purl.isHttps = true;
    }
    else {
        strcpy(purl.errorMsg, "invalid scheme");
        return purl;
    }

    if ((int)strlen(url) > MAX_URL_LEN) {
        strcpy(purl.errorMsg, "URL exceeds maximum length");
        return purl;
    }

    char temp[MAX_URL_LEN + 1];
    strcpy(temp, url);

    // Strip fragment by terminating string at '#'
    char* fragmentStart = strchr(temp, '#');
    if (fragmentStart != nullptr) {
        *fragmentStart = '\0';
    }

    // Find start of path or query
    char* pathStart = strchr(temp, '/');
    char* queryStart = strchr(temp, '?');

    char* requestStart = nullptr;

    if (pathStart != nullptr && queryStart != nullptr) {
        if (pathStart < queryStart) {
            requestStart = pathStart;
        }
        else {
            requestStart = queryStart;
        }
    }
    else if (pathStart != nullptr) {
        requestStart = pathStart;
    }
    else if (queryStart != nullptr) {
        requestStart = queryStart;
    }
    else {
        requestStart = nullptr;
    }

    char hostPort[MAX_HOST_LEN + 32];

    // Extract host[:port]
    if (requestStart != nullptr) {
        int len = (int)(requestStart - temp);

        if (len <= 0) {
            strcpy(purl.errorMsg, "missing host name");
            return purl;
        }

        if (len >= (int)sizeof(hostPort)) {
            len = (int)sizeof(hostPort) - 1;
        }

        memcpy(hostPort, temp, len);
        hostPort[len] = '\0';
    }
    else {
        strcpy(hostPort, temp);
    }

    // Parse optional port
    char* portStart = strchr(hostPort, ':');

    if (portStart != nullptr) {
        *portStart = '\0';
        const char* portStr = portStart + 1;

        if (*portStr == '\0') {
            strcpy(purl.errorMsg, "invalid port");
            return purl;
        }

        for (const char* c = portStr; *c != '\0'; c++) {
            if (!isdigit((unsigned char)*c)) {
                strcpy(purl.errorMsg, "invalid port");
                return purl;
            }
        }

        int port = atoi(portStr);

        if (port <= 0) {
            strcpy(purl.errorMsg, "invalid port");
            return purl;
        }

        purl.port = port;
    }
    else {
        if (purl.isHttps) {
            purl.port = 443;
        }
        else {
            purl.port = 80;
        }
    }

    // Validate host
    if (hostPort[0] == '\0') {
        strcpy(purl.errorMsg, "invalid hostname");
        return purl;
    }

    if ((int)strlen(hostPort) > MAX_HOST_LEN) {
        strcpy(purl.errorMsg, "hostname exceeds maximum length");
        return purl;
    }

    strcpy(purl.host, hostPort);

    // Parse path and query
    purl.query[0] = '\0';

    if (requestStart == nullptr) {
        strcpy(purl.path, "/");
    }
    else if (*requestStart == '/') {
        char* q2 = strchr(requestStart, '?');

        if (q2 == nullptr) {
            if ((int)strlen(requestStart) > MAX_REQUEST_LEN) {
                strcpy(purl.errorMsg, "request exceeds maximum length");
                return purl;
            }

            strcpy(purl.path, requestStart);
        }
        else {
            int pathLen = (int)(q2 - requestStart);

            if (pathLen <= 0) {
                strcpy(purl.path, "/");
            }
            else {
                if (pathLen >= MAX_REQUEST_LEN) {
                    pathLen = MAX_REQUEST_LEN - 1;
                }

                memcpy(purl.path, requestStart, pathLen);
                purl.path[pathLen] = '\0';
            }

            if ((int)strlen(q2 + 1) > MAX_REQUEST_LEN) {
                strcpy(purl.errorMsg, "request exceeds maximum length");
                return purl;
            }

            strcpy(purl.query, q2 + 1);
        }
    }
    else if (*requestStart == '?') {
        strcpy(purl.path, "/");

        if ((int)strlen(requestStart + 1) > MAX_REQUEST_LEN) {
            strcpy(purl.errorMsg, "request exceeds maximum length");
            return purl;
        }

        strcpy(purl.query, requestStart + 1);
    }
    else {
        strcpy(purl.errorMsg, "malformed URL");
        return purl;
    }

    // Final combined request length validation
    int reqLen = (int)strlen(purl.path);

    if (purl.query[0] != '\0') {
        reqLen += 1 + (int)strlen(purl.query);
    }

    if (reqLen > MAX_REQUEST_LEN) {
        strcpy(purl.errorMsg, "request exceeds maximum length");
        return purl;
    }

    // Success
    purl.valid = true;
    purl.errorMsg[0] = '\0';

    return purl;
}
