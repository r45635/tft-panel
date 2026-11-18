#include <app/lyrics.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static const char *LRC =
    "[ar:Nobody]\n"
    "[00:12.50][01:00]Paper boats\r\n"
    "[00:05.1]Morning tide\n"
    "\n"
    "[00:30.123]Salt on the glass\n";

void test_parse_sorts_stamps() {
    lyrics::Lyrics l = lyrics::parse(LRC);
    TEST_ASSERT_TRUE(l.synced);
    TEST_ASSERT_EQUAL(4, l.lines.size());
    const int32_t times[] = {5100, 12500, 30123, 60000};
    const char *texts[] = {"Morning tide", "Paper boats", "Salt on the glass", "Paper boats"};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT32(times[i], l.lines[i].timeMs);
        TEST_ASSERT_EQUAL_STRING(texts[i], l.lines[i].text.c_str());
    }
}

void test_current_line_switches_exactly_on_its_stamp() {
    lyrics::Lyrics l = lyrics::parse(LRC);
    TEST_ASSERT_EQUAL(-1, lyrics::currentIndex(l, 5099));
    TEST_ASSERT_EQUAL(0, lyrics::currentIndex(l, 5100));
    TEST_ASSERT_EQUAL(0, lyrics::currentIndex(l, 12499));
    TEST_ASSERT_EQUAL(1, lyrics::currentIndex(l, 12500));
    TEST_ASSERT_EQUAL(3, lyrics::currentIndex(l, 600000));
}

void test_unsynced_text_stays_plain_without_its_tags() {
    lyrics::Lyrics l = lyrics::parse("[ti:Nothing Here]\nLine one\n\nLine two\n\n");
    TEST_ASSERT_FALSE(l.synced);
    TEST_ASSERT_EQUAL(3, l.lines.size());
    TEST_ASSERT_EQUAL_STRING("Line one", l.lines[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("", l.lines[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("Line two", l.lines[2].text.c_str());
    TEST_ASSERT_EQUAL(-1, lyrics::currentIndex(l, 10000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_sorts_stamps);
    RUN_TEST(test_current_line_switches_exactly_on_its_stamp);
    RUN_TEST(test_unsynced_text_stays_plain_without_its_tags);
    return UNITY_END();
}
