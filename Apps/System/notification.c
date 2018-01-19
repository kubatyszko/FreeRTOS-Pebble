/* notification.h
 * routines for [...]
 * RebbleOS
 *
 * Author: Carson Katri <me@carsonkatri.com>
 */

#include "rebbleos.h"
#include "notification.h"
#include "notification_window.h"
#include "librebble.h"
#include "bitmap_layer.h"
#include "action_bar_layer.h"
#include "pebble_protocol.h"

const char *notif_name = "Notification";

static NotificationWindow *notif_window;
full_msg_t *noty;

void notif_init(void)
{
//     printf("init\n");
    char *app = "RebbleOS";
    char *title = "Test Alert";

    noty = notification_get();
    
    if (noty == NULL)
    {
        SYS_LOG("NOTY", APP_LOG_LEVEL_ERROR, "No Messages?");
        return;
    }
    SYS_LOG("NOTY", APP_LOG_LEVEL_INFO, "Got %d Messages:", noty->header->attr_count);
//     snprintf(test, 250, "Got %d Messages:\n", noty->header->attr_count);
    
    node_t *cur = noty->attributes;
    while(cur != NULL)
    {
        uint8_t *p = ((cmd_phone_attribute_t *)cur->data)->data;
        SYS_LOG("NOTYM", APP_LOG_LEVEL_INFO, "Got: %d %c %c", ((cmd_phone_attribute_hdr_t *)cur->data)->attr_idx, *p, *(p+1));
        
        Notification *notification = notification_create(app, 
                                            title, 
                                            ((cmd_phone_attribute_t *)cur->data)->data, gbitmap_create_with_resource(77), 
                                            GColorRed);
        
        window_stack_push_notification(notification);
        cur = cur->next;
    }
    
    
//     Notification *notification_two = notification_create("Discord", "Join Us", "Join us on the Pebble Discord in the #firmware channel", gbitmap_create_with_resource(20), GColorFromRGB(85, 0, 170));
//     window_stack_push_notification(notification_two);
}

void notif_deinit(void)
{
    window_destroy(notif_window->window);
}

void notif_main(void)
{
    notif_init();
    app_event_loop();
    notif_deinit();
}
