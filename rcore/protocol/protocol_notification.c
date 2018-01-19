
#include <stdlib.h>
#include "rebbleos.h"
#include "appmanager.h"
#include "systemapp.h"
#include "test.h"
#include "protocol_notification.h"


full_msg_t *message = NULL;

full_msg_t *notification_get(void)
{
    return message;
}

// notification processing

void process_notification_packet(uint8_t *data)
{
    // create a real message as we are peeking at the buffer.
    // lets be quick about this
    cmd_phone_notify_t *msg = (cmd_phone_notify_t *)data;

    SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X attrc %d actc %d", msg->attr_count, msg->action_count);
    
    message = malloc(sizeof(full_msg_t));
    message->header = malloc(sizeof(cmd_phone_notify_t));
    memcpy(message->header, msg, sizeof(cmd_phone_notify_t));
    message->attributes = NULL;
    message->actions = NULL;
    
    // get the attributes
    uint8_t *p = data + sizeof(cmd_phone_notify_t);
    for (uint8_t i = 0; i < msg->attr_count; i++)
    {
        
        cmd_phone_attribute_hdr_t *att = (cmd_phone_attribute_hdr_t *)p;
        uint8_t *data = p + sizeof(cmd_phone_attribute_hdr_t);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X ATTR ID:%d L:%d %s", att->attr_idx, att->str_len, data);
        cmd_phone_attribute_t *new_attr = malloc(sizeof(cmd_phone_attribute_t));
        // copy the head to the new attribute
        memcpy(new_attr, att, sizeof(cmd_phone_attribute_hdr_t));
        // copy the data in now
        // we'll null terminate strings too as they are pascal strings and seemingly not terminated
        _copy_and_null_term_string(&new_attr->data, data, att->str_len);
       
        node_add(&message->attributes, new_attr);
        
        p += sizeof(cmd_phone_attribute_hdr_t) + att->str_len;
    }

    // get the actions
    for (uint8_t i = 0; i < msg->action_count; i++)
    {
        cmd_phone_action_hdr_t *act = (cmd_phone_action_hdr_t *)p;
        uint8_t *data = p + sizeof(cmd_phone_action_hdr_t);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X ACT ID:%d L:%d AID:%d ALEN:%d %s", act->id, act->attr_count, act->attr_id, act->str_len, data);
        cmd_phone_action_t *new_act = malloc(sizeof(cmd_phone_action_t));
        // copy the head to the new action
        memcpy(new_act, act, sizeof(cmd_phone_action_hdr_t));
        // copy the data in now
        _copy_and_null_term_string(&new_act->data, data, act->str_len);
        node_add(&message->actions, new_act);
        
        p += sizeof(cmd_phone_action_hdr_t) + act->str_len;
    }
     
    appmanager_post_notification();
}

// we have pesky pascal strings.
void _copy_and_null_term_string(uint8_t **dest, uint8_t *src, uint16_t len)
{
    if (src[len - 1] != '\0')
    {
        *dest = malloc(len + 1);
        memcpy(*dest, src, len);
        *dest[len] = '\0';
//         SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "DEST %s", dest);
    }
    else
    {
        *dest = malloc(len);
        memcpy(*dest, src, len);
    }
}

/*
strncpy((char*)notification, (char*)pkt->data, 150); 
notification_len = pkt->len; 
appmanager_post_notification();
*/

void node_add(node_t **head, void *data)
{
    node_t *cur = *head;   
    if (cur == NULL)
    {
        cur = malloc(sizeof(node_t));
        cur->data = data;
        cur->next = NULL;
        *head = cur;
        return;
    }
    while (cur->next != NULL)
        cur = cur->next;

    cur->next = malloc(sizeof(node_t));
    cur->next->data = data;
    cur->next->next = NULL;
}
