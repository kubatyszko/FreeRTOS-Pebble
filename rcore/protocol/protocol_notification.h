#pragma once

//#include "node.h"

typedef struct node {
    void *data;
    struct node *next;
} node_t;


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
} __attribute__((__packed__)) cmd_phone_attribute_hdr_t;

typedef struct {
    cmd_phone_attribute_hdr_t hdr;
    uint8_t *data;
} __attribute__((__packed__)) cmd_phone_attribute_t;


typedef struct {
    uint8_t id;
    uint8_t cmd_id;
    uint8_t attr_count;
    uint8_t attr_id;
    uint16_t str_len;
} __attribute__((__packed__)) cmd_phone_action_hdr_t;

typedef struct {
    cmd_phone_action_hdr_t hdr;
    uint8_t *data;
} __attribute__((__packed__)) cmd_phone_action_t;






typedef struct full_msg {
    cmd_phone_notify_t *header;
    node_t *attributes;
    node_t *actions;
} full_msg_t;
