#include "pch.h"
#include "RobotsParser.h"
#include <sstream>
#include <algorithm>

// Trims whitespace on both ends
// Used before parsing each robots line
// Returns empty string for blank input
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";

    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Makes a lowercase copy of text
// Used for case insensitive key checks
// Keeps original string unchanged
static std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

// Checks robots rules for target path
// Reads User-agent Allow and Disallow directives
// Uses first matching rule for final decision
bool robotsAllowed(const std::string& robotsText,
    const std::string& targetPath)
{
    // Read robots text line by line
    std::istringstream stream(robotsText);
    std::string line;

    bool inStarBlock = false;

    while (std::getline(stream, line)) {

        // Trim and skip empty lines
        line = trim(line);
        if (line.empty())
            continue;

        if (line[0] == '#')
            continue;

        // Parse key and value at first colon
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, colonPos));
        std::string value = trim(line.substr(colonPos + 1));

        std::string keyLower = toLower(key);

        // Track whether we are in user agent star block
        if (keyLower == "user-agent") {
            if (toLower(value) == "*")
                inStarBlock = true;
            else
                inStarBlock = false;

            continue;
        }

        // Apply allow and disallow rules
        if (!inStarBlock)
            continue;

        if (keyLower == "allow") {
            if (value.empty())
                continue;
            if (targetPath.rfind(value, 0) == 0)
                return true;
        }

        else if (keyLower == "disallow") {
            if (value.empty())
                continue;
            if (targetPath.rfind(value, 0) == 0)
                return false;
        }
    }

    return true;
}
