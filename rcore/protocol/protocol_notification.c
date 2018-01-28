/* protocol_notification.c
 * Protocol notification processer
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */
#include <stdlib.h>
#include "rebbleos.h"
#include "appmanager.h"
#include "systemapp.h"
#include "test.h"
#include "protocol_notification.h"


// full_msg_t *message = NULL;
/*
full_msg_t *notification_get(void)
{
    return message;
}*/

// notification processing

void process_notification_packet(uint8_t *data)
{
//     if (message != NULL)
//     {
//         //some serious freeing to do here
//         _full_msg_free(message);
//     }
    full_msg_t *msg;
    notification_packet_push(data, &msg);
    notification_add(msg);
}

void notification_packet_push(uint8_t *data, full_msg_t **message)
{
    full_msg_t *new_msg = *message;
    
    // create a real message as we are peeking at the buffer.
    // lets be quick about this
    cmd_phone_notify_t *msg = (cmd_phone_notify_t *)data;

    SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X attrc %d actc %d", msg->attr_count, msg->action_count);
    
    new_msg = calloc(1, sizeof(full_msg_t));
    
    new_msg->header = calloc(1, sizeof(cmd_phone_notify_t));
    memcpy(new_msg->header, msg, sizeof(cmd_phone_notify_t));
    new_msg->attributes = NULL;
    new_msg->actions = NULL;
    
    // get the attributes
    uint8_t *p = data + sizeof(cmd_phone_notify_t);
    for (uint8_t i = 0; i < msg->attr_count; i++)
    {
        
        cmd_phone_attribute_hdr_t *att = (cmd_phone_attribute_hdr_t *)p;
        uint8_t *data = p + sizeof(cmd_phone_attribute_hdr_t);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X ATTR ID:%d L:%d", att->attr_idx, att->str_len);
        cmd_phone_attribute_t *new_attr = calloc(1, sizeof(cmd_phone_attribute_t));
        // copy the head to the new attribute
        memcpy(new_attr, att, sizeof(cmd_phone_attribute_hdr_t));
        // copy the data in now
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X COPY");
        // we'll null terminate strings too as they are pascal strings and seemingly not terminated
        _copy_and_null_term_string(&(new_attr->data), data, att->str_len);
       
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X NODE");
        node_add(&new_msg->attributes, new_attr);
        p += sizeof(cmd_phone_attribute_hdr_t) + att->str_len;
    }

    // get the actions
    for (uint8_t i = 0; i < msg->action_count; i++)
    {
        cmd_phone_action_hdr_t *act = (cmd_phone_action_hdr_t *)p;
        uint8_t *data = p + sizeof(cmd_phone_action_hdr_t);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X ACT ID:%d L:%d AID:%d ALEN:%d", act->id, act->attr_count, act->attr_id, act->str_len);
        cmd_phone_action_t *new_act = calloc(1, sizeof(cmd_phone_action_t));
        // copy the head to the new action
        memcpy(new_act, act, sizeof(cmd_phone_action_hdr_t));
        // copy the data in now
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X COPY");
        _copy_and_null_term_string(&new_act->data, data, act->str_len);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X NODE");
        node_add(&new_msg->actions, new_act);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X NODE ADDED");
        p += sizeof(cmd_phone_action_hdr_t) + act->str_len;
    }
    SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "X Done");
    *message = new_msg;
}

void _full_msg_free(full_msg_t *message)
{  
    node_t *cur = message->attributes;
    while(cur != NULL)
    {
        node_t *next = cur->next;
        // free the string
        free(((cmd_phone_attribute_t *)cur->data)->data);
        // free the attribute
        free(cur->data);
        // free the node
        free(cur);
        cur = next;
    }
    message->attributes = NULL;
    
    cur = message->actions;
    while(cur != NULL)
    {
        node_t *next = cur->next;
        // free the string
        free(((cmd_phone_action_t *)cur->data)->data);
        // free the attribute
        free(cur->data);
        // free the node
        free(cur);
        cur = next;
    }
    message->actions = NULL;
    
    free(message->header);
    message->header = NULL;
    free(message);
    message = NULL;
}


// we have pesky pascal strings.
void _copy_and_null_term_string(uint8_t **dest, uint8_t *src, uint16_t len)
{
    if (src[len - 1] != '\0')
    {
        *dest = calloc(1, len + 1);
        assert(*dest && "Malloc Failed!");
            
        // this causes a lockup when copying an odd number of bytes
        //memcpy((uint8_t *)*dest, src, len);
        for(uint16_t i = 0; i < len; i++)
            *(*dest + i) = src[i];
        //strncpy((uint8_t*)*dest, (uint8_t*)src, len);
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "DEST");
        *(*dest + len) = '\0';
        SYS_LOG("PHPKT", APP_LOG_LEVEL_INFO, "DEST %s", *dest);
    }
    else
    {
        *dest = calloc(1, len);
        memcpy((uint8_t *)*dest, src, len);
    }
}

/*
strncpy((char*)notification, (char*)pkt->data, 150); 
notification_len = pkt->len; 
appmanager_post_notification();
*/
