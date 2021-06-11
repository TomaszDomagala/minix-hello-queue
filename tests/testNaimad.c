#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <assert.h>
#include "sys/ioc_hello_queue.h"

#include "hello_queue.h"

void startDriver() {
    assert(system("service up /service/hello_queue -dev /dev/hello_queue") == 0);
}

void updateDriver() {
    assert(system("service update /service/hello_queue") == 0);
}

void refreshDriver() {
    assert(system("service refresh hello_queue") == 0);
}

void stopDriver() {
    assert(system("service down hello_queue") == 0);
}

#define BUFFER_SIZE 1000004

/* Buffer for storing bytes that are read from the queue. */
char buffer[BUFFER_SIZE];

/* Tries to read amount bytes and assert that expectedAmount were
 * actually read. */
void readBytes(int fd, size_t amount, size_t expectedAmount) {
    assert(read(fd, buffer, amount) == expectedAmount);
}

/* Reads amount bytes from fd into buffer. */
void readAmountBytes(int fd, size_t amount) {
    readBytes(fd, amount, amount);
}

/* Reads whole queue from fd into buffer. */
void readWholeQueue(int fd, size_t expectedSize) {
    readBytes(fd, BUFFER_SIZE, expectedSize);
}

/* Asserts that queue is in initial state (xyz...). */
void assertQueueInitial(int fd) {
    static char c[] = {'x', 'y', 'z'};

    readWholeQueue(fd, DEVICE_SIZE);

    for (size_t i = 0; i < DEVICE_SIZE; i++) {
        assert(buffer[i] == c[i % 3]);
    }
}

/* Asserts that the first bytes of the buffer
 * are equal to the C-string pointed by c. */
void assertInBuffer(char *c) {
    size_t len = strlen(c);

    for (size_t i = 0; i < len; i++) {
        assert(buffer[i] == c[i]);
    }
}

/* Asserts that the queue is equal to the C-string pointed by c. */
void assertQueueEqual(int fd, char *c) {
    readWholeQueue(fd, strlen(c));

    assertInBuffer(c);
}

/* Writes C-string pointed by c to the queue. */
void writeToQueue(int fd, char *c) {
    size_t len = strlen(c);
    assert(write(fd, c, len) == len);
}

/* Tests initial state of queue. */
void test1() {
    startDriver();

    int fd;

    if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
        fprintf(stderr, "Error open\n");
        exit(1);
    }

    /* Assert that queue is in initial state. */
    assertQueueInitial(fd);

    /* Assert that queue is empty... 30 times, just to be sure. */
    for (int i = 0; i < 30; i++) {
        readWholeQueue(fd, 0);
    }

    close(fd);
    stopDriver();
}

/* Tests read/write. */
void test2() {
    startDriver();

    int fd;

    if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
        fprintf(stderr, "Error open\n");
        exit(1);
    }

    /* First, clear the queue. */
    readWholeQueue(fd, DEVICE_SIZE);

    /* For all prefixes of c[], write it into queue and read it. */
    {
        char c[] = "abcdefghijklzxcvbnm";
        size_t len = strlen(c);
        for (size_t i = 0; i + 1 < len; i++) {
            char s = c[i + 1];
            c[i + 1] = 0;

            writeToQueue(fd, c);
            assertQueueEqual(fd, c);

            c[i + 1] = s;
        }
    }

    printf("Subtest 2a passed\n");

    /* Write 6 C-strings and then read each one, one by one. */
    {
        char *c[6] = {
                "sdposdpfosa",
                "sdpoe",
                "ropqope",
                "zkowqgfg",
                "kowexeqwez",
                "zxetq"
        };

        for (size_t i = 0; i < 6; i++) {
            writeToQueue(fd, c[i]);
        }

        for (size_t i = 0; i < 6; i++) {
            readAmountBytes(fd, strlen(c[i]));
            assertInBuffer(c[i]);
        }
    }

    printf("Subtest 2b passed\n");

    /* Writes two big C-strings each and reads them back. */
    {
        char c[2][1000];

        c[0][999] = 0;
        c[1][999] = 0;

        for (size_t i = 0; i < 999; i++) {
            c[0][i] = (char) (65 + (97 * i + 47) % 26);
        }

        for (size_t i = 0; i < 999; i++) {
            c[1][i] = (char) (65 + (103 * i + 37) % 26);
        }

        for (size_t i = 0; i < 2; i++) {
            writeToQueue(fd, c[i]);
        }

        for (size_t i = 0; i < 2; i++) {
            readAmountBytes(fd, strlen(c[i]));
            assertInBuffer(c[i]);
        }
    }

    printf("Subtest 2c passed\n");

    /* Reading/writing 0 bytes. */
    {
        writeToQueue(fd, "absfshodfioew");

        readAmountBytes(fd, 0);
        writeToQueue(fd, "");
    }

    printf("Subtest 2d passed\n");

    close(fd);
    stopDriver();
}

void forTest3B(int fd) {
    static char c[] = {'r', 'y', 'z'};

    readWholeQueue(fd, DEVICE_SIZE);

    for (size_t i = 0; i < DEVICE_SIZE; i++) {
        assert(buffer[i] == c[i % 3]);
    }
}

void forTest3C(int fd) {
    static char c[] = {'x', 'y'};

    readWholeQueue(fd, 2 * DEVICE_SIZE / 3 + 1);

    for (size_t i = 0; i < 2 * DEVICE_SIZE / 3 + 1; i++) {
        assert(buffer[i] == c[i % 2]);
    }
}

/* Tests ioctl. */
void test3() {
    startDriver();

    int fd;

    if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
        fprintf(stderr, "Error open\n");
        exit(1);
    }

    /* First, clear the queue. */
    readWholeQueue(fd, DEVICE_SIZE);

    /* Do HQIOCRES (restoring queue to the initial state). */
    if (ioctl(fd, HQIOCRES) < 0) {
        fprintf(stderr, "Error res\n");
        exit(1);
    }

    assertQueueInitial(fd);

    printf("Subtest 3a passed\n");

    /* Replace all 'x' occurrences with 'r'. */
    {
        if (ioctl(fd, HQIOCRES) < 0) {
            fprintf(stderr, "Error res\n");
            exit(1);
        }

        char c[2] = {'x', 'r'};

        if (ioctl(fd, HQIOCXCH, c) < 0) {
            fprintf(stderr, "Error xch\n");
            exit(1);
        }

        forTest3B(fd);
    }

    printf("Subtest 3b passed\n");

    /* Remove every 3rd element from queue. */
    {
        if (ioctl(fd, HQIOCRES) < 0) {
            fprintf(stderr, "Error res\n");
            exit(1);
        }

        char msg[7] = "message";

        if (ioctl(fd, HQIOCDEL, msg) < 0) {
            fprintf(stderr, "Error del\n");
            exit(1);
        }

        forTest3C(fd);
    }

    printf("Subtest 3c passed\n");

    /* Writes message (HQIOCSET) longer than queue. */
    {
        char msg[7] = "message";

        writeToQueue(fd, "abc");

        if (ioctl(fd, HQIOCSET, msg) < 0) {
            fprintf(stderr, "Error set\n");
            exit(1);
        }

        assertQueueEqual(fd, "message");
    }

    printf("Subtest 3d passed\n");

    {
        if (ioctl(fd, HQIOCRES) < 0) {
            fprintf(stderr, "Error res\n");
            exit(1);
        }

        char msg[7] = "message";

        if (ioctl(fd, HQIOCSET, msg) < 0) {
            fprintf(stderr, "Error set\n");
            exit(1);
        }

        char c[DEVICE_SIZE + 1];
        c[DEVICE_SIZE] = 0;

        for (size_t i = 0; i < DEVICE_SIZE - 7; i++) {
            c[i] = (char)('x' + i % 3);
        }

        for (int i = DEVICE_SIZE - 7; i < DEVICE_SIZE; i++) {
            c[i] = msg[i + 7 - DEVICE_SIZE];
        }

        assertQueueEqual(fd, c);
    }

    printf("Subtest 3e passed\n");

    close(fd);
    stopDriver();
}

/* Tests update state save. */
void test4() {
    startDriver();

    int fd;

    if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
        fprintf(stderr, "Error open\n");
        exit(1);
    }

    /* First, clear the queue. */
    readWholeQueue(fd, DEVICE_SIZE);

    char c[] = "dsfojweirhoiwejfiowqewrdshoixfds";

    writeToQueue(fd, c);

    readAmountBytes(fd, 3);

    close(fd);

    updateDriver();

    /* Waiting 3 second just to make sure that everything is restarted. */
    sleep(3);

    if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
        fprintf(stderr, "Error open\n");
        exit(1);
    }

    char d[] = "wtweuioremqa";

    writeToQueue(fd, d);

    readAmountBytes(fd, strlen(c + 3));
    assertInBuffer(c + 3);

    readAmountBytes(fd, strlen(d));
    assertInBuffer(d);

    readWholeQueue(fd, 0);

    close(fd);
    stopDriver();
}

// /* Tests refresh state save. */
// void test5() {
//     startDriver();

//     int fd;

//     if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
//         fprintf(stderr, "Error open\n");
//         exit(1);
//     }

//     /* First, clear the queue. */
//     readWholeQueue(fd, DEVICE_SIZE);

//     char c[] = "dsfojweirhoiwejfiowqewrdshoixfds";

//     writeToQueue(fd, c);

//     readAmountBytes(fd, 3);

//     close(fd);

//     refreshDriver();

//     /* Waiting 3 second just to make sure that everything is restarted. */
//     sleep(3);

//     if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
//         fprintf(stderr, "Error open\n");
//         exit(1);
//     }

//     char d[] = "wtweuioremqa";

//     writeToQueue(fd, d);

//     readAmountBytes(fd, strlen(c + 3));
//     assertInBuffer(c + 3);

//     readAmountBytes(fd, strlen(d));
//     assertInBuffer(d);

//     readWholeQueue(fd, 0);

//     close(fd);
//     stopDriver();
// }

/* Tests refresh state save. */
void test6() {
    startDriver();

    int fd;

    if ((fd = open("/dev/hello_queue", O_RDWR)) < 0) {
        fprintf(stderr, "Error open\n");
        exit(1);
    }

    /* First, clear the queue. */
    readWholeQueue(fd, DEVICE_SIZE);

    /* Write "abcd", jump to 1000, write "efgh", jump to 137
     * and try to read from there. Should read "abcdefgh",
     * since lseek should have no effect on the driver. */
    {
        writeToQueue(fd, "abcd");

        lseek(fd, 1000, SEEK_SET);

        writeToQueue(fd, "efgh");

        lseek(fd, 137, SEEK_SET);

        readWholeQueue(fd, 8);
        assertInBuffer("abcdefgh");
    }

    printf("Subtest 6a passed\n");

    /* Similar test. */
    {
        writeToQueue(fd, "abcd");

        lseek(fd, 1000, SEEK_SET);

        writeToQueue(fd, "efgh");

        lseek(fd, 4, SEEK_SET);

        readAmountBytes(fd, 4);
        assertInBuffer("abcd");

        readAmountBytes(fd, 4);
        assertInBuffer("efgh");
    }

    printf("Subtest 6b passed\n");

    close(fd);
    stopDriver();
}

#define TEST_COUNT 6

int main() {
    void (*fun[TEST_COUNT])() = {test1, test2, test3, test4, test6};

    for (int i = 0; i < TEST_COUNT; i++) {
        printf("Starting test %d.\n", i + 1);

        memset(buffer, 0, sizeof(buffer));
        fun[i]();

        printf("Test %d passed.\n\n", i + 1);
        fflush(stdout);
    }

    printf("All tests passed!\n");
}
