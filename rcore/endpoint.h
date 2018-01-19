#pragma once

// endpoint functions
#define ENDPOINT_SET_TIME               0x0b
#define ENDPOINT_FIRMWARE_VERSION       0x10
#define ENDPOINT_PHONE_MSG              0xbc2



// function parameters
#define FIRMWARE_VERSION_GETVERSION  0


/* Time Command functions */
typedef enum cmd_time_func {
    TIME_GETTIME = 0,
    TIME_SETTIME = 2,
    TIME_SETTIME_UTC = 3,
} cmd_time_func_t;

typedef struct {
    uint8_t cmd;
    uint8_t ts;
    uint8_t tso;
    uint8_t tz_len;
    uint8_t tz;
} __attribute__((__packed__)) cmd_set_time_t;


typedef struct {
    uint8_t unk0;
    uint8_t add_nofif;
    uint32_t flags;
    uint32_t id;
    uint32_t ancs_id;
    uint32_t ts;
    uint8_t layout;
    uint8_t attr_count;
    uint8_t action_count;
    // These follow in the buffer
    //cmd_phone_attribute *attributes;
    //cmd_phone_attribute *actions;
} __attribute__((__packed__)) cmd_phone_notify_t;



typedef struct {
    uint8_t attr_idx;
    uint16_t str_len;
} __attribute__((__packed__)) cmd_phone_attribute_t;

typedef struct {
    uint8_t id;
    uint8_t cmd_id;
    uint8_t attr_count;
    uint8_t attr_id;
    uint16_t str_len;
} __attribute__((__packed__)) cmd_phone_action_t;

