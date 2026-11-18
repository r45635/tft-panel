#include <net/position_clock.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// True position is t - 4700 throughout; the panel only ever sees whole seconds.
void test_polled_samples_narrow_to_the_true_offset() {
    PositionClock c;
    c.observe(5000, 10000, true, 0);
    TEST_ASSERT_EQUAL_INT32(1000, c.uncertaintyMs());
    c.observe(5000, 10650, true, 0);
    c.observe(6000, 10750, true, 0);
    TEST_ASSERT_EQUAL_INT32(100, c.uncertaintyMs());
    TEST_ASSERT_EQUAL_INT32(15300, c.positionAt(20000));
}

void test_a_seek_restarts_the_window() {
    PositionClock c;
    c.observe(5000, 10000, true, 0);
    c.observe(6000, 10750, true, 0);
    c.observe(60000, 11000, true, 0);
    TEST_ASSERT_EQUAL_INT32(1000, c.uncertaintyMs());
    TEST_ASSERT_EQUAL_INT32(60500, c.positionAt(11000));
}

void test_pushed_position_is_the_second_itself_polled_is_mid_second() {
    PositionClock pushed, polled;
    pushed.observe(5000, 10000, true, 20, 0);
    polled.observe(5000, 10000, true, 20);
    TEST_ASSERT_EQUAL_INT32(40, pushed.uncertaintyMs());
    TEST_ASSERT_EQUAL_INT32(5000, pushed.positionAt(10000));
    TEST_ASSERT_EQUAL_INT32(5500, polled.positionAt(10000));
}

void test_paused_holds_the_reported_position() {
    PositionClock c;
    c.observe(5000, 10000, true, 0);
    c.observe(42000, 20000, false, 0);
    TEST_ASSERT_FALSE(c.running());
    TEST_ASSERT_EQUAL_INT32(-1, c.uncertaintyMs());
    TEST_ASSERT_EQUAL_INT32(42000, c.positionAt(90000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_polled_samples_narrow_to_the_true_offset);
    RUN_TEST(test_a_seek_restarts_the_window);
    RUN_TEST(test_pushed_position_is_the_second_itself_polled_is_mid_second);
    RUN_TEST(test_paused_holds_the_reported_position);
    return UNITY_END();
}
