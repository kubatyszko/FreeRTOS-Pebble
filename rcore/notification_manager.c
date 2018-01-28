#include "rebbleos.h"
#include "notification_window.h"
#include "protocol_notification.h"
#include "notification_manager.h"

node_t *_messages_head = NULL;

static TaskHandle_t _noty_task;
static StackType_t _noty_task_stack[450];
static StaticTask_t _noty_task_buf;

static xQueueHandle _notif_queue;

static void _notification_thread(void *pvParameters);


void notification_init(void)
{
    /* start the thread */
    _noty_task = xTaskCreateStatic(_notification_thread, "Noty", 450, 
                                  NULL, tskIDLE_PRIORITY + 8UL, 
                                  _noty_task_stack, &_noty_task_buf);
    
    _notif_queue = xQueueCreate(1, sizeof(uint8_t));
}

void notification_add(full_msg_t *msg)
{
    uint8_t cmd = NOTIF_MSG_ARRIVED;
    node_add(&_messages_head, msg);
    
    /* notify the noty thread we have a message */
    xQueueSendToBack(_notif_queue, &cmd, 0);
}

void notification_msg_resend(void)
{
    uint8_t cmd = NOTIF_MSG_RESEND;
    xQueueSendToBack(_notif_queue, &cmd, 0);
}

void _draw_notif_window(void)
{   
    if (_messages_head == NULL)
    {
        SYS_LOG("NOTY", APP_LOG_LEVEL_ERROR, "No Messages?");
        return;
    }
    /* XXX TODO FIXME. we should walk this */
    full_msg_t *noty = _messages_head->data;
    
        
    SYS_LOG("NOTY", APP_LOG_LEVEL_INFO, "Got %d Messages:", noty->header->attr_count);
    
    node_t *cur = noty->attributes;
    while(cur != NULL)
    {
        uint8_t *p = ((cmd_phone_attribute_t *)cur->data)->data;
        SYS_LOG("NOTYM", APP_LOG_LEVEL_INFO, "Got: %d %c %s", ((cmd_phone_attribute_hdr_t *)cur->data)->attr_idx, *p, p);
        
        char *app = "RebbleOS";
        char *title = "Notification";
        // app = _app_id_to_name(id);
                
        Notification *notification = notification_create(app, 
                                            title, 
                                            (char *)((cmd_phone_attribute_t *)cur->data)->data,
                                            gbitmap_create_with_resource(77), 
                                            GColorRed);
        
        window_stack_push_notification(notification);
 
        cur = cur->next;
    }
    
    SYS_LOG("NOTYM", APP_LOG_LEVEL_INFO, "Done");
}

static void _notification_thread(void *pvParameters)
{
    uint8_t data;
    bool notification_pending = false;
    
    while(1)
    {
        // commands to be executed are send to this queue and processed
        // one at a time
        if (xQueueReceive(_notif_queue, &data, portMAX_DELAY))
        {
            switch(data)
            {
                case NOTIF_MSG_ARRIVED:
                    notification_pending = true;
                    // check if app is running
//                     _draw_notif_window();
//                     appmanager_post_notification();

                    // new
                    // hijack window and inject noty
                    break;
                case NOTIF_MSG_DELETE:
                    break;
                case NOTIF_MSG_RESEND:
                    SYS_LOG("NOTYM", APP_LOG_LEVEL_INFO, "RESEND");
                    if (notification_pending)
                    {
                        _draw_notif_window();
                        notification_pending = false;
                    }
                    
                    break;
            }
        }
    }

// delete
// full message free

// message action

}


