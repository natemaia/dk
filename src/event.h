/* dk - /dəˈkā/ window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

#include "common.h"
#include "cmd.h"
#include "parse.h"

void buttonpress(xcb_generic_event_t *ev);
void buttonrelease(xcb_generic_event_t *ev);
void clientmessage(xcb_generic_event_t *ev);
void confignotify(xcb_generic_event_t *ev);
void configrequest(xcb_generic_event_t *ev);
void destroynotify(xcb_generic_event_t *ev);
void dispatch(xcb_generic_event_t *ev);
void enternotify(xcb_generic_event_t *ev);
void error(xcb_generic_event_t *ev);
void focusin(xcb_generic_event_t *ev);
void ignore(uint8_t type);
void maprequest(xcb_generic_event_t *ev);
void motionnotify(xcb_generic_event_t *ev);
void mouse(Client *c, int move, int mx, int my);
void propertynotify(xcb_generic_event_t *ev);
void randr(xcb_generic_event_t *ev);
void unmapnotify(xcb_generic_event_t *ev);

static void (*handlers[XCB_NO_OPERATION + 1])(xcb_generic_event_t *) = {
    [XCB_BUTTON_PRESS]      = &buttonpress,
    [XCB_CLIENT_MESSAGE]    = &clientmessage,
    [XCB_CONFIGURE_NOTIFY]  = &confignotify,
    [XCB_CONFIGURE_REQUEST] = &configrequest,
    [XCB_DESTROY_NOTIFY]    = &destroynotify,
    [XCB_ENTER_NOTIFY]      = &enternotify,
    [XCB_FOCUS_IN]          = &focusin,
    [XCB_MAP_REQUEST]       = &maprequest,
    [XCB_MOTION_NOTIFY]     = &motionnotify,
    [XCB_PROPERTY_NOTIFY]   = &propertynotify,
    [XCB_UNMAP_NOTIFY]      = &unmapnotify,
    [XCB_NO_OPERATION]      = NULL
};
