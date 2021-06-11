#include "hello_queue.h"

#include <minix/chardriver.h>
#include <minix/drivers.h>
#include <minix/ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioc_hello_queue.h>

#define HELLO_MESSAGE "Hello, World!\n"

/*
 * Function prototypes for the hello driver.
 */
static int hq_open(devminor_t minor, int access, endpoint_t user_endpt);
static int hq_close(devminor_t minor);
static ssize_t hq_read(devminor_t minor, u64_t position, endpoint_t endpt,
                       cp_grant_id_t grant, size_t size, int flags,
                       cdev_id_t id);
static ssize_t hq_write(devminor_t UNUSED(minor), u64_t position,
                        endpoint_t endpt, cp_grant_id_t grant, size_t size,
                        int UNUSED(flags), cdev_id_t UNUSED(id));

static int hq_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
                    cp_grant_id_t grant, int flags, endpoint_t user_endpt,
                    cdev_id_t id);

// Fills buffer with repeating "xyz" string.
static void fill_buffer();

// Remove size bytes from the front of the buffer.
static void buffer_down(u64_t size);

// Make space for size bytes in the buffer.
static int buffer_up(u64_t size);

static int do_res();

static int do_set(endpoint_t endpt, cp_grant_id_t gid);

static int do_cxch(endpoint_t endpt, cp_grant_id_t gid);

static int do_del();

/* SEF functions and variables. */
static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
static int sef_cb_lu_state_save(int);
static int lu_state_restore(void);

/* Entry points to the hello driver. */
static struct chardriver hello_tab = {
    .cdr_open = hq_open,
    .cdr_close = hq_close,
    .cdr_read = hq_read,
    .cdr_write = hq_write,
    .cdr_ioctl = hq_ioctl,
};

// TODO remove this.
static int open_counter;

// Query buffer.
char *hq_buffer;

size_t hq_capacity;
size_t hq_size;
size_t hq_head;

static int hq_open(devminor_t UNUSED(minor), int UNUSED(access),
                   endpoint_t UNUSED(user_endpt)) {
    return OK;
}

static int hq_close(devminor_t UNUSED(minor)) { return OK; }

static ssize_t hq_read(devminor_t UNUSED(minor), u64_t UNUSED(position),
                       endpoint_t endpt, cp_grant_id_t grant, size_t size,
                       int UNUSED(flags), cdev_id_t UNUSED(id)) {
    u64_t dev_size;
    char *ptr = hq_buffer + hq_head;
    int ret;

    if (hq_size == 0) return 0; /* EOF */

    if (size > hq_size) size = hq_size;

    /* Copy the requested part to the caller. */
    if ((ret = sys_safecopyto(endpt, grant, 0, (vir_bytes)ptr, size)) != OK)
        return ret;

    buffer_down(size);

    /* Return the number of bytes read. */
    return size;
}

static ssize_t hq_write(devminor_t UNUSED(minor), u64_t position,
                        endpoint_t endpt, cp_grant_id_t grant, size_t size,
                        int UNUSED(flags), cdev_id_t UNUSED(id)) {
    int ret;
    char *ptr = hq_buffer + hq_head + hq_size;

    if ((ret = buffer_up(size)) != OK) {
        return ret;
    }

    if ((ret = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)ptr, size)) != OK) {
        return ret;
    }
    hq_size += size;

    return size;
}

static int hq_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
                    cp_grant_id_t grant, int flags, endpoint_t user_endpt,
                    cdev_id_t id) {
    switch (request) {
        case HQIOCRES:
            return do_res();
        case HQIOCSET:
            return do_set(endpt, grant);
        case HQIOCXCH:
            return do_cxch(endpt, grant);
        case HQIOCDEL:
            return do_del();
    }

    return ENOTTY;
}

static int do_res() {
    char *ptr = realloc(hq_buffer, DEVICE_SIZE);
    if (ptr == NULL) return ENOMEM;

    hq_capacity = DEVICE_SIZE;
    hq_size = DEVICE_SIZE;
    hq_head = 0;
    fill_buffer();
    return OK;
}

static int do_set(endpoint_t endpt, cp_grant_id_t gid) {
    char msg[MSG_SIZE];
    int ret;

    if ((ret = sys_safecopyfrom(endpt, gid, 0, msg, MSG_SIZE)) != OK) {
        return ret;
    }

    if (hq_size < MSG_SIZE) {
        if ((ret = buffer_up(MSG_SIZE - hq_size)) != OK) {
            return ret;
        }
        hq_size = MSG_SIZE;
    }
    memcpy(hq_buffer + hq_head + hq_size - MSG_SIZE, msg, MSG_SIZE);

    return OK;
}

static int do_cxch(endpoint_t endpt, cp_grant_id_t gid) {
    char msg[2];
    int ret;

    if ((ret = sys_safecopyfrom(endpt, gid, 0, msg, 2)) != OK) {
        return ret;
    }

    size_t i = hq_head, end = hq_head + hq_size;
    for (; i < end; i++) {
        if (hq_buffer[i] == msg[0]) {
            hq_buffer[i] = msg[1];
        }
    }

    return OK;
}

static int do_del() {
    int shift = 0;
    size_t i = hq_head, j = 1, end = hq_head + hq_size;

    for (; i < end; i++) {
        if (j == 3) {
            j = 1;
            shift++;
            continue;
        }
        hq_buffer[i - shift] = hq_buffer[i];
        j++;
    }
    hq_size -= shift;
    return OK;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
    /* Save the state. */
    ds_publish_u32("open_counter", open_counter, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
    /* Restore the state. */
    u32_t value;

    ds_retrieve_u32("open_counter", &value);
    ds_delete_u32("open_counter");
    open_counter = (int)value;

    return OK;
}

static void sef_local_startup() {
    /*
     * Register init callbacks. Use the same function for all event types
     */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /*
     * Register live update callbacks.
     */
    /* - Agree to update immediately when LU is requested in a valid state.
     */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

static int sef_cb_init(int type, sef_init_info_t *UNUSED(info)) {
    /* Initialize the hello driver. */
    int do_announce_driver = TRUE;

    open_counter = 0;
    switch (type) {
        case SEF_INIT_FRESH:
            hq_buffer = malloc(DEVICE_SIZE);
            if (hq_buffer == NULL) return ENOMEM;
            hq_capacity = DEVICE_SIZE;
            hq_size = DEVICE_SIZE;
            hq_head = 0;

            fill_buffer();
            printf("%s", HELLO_MESSAGE);
            break;

        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;

            printf("%sHey, I'm a new version!\n", HELLO_MESSAGE);
            break;

        case SEF_INIT_RESTART:
            printf("%sHey, I've just been restarted!\n", HELLO_MESSAGE);
            break;
    }

    /* Announce we are up when necessary. */
    if (do_announce_driver) {
        chardriver_announce();
    }

    /* Initialization completed successfully. */
    return OK;
}

static void fill_buffer() {
    char *pattern = "xyz";

    u64_t i = 0, j = 0;
    for (; i < hq_capacity; i++, j++) {
        if (j == 3) {
            j = 0;
        }
        hq_buffer[i] = pattern[j];
    }
}

static void buffer_down(u64_t size) {
    hq_head += size;
    hq_size -= size;

    if (hq_size < hq_capacity / 4) {
        if (hq_size > 0) {
            memmove(hq_buffer, hq_buffer + hq_head, hq_size);
        }
        hq_head = 0;

        size_t new_capacity = hq_capacity / 2;

        if (new_capacity < 1) return;

        char *ptr = realloc(hq_buffer, new_capacity);

        if (ptr == NULL) return;
        hq_buffer = ptr;
        hq_capacity = new_capacity;
    }
}

static int buffer_up(u64_t size) {
    if (hq_head + hq_size + size > hq_capacity && hq_head != 0) {
        memmove(hq_buffer, hq_buffer + hq_head, hq_size);
        hq_head = 0;
    }

    if (hq_size + size > hq_capacity) {
        size_t new_capacity = hq_capacity;
        while (new_capacity <= hq_size + size) {
            new_capacity *= 2;
        }
        char *ptr = realloc(hq_buffer, new_capacity);
        if (ptr == NULL) return ENOMEM;
        hq_buffer = ptr;
        hq_capacity = new_capacity;
    }

    return OK;
}

int main(void) {
    /*
     * Perform initialization.
     */
    sef_local_startup();

    /*
     * Run the main loop.
     */
    chardriver_task(&hello_tab);
    return OK;
}
