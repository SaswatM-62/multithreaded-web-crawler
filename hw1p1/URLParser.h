// Saswat Mishra
// CSCE 612
// Spring 2026

#pragma once
#include "pch.h"

struct URLParser {
    char host[MAX_HOST_LEN + 1];        // hostname (e.g., www.example.com)
    char path[MAX_REQUEST_LEN + 1];     // request path, always starts with '/'
    char query[MAX_REQUEST_LEN + 1];    // query string without '?'
    char errorMsg[256];                 // error message if parsing fails
    int  port;                          // port number (default 80)
    bool valid;                         // true if URL is valid
    bool isHttps;                       // true if scheme is https

    URLParser() {                          // Initialize defaults
        host[0] = '\0';                 // empty host
        path[0] = '\0';                 // empty path
        query[0] = '\0';                // empty query
        errorMsg[0] = '\0';             // empty error message
        port = 0;                       // unset port
        valid = false;                  // mark as invalid initially
		isHttps = false;                // default to false
    }
};

URLParser parseURL(const char* url);    // returns populated URLParser struct
