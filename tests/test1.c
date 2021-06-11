#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioc_hello_queue.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "assert.h"
#include "stdio.h"

typedef int (*test_func)(void);

typedef struct {
    char *name;
    test_func func;
    int status;
} test_case;

char *driver_up = "service up /service/hello_queue -dev /dev/hello_queue";
char *driver_down = "service down hello_queue";

#define ASSERT(pred, msg)                                            \
    do {                                                             \
        if (!(pred)) {                                               \
            printf("assertion failed %s: %d\n", __FILE__, __LINE__); \
            printf("message: %s\n", msg);                            \
            return 1;                                                \
        }                                                            \
    } while (0)

#define ASSERT_NOT(pred, msg) ASSERT(!(pred), msg)

#define ASSERT_EQ(expected, actual)                                  \
    do {                                                             \
        long long int e = expected, a = actual;                      \
        if ((e) != (a)) {                                            \
            printf("assertion failed %s: %d\n", __FILE__, __LINE__); \
            printf("expected: %lld, actual: %lld\n", e, a);          \
            return 1;                                                \
        }                                                            \
    } while (0)

#define ASSERT_NEQ(notexpected, actual)                              \
    do {                                                             \
        long long int e = notexpected, a = actual;                   \
        if ((e) == (a)) {                                            \
            printf("assertion failed %s: %d\n", __FILE__, __LINE__); \
            printf("notexpected: %lld, actual: %lld\n", e, a);       \
            return 1;                                                \
        }                                                            \
    } while (0)

#define ASSERT_MEM_EQ(expected, actual, n)                           \
    do {                                                             \
        char *e = expected, *a = actual;                             \
        if (memcmp(e, a, n) != 0) {                                  \
            printf("assertion failed %s: %d\n", __FILE__, __LINE__); \
            printf("expected: %.*s, actual %.*s\n", n, e, n, a);     \
            return 1;                                                \
        }                                                            \
    } while (0)

#define START_DRIVER() ASSERT(system(driver_up) == 0, "cannot start driver")

#define BUFFER_SIZE 1000000
char buffer[BUFFER_SIZE];
int fd;

// Initialize test with empty queue.
#define TEST_INIT()                                                   \
    do {                                                              \
        START_DRIVER();                                               \
        fd = open("/dev/hello_queue", O_RDWR);                        \
        ASSERT_NOT(fd < 0, "cannot open hello_queue");                \
        ASSERT(0 < read(fd, buffer, BUFFER_SIZE), "init read error"); \
    } while (0)

// TESTS.
int test_init_state() {
    START_DRIVER();

    fd = open("/dev/hello_queue", O_RDWR);
    ASSERT_NOT(fd < 0, "cannot open hello_queue");

    int size = read(fd, buffer, BUFFER_SIZE);
    ASSERT_EQ(61, size);

    char *pattern = "xyz";
    for (size_t i = 0; i < size; i++) {
        ASSERT(buffer[i] == pattern[i % 3], "wrong xyz pattern");
    }

    ASSERT_EQ(0, read(fd, buffer, BUFFER_SIZE));

    return 0;
}

int test_read_write_simple() {
    TEST_INIT();

    ASSERT_EQ(10, write(fd, "0123456789", 10));

    ASSERT_EQ(5, read(fd, buffer, 5));
    ASSERT_MEM_EQ("01234", buffer, 5);

    ASSERT_EQ(5, read(fd, buffer, 5));
    ASSERT_MEM_EQ("56789", buffer, 5);

    ASSERT_EQ(0, read(fd, buffer, 5));

    return 0;
}

int test_read_write_blocks() {
    TEST_INIT();

    int n = 1000;
    char block[3];
    char pattern[] = {'a', 'b', 'c'};

    // Save blocks of the same letter in the queue.
    for (size_t i = 0; i < n; i++) {
        memset(block, pattern[i % sizeof(pattern)], sizeof(block));
        ASSERT_EQ(sizeof(block), write(fd, block, sizeof(block)));
    }

    // Read blocks of the same letter.
    for (size_t i = 0; i < n; i++) {
        memset(block, pattern[i % sizeof(pattern)], sizeof(block));
        ASSERT_EQ(sizeof(block), read(fd, buffer, sizeof(block)));
        ASSERT_MEM_EQ(block, buffer, sizeof(block));
    }

    ASSERT_EQ(0, read(fd, buffer, BUFFER_SIZE));

    return 0;
}

int test_iocres_1() {
    TEST_INIT();

    // Test queue reset many times.
    for (size_t i = 0; i < 10; i++) {
        ASSERT_NEQ(-1, ioctl(fd, HQIOCRES));

        int size = read(fd, buffer, BUFFER_SIZE);
        ASSERT_EQ(61, size);

        char *pattern = "xyz";
        for (size_t i = 0; i < size; i++) {
            ASSERT(buffer[i] == pattern[i % 3], "wrong xyz pattern");
        }

        ASSERT_EQ(0, read(fd, buffer, BUFFER_SIZE));
    }

    return 0;
}

int test_iocres_2() {
    TEST_INIT();

    // Insert a lot of bytes into queue.
    int n = 40000;
    memset(buffer, 'x', n);
    ASSERT_EQ(n, write(fd, buffer, n));

    // Reset queue and check if it.
    ASSERT_NEQ(-1, ioctl(fd, HQIOCRES));

    int size = read(fd, buffer, BUFFER_SIZE);
    ASSERT_EQ(61, size);

    char *pattern = "xyz";
    for (size_t i = 0; i < size; i++) {
        ASSERT(buffer[i] == pattern[i % 3], "wrong xyz pattern");
    }

    ASSERT_EQ(0, read(fd, buffer, BUFFER_SIZE));

    return 0;
}

int test_ioset_1() {
    TEST_INIT();

    // REMEMBER ABOUT \0!!!
    char orig[] = "0123456789";
    char changed[] = "0123abcdef";
    char suffix[7] = "abcdef";

    ASSERT_EQ(11, write(fd, orig, sizeof(orig)));
    ASSERT_NEQ(-1, ioctl(fd, HQIOCSET, suffix));

    ASSERT_EQ(11, read(fd, buffer, BUFFER_SIZE));
    ASSERT_MEM_EQ(changed, buffer, 11);

    return 0;
}

int test_ioset_2() {
    TEST_INIT();

    char msg[7] = "123456";
    ASSERT_NEQ(-1, ioctl(fd, HQIOCSET, msg));
    ASSERT_EQ(7, read(fd, buffer, BUFFER_SIZE));

    ASSERT_MEM_EQ(msg, buffer, 7);

    return 0;
}

int test_ioset_3() {
    START_DRIVER();
    fd = open("/dev/hello_queue", O_RDWR);
    ASSERT_NEQ(-1, fd);

    // Empty queueu slowly, so it can raect to change size.
    while (read(fd, buffer, 1) > 0) {
    }

    char msg[7] = "123456";
    ASSERT_NEQ(-1, ioctl(fd, HQIOCSET, msg));
    ASSERT_EQ(7, read(fd, buffer, BUFFER_SIZE));

    ASSERT_MEM_EQ(msg, buffer, 7);

    return 0;
}

int test_read_slowly() {
    START_DRIVER();
    fd = open("/dev/hello_queue", O_RDWR);
    ASSERT_NEQ(-1, fd);

    int n = 10;
    char *pattern = "xyz";
    char buf[300];
    for (size_t k = 0; k < sizeof(buf); k++) {
        buf[k] = pattern[k % 3];
    }

    for (size_t i = 0; i < n; i++) {
        // printf("i=%d\n",i);

        // Empty queueu slowly, so it can raect to change size.
        int j = 0;
        while (read(fd, buffer, 1) > 0) {
            // printf("j=%d\n",j);
            ASSERT_EQ(pattern[j % 3], buffer[0]);
            j++;
        }

        ASSERT_EQ(sizeof(buf), write(fd, buf, sizeof(buf)));
    }

    return 0;
}

int test_xch() {
    START_DRIVER();
    fd = open("/dev/hello_queue", O_RDWR);
    ASSERT_NEQ(-1, fd);

    char xch[2] = {'x', 'a'};

    ASSERT_NEQ(-1, ioctl(fd, HQIOCXCH, xch));
    ASSERT_EQ(61, read(fd, buffer, BUFFER_SIZE));

    char *pattern = "ayz";
    for (size_t i = 0; i < 61; i++) {
        ASSERT_EQ(pattern[i % 3], buffer[i]);
    }

    return 0;
}

int test_del() {
    START_DRIVER();
    fd = open("/dev/hello_queue", O_RDWR);
    ASSERT_NEQ(-1, fd);

    ASSERT_NEQ(-1, ioctl(fd, HQIOCDEL));

    ASSERT_EQ(41, read(fd, buffer, BUFFER_SIZE));

    char *pattern = "xy";
    for (size_t i = 0; i < 41; i++) {
        ASSERT_EQ(pattern[i % 2], buffer[i]);
    }

    return 0;
}

// ALL TESTS IN THIS ARRAY WILL RUN.
test_case tests[] = {
    {"test_init_state", &test_init_state},
    {"test_read_write_simple", &test_read_write_simple},
    {"test_read_write_blocks", &test_read_write_blocks},
    {"test_read_slowly", &test_read_slowly},
    {"test_iocres_1", &test_iocres_1},
    {"test_iocres_2", &test_iocres_2},
    {"test_ioset_1", &test_ioset_1},
    {"test_ioset_2", &test_ioset_2},
    {"test_ioset_3", &test_ioset_3},
    {"test_xch", &test_xch},
    {"test_del", &test_del},

};

// RUN TESTS.
int main() {
    int test_num = sizeof(tests) / sizeof(test_case);

    for (size_t i = 0; i < test_num; i++) {
        test_case *test = &tests[i];

        printf("######\nRunning test: %s\n", test->name);
        test->status = (*test->func)();
        if (test->status == 0) {
            printf("Passed\n");
        } else {
            printf("Failed\n");
        }
        printf("\n");
        assert(system(driver_down) == 0);  // Shut down driver no matter what.
    }

    int failed = 0;
    for (size_t i = 0; i < test_num; i++) {
        test_case *test = &tests[i];
        if (test->status != 0) {
            printf("%s failed\n", test->name);
            failed++;
        }
    }
    printf("######\n");
    printf("%d test(s) passed\n", test_num - failed);
    printf("%d test(s) failed\n", failed);
    if (failed == 0) {
        printf("All tests passed!\n");
    }

    return failed;
}