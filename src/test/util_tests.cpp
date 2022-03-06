
// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/system.h>

#include <clientversion.h>
#include <hash.h> // For Hash()
#include <key.h>  // For CKey
#include <sync.h>
#include <test/util/logging.h>
#include <test/util/setup_common.h>
#include <test/util/str.h>
#include <uint256.h>
#include <util/getuniquepath.h>
#include <util/message.h> // For MessageSign(), MessageVerify(), MESSAGE_MAGIC
#include <util/moneystr.h>
#include <util/overflow.h>
#include <util/spanparsing.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/time.h>
#include <util/vector.h>

#include <array>
#include <optional>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <univalue.h>
#include <utility>
#include <vector>
#ifndef WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <boost/test/unit_test.hpp>

using namespace std::literals;
static const std::string STRING_WITH_EMBEDDED_NULL_CHAR{"1"s "\0" "1"s};

/* defined in logging.cpp */
namespace BCLog {
    std::string LogEscapeMessage(const std::string& str);
}

BOOST_FIXTURE_TEST_SUITE(util_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(util_datadir)
{
    // Use local args variable instead of m_args to avoid making assumptions about test setup
    ArgsManager args;
    args.ForceSetArg("-datadir", fs::PathToString(m_path_root));

    const fs::path dd_norm = args.GetDataDirBase();

    args.ForceSetArg("-datadir", fs::PathToString(dd_norm) + "/");
    args.ClearPathCache();
    BOOST_CHECK_EQUAL(dd_norm, args.GetDataDirBase());

    args.ForceSetArg("-datadir", fs::PathToString(dd_norm) + "/.");
    args.ClearPathCache();
    BOOST_CHECK_EQUAL(dd_norm, args.GetDataDirBase());

    args.ForceSetArg("-datadir", fs::PathToString(dd_norm) + "/./");
    args.ClearPathCache();
    BOOST_CHECK_EQUAL(dd_norm, args.GetDataDirBase());

    args.ForceSetArg("-datadir", fs::PathToString(dd_norm) + "/.//");
    args.ClearPathCache();
    BOOST_CHECK_EQUAL(dd_norm, args.GetDataDirBase());
}

BOOST_AUTO_TEST_CASE(util_check)
{
    // Check that Assert can forward
    const std::unique_ptr<int> p_two = Assert(std::make_unique<int>(2));
    // Check that Assert works on lvalues and rvalues
    const int two = *Assert(p_two);
    Assert(two == 2);
    Assert(true);
    // Check that Assume can be used as unary expression
    const bool result{Assume(two == 2)};
    Assert(result);
}

BOOST_AUTO_TEST_CASE(util_criticalsection)
{
    RecursiveMutex cs;

    do {
        LOCK(cs);
        break;

        BOOST_ERROR("break was swallowed!");
    } while(0);

    do {
        TRY_LOCK(cs, lockTest);
        if (lockTest) {
            BOOST_CHECK(true); // Needed to suppress "Test case [...] did not check any assertions"
            break;
        }

        BOOST_ERROR("break was swallowed!");
    } while(0);
}

static const unsigned char ParseHex_expected[65] = {
    0x04, 0x67, 0x8a, 0xfd, 0xb0, 0xfe, 0x55, 0x48, 0x27, 0x19, 0x67, 0xf1, 0xa6, 0x71, 0x30, 0xb7,
    0x10, 0x5c, 0xd6, 0xa8, 0x28, 0xe0, 0x39, 0x09, 0xa6, 0x79, 0x62, 0xe0, 0xea, 0x1f, 0x61, 0xde,
    0xb6, 0x49, 0xf6, 0xbc, 0x3f, 0x4c, 0xef, 0x38, 0xc4, 0xf3, 0x55, 0x04, 0xe5, 0x1e, 0xc1, 0x12,
    0xde, 0x5c, 0x38, 0x4d, 0xf7, 0xba, 0x0b, 0x8d, 0x57, 0x8a, 0x4c, 0x70, 0x2b, 0x6b, 0xf1, 0x1d,
    0x5f
};
BOOST_AUTO_TEST_CASE(util_ParseHex)
{
    std::vector<unsigned char> result;
    std::vector<unsigned char> expected(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected));
    // Basic test vector
    result = ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f");
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());

    // Spaces between bytes must be supported
    result = ParseHex("12 34 56 78");
    BOOST_CHECK(result.size() == 4 && result[0] == 0x12 && result[1] == 0x34 && result[2] == 0x56 && result[3] == 0x78);

    // Leading space must be supported (used in BerkeleyEnvironment::Salvage)
    result = ParseHex(" 89 34 56 78");
    BOOST_CHECK(result.size() == 4 && result[0] == 0x89 && result[1] == 0x34 && result[2] == 0x56 && result[3] == 0x78);

    // Stop parsing at invalid value
    result = ParseHex("1234 invalid 1234");
    BOOST_CHECK(result.size() == 2 && result[0] == 0x12 && result[1] == 0x34);
}

BOOST_AUTO_TEST_CASE(util_HexStr)
{
    BOOST_CHECK_EQUAL(
        HexStr(ParseHex_expected),
        "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f");

    BOOST_CHECK_EQUAL(
        HexStr(Span{ParseHex_expected}.last(0)),
        "");

    BOOST_CHECK_EQUAL(
        HexStr(Span{ParseHex_expected}.first(0)),
        "");

    {
        const std::vector<char> in_s{ParseHex_expected, ParseHex_expected + 5};
        const Span<const uint8_t> in_u{MakeUCharSpan(in_s)};
        const Span<const std::byte> in_b{MakeByteSpan(in_s)};
        const std::string out_exp{"04678afdb0"};

        BOOST_CHECK_EQUAL(HexStr(in_u), out_exp);
        BOOST_CHECK_EQUAL(HexStr(in_s), out_exp);
        BOOST_CHECK_EQUAL(HexStr(in_b), out_exp);
    }
}

BOOST_AUTO_TEST_CASE(span_write_bytes)
{
    std::array mut_arr{uint8_t{0xaa}, uint8_t{0xbb}};
    const auto mut_bytes{MakeWritableByteSpan(mut_arr)};
    mut_bytes[1] = std::byte{0x11};
    BOOST_CHECK_EQUAL(mut_arr.at(0), 0xaa);
    BOOST_CHECK_EQUAL(mut_arr.at(1), 0x11);
}

BOOST_AUTO_TEST_CASE(util_Join)
{
    // Normal version
    BOOST_CHECK_EQUAL(Join({}, ", "), "");
    BOOST_CHECK_EQUAL(Join({"foo"}, ", "), "foo");
    BOOST_CHECK_EQUAL(Join({"foo", "bar"}, ", "), "foo, bar");

    // Version with unary operator
    const auto op_upper = [](const std::string& s) { return ToUpper(s); };
    BOOST_CHECK_EQUAL(Join<std::string>({}, ", ", op_upper), "");
    BOOST_CHECK_EQUAL(Join<std::string>({"foo"}, ", ", op_upper), "FOO");
    BOOST_CHECK_EQUAL(Join<std::string>({"foo", "bar"}, ", ", op_upper), "FOO, BAR");
}

BOOST_AUTO_TEST_CASE(util_TrimString)
{
    BOOST_CHECK_EQUAL(TrimString(" foo bar "), "foo bar");
    BOOST_CHECK_EQUAL(TrimString("\t \n  \n \f\n\r\t\v\tfoo \n \f\n\r\t\v\tbar\t  \n \f\n\r\t\v\t\n "), "foo \n \f\n\r\t\v\tbar");
    BOOST_CHECK_EQUAL(TrimString("\t \n foo \n\tbar\t \n "), "foo \n\tbar");
    BOOST_CHECK_EQUAL(TrimString("\t \n foo \n\tbar\t \n ", "fobar"), "\t \n foo \n\tbar\t \n ");
    BOOST_CHECK_EQUAL(TrimString("foo bar"), "foo bar");
    BOOST_CHECK_EQUAL(TrimString("foo bar", "fobar"), " ");
    BOOST_CHECK_EQUAL(TrimString(std::string("\0 foo \0 ", 8)), std::string("\0 foo \0", 7));
    BOOST_CHECK_EQUAL(TrimString(std::string(" foo ", 5)), std::string("foo", 3));
    BOOST_CHECK_EQUAL(TrimString(std::string("\t\t\0\0\n\n", 6)), std::string("\0\0", 2));
    BOOST_CHECK_EQUAL(TrimString(std::string("\x05\x04\x03\x02\x01\x00", 6)), std::string("\x05\x04\x03\x02\x01\x00", 6));
    BOOST_CHECK_EQUAL(TrimString(std::string("\x05\x04\x03\x02\x01\x00", 6), std::string("\x05\x04\x03\x02\x01", 5)), std::string("\0", 1));
    BOOST_CHECK_EQUAL(TrimString(std::string("\x05\x04\x03\x02\x01\x00", 6), std::string("\x05\x04\x03\x02\x01\x00", 6)), "");
}

BOOST_AUTO_TEST_CASE(util_FormatParseISO8601DateTime)
{
    BOOST_CHECK_EQUAL(FormatISO8601DateTime(1317425777), "2011-09-30T23:36:17Z");
    BOOST_CHECK_EQUAL(FormatISO8601DateTime(0), "1970-01-01T00:00:00Z");

    BOOST_CHECK_EQUAL(ParseISO8601DateTime("1970-01-01T00:00:00Z"), 0);
    BOOST_CHECK_EQUAL(ParseISO8601DateTime("1960-01-01T00:00:00Z"), 0);
    BOOST_CHECK_EQUAL(ParseISO8601DateTime("2011-09-30T23:36:17Z"), 1317425777);

    auto time = GetTimeSeconds();
    BOOST_CHECK_EQUAL(ParseISO8601DateTime(FormatISO8601DateTime(time)), time);
}

BOOST_AUTO_TEST_CASE(util_FormatISO8601Date)
{
    BOOST_CHECK_EQUAL(FormatISO8601Date(1317425777), "2011-09-30");
}

struct TestArgsManager : public ArgsManager
{
    TestArgsManager() { m_network_only_args.clear(); }
    void ReadConfigString(const std::string str_config)
    {
        std::istringstream streamConfig(str_config);
        {
            LOCK(cs_args);
            m_settings.ro_config.clear();
            m_config_sections.clear();
        }
        std::string error;
        BOOST_REQUIRE(ReadConfigStream(streamConfig, "", error));
    }
    void SetNetworkOnlyArg(const std::string arg)
    {
        LOCK(cs_args);
        m_network_only_args.insert(arg);
    }
    void SetupArgs(const std::vector<std::pair<std::string, unsigned int>>& args)
    {
        for (const auto& arg : args) {
            AddArg(arg.first, "", arg.second, OptionsCategory::OPTIONS);
        }
    }
    using ArgsManager::GetSetting;
    using ArgsManager::GetSettingsList;
    using ArgsManager::ReadConfigStream;
    using ArgsManager::cs_args;
    using ArgsManager::m_network;
    using ArgsManager::m_settings;
};

//! Test GetSetting and GetArg type coercion, negation, and default value handling.
class CheckValueTest : public TestChain100Setup
{
public:
    struct Expect {
        util::SettingsValue setting;
        bool default_string = false;
        bool default_int = false;
        bool default_bool = false;
        const char* string_value = nullptr;
        std::optional<int64_t> int_value;
        std::optional<bool> bool_value;
        std::optional<std::vector<std::string>> list_value;
        const char* error = nullptr;

        explicit Expect(util::SettingsValue s) : setting(std::move(s)) {}
        Expect& DefaultString() { default_string = true; return *this; }
        Expect& DefaultInt() { default_int = true; return *this; }
        Expect& DefaultBool() { default_bool = true; return *this; }
        Expect& String(const char* s) { string_value = s; return *this; }
        Expect& Int(int64_t i) { int_value = i; return *this; }
        Expect& Bool(bool b) { bool_value = b; return *this; }
        Expect& List(std::vector<std::string> m) { list_value = std::move(m); return *this; }
        Expect& Error(const char* e) { error = e; return *this; }
    };

    void CheckValue(unsigned int flags, const char* arg, const Expect& expect)
    {
        TestArgsManager test;
        test.SetupArgs({{"-value", flags}});
        const char* argv[] = {"ignored", arg};
        std::string error;
        bool success = test.ParseParameters(arg ? 2 : 1, (char**)argv, error);

        BOOST_CHECK_EQUAL(test.GetSetting("-value").write(), expect.setting.write());
        auto settings_list = test.GetSettingsList("-value");
        if (expect.setting.isNull() || expect.setting.isFalse()) {
            BOOST_CHECK_EQUAL(settings_list.size(), 0U);
        } else {
            BOOST_CHECK_EQUAL(settings_list.size(), 1U);
            BOOST_CHECK_EQUAL(settings_list[0].write(), expect.setting.write());
        }

        if (expect.error) {
            BOOST_CHECK(!success);
            BOOST_CHECK_NE(error.find(expect.error), std::string::npos);
        } else {
            BOOST_CHECK(success);
            BOOST_CHECK_EQUAL(error, "");
        }

        if (expect.default_string) {
            BOOST_CHECK_EQUAL(test.GetArg("-value", "zzzzz"), "zzzzz");
        } else if (expect.string_value) {
            BOOST_CHECK_EQUAL(test.GetArg("-value", "zzzzz"), expect.string_value);
        } else {
            BOOST_CHECK(!success);
        }

        if (expect.default_int) {
            BOOST_CHECK_EQUAL(test.GetIntArg("-value", 99999), 99999);
        } else if (expect.int_value) {
            BOOST_CHECK_EQUAL(test.GetIntArg("-value", 99999), *expect.int_value);
        } else {
            BOOST_CHECK(!success);
        }

        if (expect.default_bool) {
            BOOST_CHECK_EQUAL(test.GetBoolArg("-value", false), false);
            BOOST_CHECK_EQUAL(test.GetBoolArg("-value", true), true);
        } else if (expect.bool_value) {
            BOOST_CHECK_EQUAL(test.GetBoolArg("-value", false), *expect.bool_value);
            BOOST_CHECK_EQUAL(test.GetBoolArg("-value", true), *expect.bool_value);
        } else {
            BOOST_CHECK(!success);
        }

        if (expect.list_value) {
            auto l = test.GetArgs("-value");
            BOOST_CHECK_EQUAL_COLLECTIONS(l.begin(), l.end(), expect.list_value->begin(), expect.list_value->end());
        } else {
            BOOST_CHECK(!success);
        }
    }
};

BOOST_FIXTURE_TEST_CASE(util_CheckValue, CheckValueTest)
{
    using M = ArgsManager;

    CheckValue(M::ALLOW_ANY, nullptr, Expect{{}}.DefaultString().DefaultInt().DefaultBool().List({}));
    CheckValue(M::ALLOW_ANY, "-novalue", Expect{false}.String("0").Int(0).Bool(false).List({}));
    CheckValue(M::ALLOW_ANY, "-novalue=", Expect{false}.String("0").Int(0).Bool(false).List({}));
    CheckValue(M::ALLOW_ANY, "-novalue=0", Expect{true}.String("1").Int(1).Bool(true).List({"1"}));
    CheckValue(M::ALLOW_ANY, "-novalue=1", Expect{false}.String("0").Int(0).Bool(false).List({}));
    CheckValue(M::ALLOW_ANY, "-novalue=2", Expect{false}.String("0").Int(0).Bool(false).List({}));
    CheckValue(M::ALLOW_ANY, "-novalue=abc", Expect{true}.String("1").Int(1).Bool(true).List({"1"}));
    CheckValue(M::ALLOW_ANY, "-value", Expect{""}.String("").Int(0).Bool(true).List({""}));
    CheckValue(M::ALLOW_ANY, "-value=", Expect{""}.String("").Int(0).Bool(true).List({""}));
    CheckValue(M::ALLOW_ANY, "-value=0", Expect{"0"}.String("0").Int(0).Bool(false).List({"0"}));
    CheckValue(M::ALLOW_ANY, "-value=1", Expect{"1"}.String("1").Int(1).Bool(true).List({"1"}));
    CheckValue(M::ALLOW_ANY, "-value=2", Expect{"2"}.String("2").Int(2).Bool(true).List({"2"}));
    CheckValue(M::ALLOW_ANY, "-value=abc", Expect{"abc"}.String("abc").Int(0).Bool(false).List({"abc"}));
}

struct NoIncludeConfTest {
    std::string Parse(const char* arg)
    {
        TestArgsManager test;
        test.SetupArgs({{"-includeconf", ArgsManager::ALLOW_ANY}});
        std::array argv{"ignored", arg};
        std::string error;
        (void)test.ParseParameters(argv.size(), argv.data(), error);
        return error;
    }
};

BOOST_FIXTURE_TEST_CASE(util_NoIncludeConf, NoIncludeConfTest)
{
    BOOST_CHECK_EQUAL(Parse("-noincludeconf"), "");
    BOOST_CHECK_EQUAL(Parse("-includeconf"), "-includeconf cannot be used from commandline; -includeconf=\"\"");
    BOOST_CHECK_EQUAL(Parse("-includeconf=file"), "-includeconf cannot be used from commandline; -includeconf=\"file\"");
}

BOOST_AUTO_TEST_CASE(util_ParseParameters)
{
    TestArgsManager testArgs;
    const auto a = std::make_pair("-a", ArgsManager::ALLOW_ANY);
    const auto b = std::make_pair("-b", ArgsManager::ALLOW_ANY);
    const auto ccc = std::make_pair("-ccc", ArgsManager::ALLOW_ANY);
    const auto d = std::make_pair("-d", ArgsManager::ALLOW_ANY);

    const char *argv_test[] = {"-ignored", "-a", "-b", "-ccc=argument", "-ccc=multiple", "f", "-d=e"};

    std::string error;
    LOCK(testArgs.cs_args);
    testArgs.SetupArgs({a, b, ccc, d});
    BOOST_CHECK(testArgs.ParseParameters(0, (char**)argv_test, error));
    BOOST_CHECK(testArgs.m_settings.command_line_options.empty() && testArgs.m_settings.ro_config.empty());

    BOOST_CHECK(testArgs.ParseParameters(1, (char**)argv_test, error));
    BOOST_CHECK(testArgs.m_settings.command_line_options.empty() && testArgs.m_settings.ro_config.empty());

    BOOST_CHECK(testArgs.ParseParameters(7, (char**)argv_test, error));
    // expectation: -ignored is ignored (program name argument),
    // -a, -b and -ccc end up in map, -d ignored because it is after
    // a non-option argument (non-GNU option parsing)
    BOOST_CHECK(testArgs.m_settings.command_line_options.size() == 3 && testArgs.m_settings.ro_config.empty());
    BOOST_CHECK(testArgs.IsArgSet("-a") && testArgs.IsArgSet("-b") && testArgs.IsArgSet("-ccc")
                && !testArgs.IsArgSet("f") && !testArgs.IsArgSet("-d"));
    BOOST_CHECK(testArgs.m_settings.command_line_options.count("a") && testArgs.m_settings.command_line_options.count("b") && testArgs.m_settings.command_line_options.count("ccc")
                && !testArgs.m_settings.command_line_options.count("f") && !testArgs.m_settings.command_line_options.count("d"));

    BOOST_CHECK(testArgs.m_settings.command_line_options["a"].size() == 1);
    BOOST_CHECK(testArgs.m_settings.command_line_options["a"].front().get_str() == "");
    BOOST_CHECK(testArgs.m_settings.command_line_options["ccc"].size() == 2);
    BOOST_CHECK(testArgs.m_settings.command_line_options["ccc"].front().get_str() == "argument");
    BOOST_CHECK(testArgs.m_settings.command_line_options["ccc"].back().get_str() == "multiple");
    BOOST_CHECK(testArgs.GetArgs("-ccc").size() == 2);
}

BOOST_AUTO_TEST_CASE(util_ParseInvalidParameters)
{
    TestArgsManager test;
    test.SetupArgs({{"-registered", ArgsManager::ALLOW_ANY}});

    const char* argv[] = {"ignored", "-registered"};
    std::string error;
    BOOST_CHECK(test.ParseParameters(2, (char**)argv, error));
    BOOST_CHECK_EQUAL(error, "");

    argv[1] = "-unregistered";
    BOOST_CHECK(!test.ParseParameters(2, (char**)argv, error));
    BOOST_CHECK_EQUAL(error, "Invalid parameter -unregistered");

    // Make sure registered parameters prefixed with a chain name trigger errors.
    // (Previously, they were accepted and ignored.)
    argv[1] = "-test.registered";
    BOOST_CHECK(!test.ParseParameters(2, (char**)argv, error));
    BOOST_CHECK_EQUAL(error, "Invalid parameter -test.registered");
}

static void TestParse(const std::string& str, bool expected_bool, int64_t expected_int)
{
    TestArgsManager test;
    test.SetupArgs({{"-value", ArgsManager::ALLOW_ANY}});
    std::string arg = "-value=" + str;
    const char* argv[] = {"ignored", arg.c_str()};
    std::string error;
    BOOST_CHECK(test.ParseParameters(2, (char**)argv, error));
    BOOST_CHECK_EQUAL(test.GetBoolArg("-value", false), expected_bool);
    BOOST_CHECK_EQUAL(test.GetBoolArg("-value", true), expected_bool);
    BOOST_CHECK_EQUAL(test.GetIntArg("-value", 99998), expected_int);
    BOOST_CHECK_EQUAL(test.GetIntArg("-value", 99999), expected_int);
}

// Test bool and int parsing.
BOOST_AUTO_TEST_CASE(util_ArgParsing)
{
    // Some of these cases could be ambiguous or surprising to users, and might
    // be worth triggering errors or warnings in the future. But for now basic
    // test coverage is useful to avoid breaking backwards compatibility
    // unintentionally.
    TestParse("", true, 0);
    TestParse(" ", false, 0);
    TestParse("0", false, 0);
    TestParse("0 ", false, 0);
    TestParse(" 0", false, 0);
    TestParse("+0", false, 0);
    TestParse("-0", false, 0);
    TestParse("5", true, 5);
    TestParse("5 ", true, 5);
    TestParse(" 5", true, 5);
    TestParse("+5", true, 5);
    TestParse("-5", true, -5);
    TestParse("0 5", false, 0);
    TestParse("5 0", true, 5);
    TestParse("050", true, 50);
    TestParse("0.", false, 0);
    TestParse("5.", true, 5);
    TestParse("0.0", false, 0);
    TestParse("0.5", false, 0);
    TestParse("5.0", true, 5);
    TestParse("5.5", true, 5);
    TestParse("x", false, 0);
    TestParse("x0", false, 0);
    TestParse("x5", false, 0);
    TestParse("0x", false, 0);
    TestParse("5x", true, 5);
    TestParse("0x5", false, 0);
    TestParse("false", false, 0);
    TestParse("true", false, 0);
    TestParse("yes", false, 0);
    TestParse("no", false, 0);
}

BOOST_AUTO_TEST_CASE(util_GetBoolArg)
{
    TestArgsManager testArgs;
    const auto a = std::make_pair("-a", ArgsManager::ALLOW_ANY);
    const auto b = std::make_pair("-b", ArgsManager::ALLOW_ANY);
    const auto c = std::make_pair("-c", ArgsManager::ALLOW_ANY);
    const auto d = std::make_pair("-d", ArgsManager::ALLOW_ANY);
    const auto e = std::make_pair("-e", ArgsManager::ALLOW_ANY);
    const auto f = std::make_pair("-f", ArgsManager::ALLOW_ANY);

    const char *argv_test[] = {
        "ignored", "-a", "-nob", "-c=0", "-d=1", "-e=false", "-f=true"};
    std::string error;
    LOCK(testArgs.cs_args);
    testArgs.SetupArgs({a, b, c, d, e, f});
    BOOST_CHECK(testArgs.ParseParameters(7, (char**)argv_test, error));

    // Each letter should be set.
    for (const char opt : "abcdef")
        BOOST_CHECK(testArgs.IsArgSet({'-', opt}) || !opt);

    // Nothing else should be in the map
    BOOST_CHECK(testArgs.m_settings.command_line_options.size() == 6 &&
                testArgs.m_settings.ro_config.empty());

    // The -no prefix should get stripped on the way in.
    BOOST_CHECK(!testArgs.IsArgSet("-nob"));

    // The -b option is flagged as negated, and nothing else is
    BOOST_CHECK(testArgs.IsArgNegated("-b"));
    BOOST_CHECK(!testArgs.IsArgNegated("-a"));

    // Check expected values.
    BOOST_CHECK(testArgs.GetBoolArg("-a", false) == true);
    BOOST_CHECK(testArgs.GetBoolArg("-b", true) == false);
    BOOST_CHECK(testArgs.GetBoolArg("-c", true) == false);
    BOOST_CHECK(testArgs.GetBoolArg("-d", false) == true);
    BOOST_CHECK(testArgs.GetBoolArg("-e", true) == false);
    BOOST_CHECK(testArgs.GetBoolArg("-f", true) == false);
}

BOOST_AUTO_TEST_CASE(util_GetBoolArgEdgeCases)
{
    // Test some awful edge cases that hopefully no user will ever exercise.
    TestArgsManager testArgs;

    // Params test
    const auto foo = std::make_pair("-foo", ArgsManager::ALLOW_ANY);
    const auto bar = std::make_pair("-bar", ArgsManager::ALLOW_ANY);
    const char *argv_test[] = {"ignored", "-nofoo", "-foo", "-nobar=0"};
    testArgs.SetupArgs({foo, bar});
    std::string error;
    BOOST_CHECK(testArgs.ParseParameters(4, (char**)argv_test, error));

    // This was passed twice, second one overrides the negative setting.
    BOOST_CHECK(!testArgs.IsArgNegated("-foo"));
    BOOST_CHECK(testArgs.GetArg("-foo", "xxx") == "");

    // A double negative is a positive, and not marked as negated.
    BOOST_CHECK(!testArgs.IsArgNegated("-bar"));
    BOOST_CHECK(testArgs.GetArg("-bar", "xxx") == "1");

    // Config test
    const char *conf_test = "nofoo=1\nfoo=1\nnobar=0\n";
    BOOST_CHECK(testArgs.ParseParameters(1, (char**)argv_test, error));
    testArgs.ReadConfigString(conf_test);

    // This was passed twice, second one overrides the negative setting,
    // and the value.
    BOOST_CHECK(!testArgs.IsArgNegated("-foo"));
    BOOST_CHECK(testArgs.GetArg("-foo", "xxx") == "1");

    // A double negative is a positive, and does not count as negated.
    BOOST_CHECK(!testArgs.IsArgNegated("-bar"));
    BOOST_CHECK(testArgs.GetArg("-bar", "xxx") == "1");

    // Combined test
    const char *combo_test_args[] = {"ignored", "-nofoo", "-bar"};
    const char *combo_test_conf = "foo=1\nnobar=1\n";
    BOOST_CHECK(testArgs.ParseParameters(3, (char**)combo_test_args, error));
    testArgs.ReadConfigString(combo_test_conf);

    // Command line overrides, but doesn't erase old setting
    BOOST_CHECK(testArgs.IsArgNegated("-foo"));
    BOOST_CHECK(testArgs.GetArg("-foo", "xxx") == "0");
    BOOST_CHECK(testArgs.GetArgs("-foo").size() == 0);

    // Command line overrides, but doesn't erase old setting
    BOOST_CHECK(!testArgs.IsArgNegated("-bar"));
    BOOST_CHECK(testArgs.GetArg("-bar", "xxx") == "");
    BOOST_CHECK(testArgs.GetArgs("-bar").size() == 1
                && testArgs.GetArgs("-bar").front() == "");
}

BOOST_AUTO_TEST_CASE(util_ReadConfigStream)
{
    const char *str_config =
       "a=\n"
       "b=1\n"
       "ccc=argument\n"
       "ccc=multiple\n"
       "d=e\n"
       "nofff=1\n"
       "noggg=0\n"
       "h=1\n"
       "noh=1\n"
       "noi=1\n"
       "i=1\n"
       "sec1.ccc=extend1\n"
       "\n"
       "[sec1]\n"
       "ccc=extend2\n"
       "d=eee\n"
       "h=1\n"
       "[sec2]\n"
       "ccc=extend3\n"
       "iii=2\n";

    TestArgsManager test_args;
    LOCK(test_args.cs_args);
    const auto a = std::make_pair("-a", ArgsManager::ALLOW_ANY);
    const auto b = std::make_pair("-b", ArgsManager::ALLOW_ANY);
    const auto ccc = std::make_pair("-ccc", ArgsManager::ALLOW_ANY);
    const auto d = std::make_pair("-d", ArgsManager::ALLOW_ANY);
    const auto e = std::make_pair("-e", ArgsManager::ALLOW_ANY);
    const auto fff = std::make_pair("-fff", ArgsManager::ALLOW_ANY);
    const auto ggg = std::make_pair("-ggg", ArgsManager::ALLOW_ANY);
    const auto h = std::make_pair("-h", ArgsManager::ALLOW_ANY);
    const auto i = std::make_pair("-i", ArgsManager::ALLOW_ANY);
    const auto iii = std::make_pair("-iii", ArgsManager::ALLOW_ANY);
    test_args.SetupArgs({a, b, ccc, d, e, fff, ggg, h, i, iii});

    test_args.ReadConfigString(str_config);
    // expectation: a, b, ccc, d, fff, ggg, h, i end up in map
    // so do sec1.ccc, sec1.d, sec1.h, sec2.ccc, sec2.iii

    BOOST_CHECK(test_args.m_settings.command_line_options.empty());
    BOOST_CHECK(test_args.m_settings.ro_config.size() == 3);
    BOOST_CHECK(test_args.m_settings.ro_config[""].size() == 8);
    BOOST_CHECK(test_args.m_settings.ro_config["sec1"].size() == 3);
    BOOST_CHECK(test_args.m_settings.ro_config["sec2"].size() == 2);

    BOOST_CHECK(test_args.m_settings.ro_config[""].count("a"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("b"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("ccc"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("d"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("fff"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("ggg"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("h"));
    BOOST_CHECK(test_args.m_settings.ro_config[""].count("i"));
    BOOST_CHECK(test_args.m_settings.ro_config["sec1"].count("ccc"));
    BOOST_CHECK(test_args.m_settings.ro_config["sec1"].count("h"));
    BOOST_CHECK(test_args.m_settings.ro_config["sec2"].count("ccc"));
    BOOST_CHECK(test_args.m_settings.ro_config["sec2"].count("iii"));

    BOOST_CHECK(test_args.IsArgSet("-a"));
    BOOST_CHECK(test_args.IsArgSet("-b"));
    BOOST_CHECK(test_args.IsArgSet("-ccc"));
    BOOST_CHECK(test_args.IsArgSet("-d"));
    BOOST_CHECK(test_args.IsArgSet("-fff"));
    BOOST_CHECK(test_args.IsArgSet("-ggg"));
    BOOST_CHECK(test_args.IsArgSet("-h"));
    BOOST_CHECK(test_args.IsArgSet("-i"));
    BOOST_CHECK(!test_args.IsArgSet("-zzz"));
    BOOST_CHECK(!test_args.IsArgSet("-iii"));

    BOOST_CHECK_EQUAL(test_args.GetArg("-a", "xxx"), "");
    BOOST_CHECK_EQUAL(test_args.GetArg("-b", "xxx"), "1");
    BOOST_CHECK_EQUAL(test_args.GetArg("-ccc", "xxx"), "argument");
    BOOST_CHECK_EQUAL(test_args.GetArg("-d", "xxx"), "e");
    BOOST_CHECK_EQUAL(test_args.GetArg("-fff", "xxx"), "0");
    BOOST_CHECK_EQUAL(test_args.GetArg("-ggg", "xxx"), "1");
    BOOST_CHECK_EQUAL(test_args.GetArg("-h", "xxx"), "0");
    BOOST_CHECK_EQUAL(test_args.GetArg("-i", "xxx"), "1");
    BOOST_CHECK_EQUAL(test_args.GetArg("-zzz", "xxx"), "xxx");
    BOOST_CHECK_EQUAL(test_args.GetArg("-iii", "xxx"), "xxx");

    for (const bool def : {false, true}) {
        BOOST_CHECK(test_args.GetBoolArg("-a", def));
        BOOST_CHECK(test_args.GetBoolArg("-b", def));
        BOOST_CHECK(!test_args.GetBoolArg("-ccc", def));
        BOOST_CHECK(!test_args.GetBoolArg("-d", def));
        BOOST_CHECK(!test_args.GetBoolArg("-fff", def));
        BOOST_CHECK(test_args.GetBoolArg("-ggg", def));
        BOOST_CHECK(!test_args.GetBoolArg("-h", def));
        BOOST_CHECK(test_args.GetBoolArg("-i", def));
        BOOST_CHECK(test_args.GetBoolArg("-zzz", def) == def);
        BOOST_CHECK(test_args.GetBoolArg("-iii", def) == def);
    }

    BOOST_CHECK(test_args.GetArgs("-a").size() == 1
                && test_args.GetArgs("-a").front() == "");
    BOOST_CHECK(test_args.GetArgs("-b").size() == 1
                && test_args.GetArgs("-b").front() == "1");
    BOOST_CHECK(test_args.GetArgs("-ccc").size() == 2
                && test_args.GetArgs("-ccc").front() == "argument"
                && test_args.GetArgs("-ccc").back() == "multiple");
    BOOST_CHECK(test_args.GetArgs("-fff").size() == 0);
    BOOST_CHECK(test_args.GetArgs("-nofff").size() == 0);
    BOOST_CHECK(test_args.GetArgs("-ggg").size() == 1
                && test_args.GetArgs("-ggg").front() == "1");
    BOOST_CHECK(test_args.GetArgs("-noggg").size() == 0);
    BOOST_CHECK(test_args.GetArgs("-h").size() == 0);
    BOOST_CHECK(test_args.GetArgs("-noh").size() == 0);
    BOOST_CHECK(test_args.GetArgs("-i").size() == 1
                && test_args.GetArgs("-i").front() == "1");
    BOOST_CHECK(test_args.GetArgs("-noi").size() == 0);
    BOOST_CHECK(test_args.GetArgs("-zzz").size() == 0);

    BOOST_CHECK(!test_args.IsArgNegated("-a"));
    BOOST_CHECK(!test_args.IsArgNegated("-b"));
    BOOST_CHECK(!test_args.IsArgNegated("-ccc"));
    BOOST_CHECK(!test_args.IsArgNegated("-d"));
    BOOST_CHECK(test_args.IsArgNegated("-fff"));
    BOOST_CHECK(!test_args.IsArgNegated("-ggg"));
    BOOST_CHECK(test_args.IsArgNegated("-h")); // last setting takes precedence
    BOOST_CHECK(!test_args.IsArgNegated("-i")); // last setting takes precedence
    BOOST_CHECK(!test_args.IsArgNegated("-zzz"));

    // Test sections work
    test_args.SelectConfigNetwork("sec1");

    // same as original
    BOOST_CHECK_EQUAL(test_args.GetArg("-a", "xxx"), "");
    BOOST_CHECK_EQUAL(test_args.GetArg("-b", "xxx"), "1");
    BOOST_CHECK_EQUAL(test_args.GetArg("-fff", "xxx"), "0");
    BOOST_CHECK_EQUAL(test_args.GetArg("-ggg", "xxx"), "1");
    BOOST_CHECK_EQUAL(test_args.GetArg("-zzz", "xxx"), "xxx");
    BOOST_CHECK_EQUAL(test_args.GetArg("-iii", "xxx"), "xxx");
    // d is overridden
    BOOST_CHECK(test_args.GetArg("-d", "xxx") == "eee");
    // section-specific setting
    BOOST_CHECK(test_args.GetArg("-h", "xxx") == "1");
    // section takes priority for multiple values
    BOOST_CHECK(test_args.GetArg("-ccc", "xxx") == "extend1");
    // check multiple values works
    const std::vector<std::string> sec1_ccc_expected = {"extend1","extend2","argument","multiple"};
    const auto& sec1_ccc_res = test_args.GetArgs("-ccc");
    BOOST_CHECK_EQUAL_COLLECTIONS(sec1_ccc_res.begin(), sec1_ccc_res.end(), sec1_ccc_expected.begin(), sec1_ccc_expected.end());

    test_args.SelectConfigNetwork("sec2");
