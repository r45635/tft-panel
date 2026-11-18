#include <net/version_compare.h>
#include <unity.h>

using ota::isNewer;

void setUp() {}
void tearDown() {}

void test_release_numbers_compare_numerically() {
    TEST_ASSERT_TRUE(isNewer("v1.2.4", "v1.2.3"));
    TEST_ASSERT_FALSE(isNewer("v1.2.3", "v1.2.4"));
    TEST_ASSERT_TRUE(isNewer("v1.10.0", "v1.9.9"));
    TEST_ASSERT_FALSE(isNewer("v1.2.3", "v1.2.3"));
}

void test_a_dev_build_is_only_updated_by_a_later_release() {
    TEST_ASSERT_FALSE(isNewer("v1.2.3", "v1.2.3-4-gabc1234"));
    TEST_ASSERT_TRUE(isNewer("v1.2.4", "v1.2.3-4-gabc1234"));
}

void test_a_tag_with_a_suffix_is_never_offered() {
    TEST_ASSERT_FALSE(isNewer("v2.0.0-rc1", "v1.0.0"));
}

void test_unparseable_versions() {
    TEST_ASSERT_TRUE(isNewer("v1.0.0", "unknown"));
    TEST_ASSERT_FALSE(isNewer("latest", "v1.0.0"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_release_numbers_compare_numerically);
    RUN_TEST(test_a_dev_build_is_only_updated_by_a_later_release);
    RUN_TEST(test_a_tag_with_a_suffix_is_never_offered);
    RUN_TEST(test_unparseable_versions);
    return UNITY_END();
}
