// The ladder is the one place that decides whether a panel has lyrics at all. Each rung here is
// a situation a listener was in on 2026-11-21: the companion quit, the mirror was up, nothing was
// typed - and the screen stayed blank. These tests say what each rung must do.
#include <net/lyrics_ladder.h>
#include <unity.h>

#include <cstring>
#include <string>

using S = std::string;
using Choice = net::ladder::Choice<S>;
using Kind = Choice::Kind;

static Choice choose(const char *setting, const char *companion, const char *api, const char *host) {
    return net::ladder::choose<S>(setting, companion, api, host);
}

void setUp() {}
void tearDown() {}

static const char *HOST = "tft-panel-1800";

void test_off_asks_nobody_and_says_so() {
    Choice c = choose("none", "http://192.168.2.57:3888", "get", HOST);
    TEST_ASSERT_EQUAL(Kind::Off, c.kind);
    TEST_ASSERT_TRUE(c.base.empty());
    TEST_ASSERT_EQUAL_STRING("Lyrics are turned off for this panel", c.why.c_str());
}

void test_companion_wins_over_a_typed_server() {
    Choice c = choose("http://192.168.2.57:3300", "http://192.168.2.57:3888/", "get", HOST);
    TEST_ASSERT_EQUAL(Kind::Companion, c.kind);
    TEST_ASSERT_EQUAL_STRING("http://192.168.2.57:3888", c.base.c_str());
    TEST_ASSERT_TRUE(c.getApi);
    TEST_ASSERT_TRUE(c.why.empty());
}

void test_an_old_companion_is_asked_the_old_way() {
    Choice c = choose("", "http://192.168.2.30:3888", "", HOST);
    TEST_ASSERT_EQUAL(Kind::Companion, c.kind);
    TEST_ASSERT_FALSE(c.getApi);
}

void test_the_typed_server_is_the_fallback() {
    Choice c = choose("http://192.168.2.57:3300/", "", "", HOST);
    TEST_ASSERT_EQUAL(Kind::Server, c.kind);
    TEST_ASSERT_EQUAL_STRING("http://192.168.2.57:3300", c.base.c_str());
    TEST_ASSERT_TRUE(c.why.empty());
}

void test_nothing_at_all_names_what_is_missing_and_where() {
    Choice c = choose("", "", "", HOST);
    TEST_ASSERT_EQUAL(Kind::None, c.kind);
    TEST_ASSERT_TRUE(c.base.empty());
    TEST_ASSERT_NOT_NULL(strstr(c.why.c_str(), "No lyrics source"));
    TEST_ASSERT_NOT_NULL(strstr(c.why.c_str(), "No companion"));
    TEST_ASSERT_NOT_NULL(strstr(c.why.c_str(), "no fallback lyrics server is set"));
    TEST_ASSERT_NOT_NULL(strstr(c.why.c_str(), "http://tft-panel-1800.local"));
}

void test_off_beats_everything_including_a_companion() {
    // "none" is a decision the listener made; a companion appearing does not overturn it.
    Choice c = choose("none", "", "", HOST);
    TEST_ASSERT_EQUAL(Kind::Off, c.kind);
}

void test_api_key_is_exact() {
    TEST_ASSERT_FALSE(choose("", "http://h:3888", "GET", HOST).getApi);
    TEST_ASSERT_FALSE(choose("", "http://h:3888", "get2", HOST).getApi);
    TEST_ASSERT_TRUE(choose("", "http://h:3888", "get", HOST).getApi);
}

void test_slashes_are_stripped_once_for_all() {
    TEST_ASSERT_EQUAL_STRING("http://h:3300", choose("http://h:3300///", "", "", HOST).base.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_off_asks_nobody_and_says_so);
    RUN_TEST(test_companion_wins_over_a_typed_server);
    RUN_TEST(test_an_old_companion_is_asked_the_old_way);
    RUN_TEST(test_the_typed_server_is_the_fallback);
    RUN_TEST(test_nothing_at_all_names_what_is_missing_and_where);
    RUN_TEST(test_off_beats_everything_including_a_companion);
    RUN_TEST(test_api_key_is_exact);
    RUN_TEST(test_slashes_are_stripped_once_for_all);
    return UNITY_END();
}
