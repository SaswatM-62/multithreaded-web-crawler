# Multithreaded Web Crawler

This repository contains code for the CSCE 463/612 Applied Networks and Distributed Processing course. Students actively taking this course should not copy from this repository and should follow the Aggie Honor Code.

A Windows C++ crawler built directly on Winsock, with a multithreaded HTTP batch mode and
a single-URL HTTPS mode using OpenSSL. The implementation includes the Part 3 crawler and
HTTPS/robots extra-credit functionality, although the solution and source folder retain their
original `hw1` and `hw1p1` names.

## Features

- Bulk processing of a text file containing one HTTP URL per line, with a configurable worker count.
- Host and IPv4-address uniqueness checks before crawling each site.
- HTTP/1.0 request construction, DNS resolution, TCP connections, and dynamically grown receive buffers.
- A bulk robots policy that sends `HEAD /robots.txt` and proceeds only after a 4xx response.
- Single-URL HTTPS downloads with TLS and a separate `GET /robots.txt` request.
- Lightweight robots parsing for `User-agent: *`, case-insensitive directive names, and the first matching `Allow` or `Disallow` path prefix. Empty directives are ignored; wildcard matching is not implemented.
- HTML link extraction using the course-provided parser, with one parser instance per worker.
- Progress output every two seconds and a final summary of crawling rates, links, and HTTP status classes.

## Implementation Details

[solution.cpp](hw1p1/solution.cpp) reads the input file, starts the worker and statistics threads,
feeds a shared URL queue, and coordinates shutdown. [CrawlerState.h](hw1p1/CrawlerState.h)
holds the queue, uniqueness sets, counters, critical sections, semaphore, and shutdown event.

[CrawlerThread.cpp](hw1p1/CrawlerThread.cpp) implements the batch pipeline: parse a URL,
check host uniqueness, resolve DNS, check IP uniqueness, check robots status, fetch the page,
and parse links from 2xx responses. Extracted links are counted rather than added back into
the queue, so this processes the supplied URL list rather than recursively exploring the web.
Batch mode skips HTTPS URLs and URLs using port 443.

[Socket.cpp](hw1p1/Socket.cpp) handles Winsock and OpenSSL operations;
[URLParser.cpp](hw1p1/URLParser.cpp) separates the scheme, host, port, path, query, and fragment.
[RobotsParser.cpp](hw1p1/RobotsParser.cpp) is used by single-URL HTTPS mode. The single-URL
path always attempts TLS, so use an `https://` URL. It currently sends the parsed path without
its query string; query-dependent URLs are not fully supported in that mode.

[StatsThread.cpp](hw1p1/StatsThread.cpp) reports the following counters:

| Field | Meaning |
| --- | --- |
| Unlabeled columns | Elapsed seconds and active worker threads |
| `Q` | URLs still queued |
| `E` | URLs extracted from the queue |
| `H` / `D` | URLs passing host uniqueness / successful DNS lookups |
| `I` / `R` | URLs passing IP uniqueness / robots checks |
| `C` / `L` | Completed page downloads / discovered links in thousands |
| `pps` / `Mbps` | Page and download rates since the previous update |

The download-byte counter includes successful robots and page responses. The final value
labeled `MB` is calculated using 1024² bytes. The optional TAMU-link analysis is present as
commented code and is not collected by the current build.

## Build

Required build environment:

- Windows with Visual Studio 2022 and the **Desktop development with C++** workload, including the MSVC **v143** toolset and Windows SDK.
- **x64** configuration: the provided HTML parser libraries do not support Win32 builds.
- `HTMLParserBase.h`, `HTMLParser_debug_x64.lib`, and `HTMLParser_release_x64.lib`, included in `hw1p1/` from the course package.
- A compatible **x64 OpenSSL** installation providing headers, `libssl.lib`, and `libcrypto.lib`. The submission report records OpenSSL 3.6.1. OpenSSL is linked unconditionally, so it is needed even when only running HTTP batch mode.

Open [hw1.sln](hw1.sln), select **Release | x64**, and build. The current project expects:

| Setting | Configured path |
| --- | --- |
| OpenSSL headers | `C:\Program Files\OpenSSL-Win64\include` |
| Release libraries | `C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD` |
| Debug libraries | `C:\Program Files\OpenSSL-Win64\lib\VC\x64\MDd` |

If OpenSSL is installed elsewhere, adjust **C/C++ → Additional Include Directories** and
**Linker → Additional Library Directories** for the selected configuration. Match the OpenSSL
library architecture and runtime to the build. The project also lists an old `Downloads`
location for `HTMLParserBase.h`; the actual header is included locally. If Visual Studio shows
a missing header entry, replace that project entry with `hw1p1/HTMLParserBase.h`.

Alternatively, from a Visual Studio Developer Command Prompt in this directory:

```bat
msbuild hw1.sln /m /p:Configuration=Release /p:Platform=x64
```

The executable is `x64\Release\hw1.exe` (`x64\Debug\hw1.exe` for Debug).

## Run

Required at runtime: the matching OpenSSL DLLs on `PATH` or beside the executable, the MSVC
runtime used by the build, working IPv4 DNS/network access, and reachable HTTP or HTTPS servers.
A bulk crawl additionally needs a URL-list file; the course input datasets are not included here.
Administrator privileges are not required for these TCP connections.

Run the examples from the directory containing `hw1.sln`. Input-file paths are relative to the
terminal's current directory; quote paths containing spaces.

Bulk crawl syntax:

```text
hw1.exe <thread-count> <url-list-file.txt>
```

For example, create `urls.txt` with one complete `http://` URL per line, then run:

```powershell
.\x64\Release\hw1.exe 10 .\urls.txt
.\x64\Release\hw1.exe 500 .\URL-input-1M.txt
```

The thread count must be a positive integer. Larger counts create more simultaneous connections
and consume more memory; start with a small input and worker count when checking setup.

Single-URL HTTPS syntax and examples:

```text
hw1.exe <https-url>
```

```powershell
.\x64\Release\hw1.exe https://irl.cs.tamu.edu
.\x64\Release\hw1.exe https://www.google.com/maps/reserve
```

This mode downloads robots rules first, checks the target path, and fetches the page only if
allowed. It prints connection/download timings, status information, link counts, and response
headers. Actual results depend on the target's availability and current robots policy.
