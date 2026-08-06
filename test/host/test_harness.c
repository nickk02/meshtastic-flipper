/* Tests for the test harness itself. If these do not behave, nothing else
   proves anything. */
#include "tinytest.h"

TEST(test_assert_true_passes) {
    ASSERT_TRUE(1 == 1);
}

TEST(test_assert_eq_int_passes) {
    ASSERT_EQ_INT(2 + 2, 4);
}

TEST(test_assert_eq_mem_passes) {
    const unsigned char a[3] = {1, 2, 3};
    const unsigned char b[3] = {1, 2, 3};
    ASSERT_EQ_MEM(a, b, 3);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_assert_true_passes);
RUN_TEST(test_assert_eq_int_passes);
RUN_TEST(test_assert_eq_mem_passes);
TEST_MAIN_END()
