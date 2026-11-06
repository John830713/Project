// AdminCommandTest.cpp - Tests for the split-elevation admin helper
//
// The elevated round trip itself (ShellExecuteEx "runas" -> helper -> result
// file) cannot run unattended, so this covers the parts that decide *what* the
// helper is asked to do and how its answer is parsed:
//
//   - command-line quoting / argument building
//   - the shared "--name value" parser
//   - the RESULT=OK|FAIL file format (including CJK round trip)
//
// mingw32-make Debug/Services/AdminCommandTest.exe

#include <cstdio>
#include <string>
#include <vector>

#include "Core/CommandLine.h"
#include "Services/AdminCommand.h"

static int g_tests = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    g_tests++; \
    if (!(cond)) { \
        printf("FAIL %s\n", msg); \
        g_fails++; \
    } else { \
        printf("OK   %s\n", msg); \
    } } while (0)

static bool ContainsW(const std::wstring& haystack, const std::wstring& needle) {
    return haystack.find(needle) != std::wstring::npos;
}

static bool StartsWithW(const std::wstring& text, const std::wstring& prefix) {
    return text.rfind(prefix, 0) == 0;
}

static void TestQuoteArgument() {
    printf("-- QuoteArgument --\n");

    CHECK(AdminCommand::QuoteArgument(L"plain") == L"plain",
          "plain argument is not quoted");
    CHECK(AdminCommand::QuoteArgument(L"a b") == L"\"a b\"",
          "argument with space is quoted");
    CHECK(AdminCommand::QuoteArgument(L"a\tb") == L"\"a\tb\"",
          "argument with tab is quoted");
    CHECK(AdminCommand::QuoteArgument(L"say \"hi\"") == L"\"say \\\"hi\\\"\"",
          "embedded quotes are escaped");
    CHECK(AdminCommand::QuoteArgument(L"") == L"", "empty argument stays empty");

    // A trailing backslash must be doubled, otherwise it escapes the closing
    // quote and the helper sees a truncated value.
    const std::wstring trailing = AdminCommand::QuoteArgument(L"a b\\");
    CHECK(trailing == L"\"a b\\\\\"", "trailing backslash is doubled");
}

static void TestBuildArgs() {
    printf("-- BuildArgs --\n");

    const std::wstring apply =
        AdminCommand::BuildApplyArgs({L"rdp", L"nla"}, L"WORKGROUP\\alice");
    CHECK(StartsWithW(apply, L"--admin remote-init-apply --ids rdp,nla"),
          "apply args name the op and join ids");
    CHECK(ContainsW(apply, L"--user WORKGROUP\\alice"),
          "apply args carry the user name");

    const std::wstring noUser = AdminCommand::BuildApplyArgs({L"winrm"}, L"");
    CHECK(StartsWithW(noUser, L"--admin remote-init-apply --ids winrm"),
          "apply args without a user");
    CHECK(!ContainsW(noUser, L"--user"), "empty user name is omitted");

    const std::wstring empty = AdminCommand::BuildApplyArgs({}, L"");
    CHECK(StartsWithW(empty, L"--admin remote-init-apply --ids"),
          "empty id list still produces a complete command");

    const std::wstring restore =
        AdminCommand::BuildRestoreArgs(L"C:\\tmp\\backup one.json");
    CHECK(restore ==
              L"--admin remote-init-restore --backup \"C:\\tmp\\backup one.json\"",
          "restore args quote the backup path");
}

static void TestParseResult() {
    printf("-- ParseResultText --\n");

    std::vector<std::wstring> lines;
    CHECK(AdminCommand::ParseResultText(L"RESULT=OK\n[OK] one\r\n[OK] two\n",
                                        lines),
          "RESULT=OK means success");
    CHECK(lines.size() == 2, "log lines are collected");
    CHECK(lines.size() == 2 && lines[0] == L"[OK] one",
          "CRLF line endings are stripped");
    CHECK(lines.size() == 2 && lines[1] == L"[OK] two",
          "last line is collected");

    lines.clear();
    CHECK(!AdminCommand::ParseResultText(L"RESULT=FAIL\n[FAIL] nope\n", lines),
          "RESULT=FAIL means failure");
    CHECK(lines.size() == 1 && lines[0] == L"[FAIL] nope",
          "failure log line is kept");

    lines.clear();
    CHECK(!AdminCommand::ParseResultText(L"", lines), "empty text is a failure");
    CHECK(lines.empty(), "empty text yields no lines");

    lines.clear();
    CHECK(!AdminCommand::ParseResultText(L"[OK] no result marker\n", lines),
          "missing result marker is a failure");
    CHECK(lines.size() == 1, "lines without a marker are still collected");
}

static void TestResultFileRoundTrip() {
    printf("-- result file round trip --\n");

    const std::wstring path = AdminCommand::MakeTempResultPath();
    CHECK(!path.empty(), "temp result path is created");
    if (path.empty()) return;

    std::vector<std::wstring> lines = {
        L"[OK] 啟用遠端桌面",
        L"[FAIL] 防火牆規則建立失敗",
        L"[OK] winrm listener 已存在",
    };
    AdminCommand::WriteResultFile(path, false, lines);

    const std::wstring text = AdminCommand::ReadTextFile(path);
    CHECK(!text.empty(), "result file is readable");

    std::vector<std::wstring> parsed;
    const bool ok = AdminCommand::ParseResultText(text, parsed);
    CHECK(!ok, "written failure result parses as failure");
    CHECK(parsed.size() == lines.size(), "every log line survives the round trip");
    CHECK(parsed.size() == lines.size() && parsed[0] == lines[0],
          "CJK text survives the UTF-8 round trip");
    CHECK(parsed.size() == lines.size() && parsed[2] == lines[2],
          "last line survives the UTF-8 round trip");

    AdminCommand::WriteResultFile(path, true, {L"[OK] done"});
    parsed.clear();
    CHECK(AdminCommand::ParseResultText(AdminCommand::ReadTextFile(path), parsed),
          "written success result parses as success");

    DeleteFileW(path.c_str());
    CHECK(AdminCommand::ReadTextFile(path).empty(),
          "missing result file reads as empty");
}

static void TestCommandLine() {
    printf("-- CommandLine::GetValue --\n");

    CHECK(CommandLine::GetValue(L"--carrier tray --flavor demo", L"--carrier") ==
              L"tray",
          "value is read from the middle of the line");
    CHECK(CommandLine::GetValue(L"--carrier tray --flavor demo", L"--flavor") ==
              L"demo",
          "second flag is read");
    CHECK(CommandLine::GetValue(L"--flavor \"My Flavor\"", L"--flavor") ==
              L"My Flavor",
          "quoted value is unquoted");
    CHECK(CommandLine::GetValue(L"  --carrier tray", L"--carrier") == L"tray",
          "leading whitespace is accepted");
    CHECK(CommandLine::GetValue(L"--carrier", L"--carrier").empty(),
          "flag without a value yields empty");
    CHECK(CommandLine::GetValue(L"--carriers tray", L"--carrier").empty(),
          "flag prefix of a longer token does not match");
    CHECK(CommandLine::GetValue(L"foo--carrier tray", L"--carrier").empty(),
          "flag must start a token");
    CHECK(CommandLine::GetValue(L"--carrier tray", L"--missing").empty(),
          "missing flag yields empty");
    CHECK(CommandLine::GetValue(nullptr, L"--carrier").empty(),
          "null command line yields empty");
    CHECK(CommandLine::GetValue(L"--carrier tray", nullptr).empty(),
          "null flag yields empty");
}

static void TestIsInvocation() {
    printf("-- IsInvocation --\n");

    CHECK(AdminCommand::IsInvocation(L"--admin remote-init-apply"),
          "--admin as the first token is an admin invocation");
    CHECK(AdminCommand::IsInvocation(L"   --admin"), "leading spaces are skipped");
    CHECK(AdminCommand::IsInvocation(L"--admin"), "bare --admin is an invocation");
    CHECK(!AdminCommand::IsInvocation(L"--administrator x"),
          "--administrator is not --admin");
    CHECK(!AdminCommand::IsInvocation(L"--carrier pet --admin x"),
          "admin flag must come first");
    CHECK(!AdminCommand::IsInvocation(L""), "empty command line is not admin");
    CHECK(!AdminCommand::IsInvocation(nullptr),
          "null command line is not admin");
}

int main() {
    printf("=== AdminCommandTest ===\n");

    TestQuoteArgument();
    TestBuildArgs();
    TestParseResult();
    TestResultFileRoundTrip();
    TestCommandLine();
    TestIsInvocation();

    printf("\n%d/%d passed\n", g_tests - g_fails, g_tests);
    return g_fails == 0 ? 0 : 1;
}
