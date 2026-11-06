#pragma once

#include <string>

// Minimal "--name value" parser for this app's own command line.
//
// wWinMain receives the tail of the command line as one string, and every
// flag we support is a simple pair, so a full argv parser is unnecessary.
namespace CommandLine {

// Returns the value after `flag`, or an empty string when the flag is absent.
// A flag only matches at a token boundary, so "foo--carrier" cannot match.
std::wstring GetValue(const wchar_t* cmdLine, const wchar_t* flag);

}  // namespace CommandLine
