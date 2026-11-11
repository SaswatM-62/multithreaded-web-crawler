// Saswat Mishra
// CSCE 612
// Spring 2026

#pragma once

#ifdef _WIN64
#ifdef _DEBUG
#pragma comment (lib, "HTMLParser_debug_x64.lib")
#else
#pragma comment (lib, "HTMLParser_release_x64.lib")
#endif
#else
#ifdef _DEBUG
#error Debug 32-bit not supported by the HTML parser
#else
#error Release 32-bit not supported by the HTML parser
#endif
#endif

#define MAX_HOST_LEN		256
#define MAX_URL_LEN			2048
#define MAX_REQUEST_LEN		2048

class HTMLParserBase
{
public:
	HTMLParserBase(); // Constructor (implemented in provided parser library)
	~HTMLParserBase(); // Destructor (implemented in provided parser library)
	char*	Parse (char *htmlCode, int codeSize, char *baseURL, int urlLen, int *nLinks); // Returns parser-owned link buffer

private:
	void	*parser;
	void	*buffer;
};
