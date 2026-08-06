/* Minimal assert harness for the host tests.
 *
 * No dependencies beyond the C standard library so it runs anywhere gcc does.
 * Include it in one translation unit per test binary. */
#ifndef TINYTEST_H
#define TINYTEST_H

#include <stdio.h>
#include <string.h>

static int tt_failures = 0;
static int tt_checks = 0;
static const char* tt_current = "";

#define TEST(name) static void name(void)

#define RUN_TEST(name)      \
    do {                    \
        tt_current = #name; \
        name();             \
    } while(0)

#define TT_FAIL(fmt, ...)                                                    \
    do {                                                                     \
        tt_failures++;                                                       \
        printf("FAIL %s (%s:%d): " fmt "\n", tt_current, __FILE__, __LINE__, \
               ##__VA_ARGS__);                                               \
    } while(0)

#define ASSERT_TRUE(cond)                 \
    do {                                  \
        tt_checks++;                      \
        if(!(cond)) TT_FAIL("%s", #cond); \
    } while(0)

#define ASSERT_EQ_INT(a, b)                                           \
    do {                                                              \
        long long tt_a = (long long)(a);                              \
        long long tt_b = (long long)(b);                              \
        tt_checks++;                                                  \
        if(tt_a != tt_b) TT_FAIL("%s: %lld != %lld", #a, tt_a, tt_b); \
    } while(0)

/* static inline so a test file that never compares buffers does not trip
   -Wunused-function under -Werror. */
static inline void tt_dump(const char* label, const unsigned char* p, size_t n) {
    printf("  %s:", label);
    for(size_t i = 0; i < n; i++) printf(" %02x", p[i]);
    printf("\n");
}

#define ASSERT_EQ_MEM(a, b, len)                                       \
    do {                                                               \
        tt_checks++;                                                   \
        if(memcmp((a), (b), (len)) != 0) {                             \
            TT_FAIL("%s != %s over %d bytes", #a, #b, (int)(len));     \
            tt_dump("got     ", (const unsigned char*)(a), (len));     \
            tt_dump("expected", (const unsigned char*)(b), (len));     \
        }                                                              \
    } while(0)

#define TEST_MAIN_BEGIN() int main(void) {

#define TEST_MAIN_END()                                                \
    printf("%s: %d checks, %d failures\n",                             \
           tt_failures ? "FAILED" : "PASSED", tt_checks, tt_failures); \
    return tt_failures ? 1 : 0;                                        \
    }

#endif
