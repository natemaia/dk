/* dk window manager
 *
 * see license file for copyright and license details
 * vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
 */

#pragma once

void buttonpress(xcb_generic_event_t *ev);
void buttonrelease(int move);
void clientmessage(xcb_generic_event_t *ev);
void confignotify(xcb_generic_event_t *ev);
void configrequest(xcb_generic_event_t *ev);
void destroynotify(xcb_generic_event_t *ev);
void dispatch(xcb_generic_event_t *ev);
void enternotify(xcb_generic_event_t *ev);
void focusin(xcb_generic_event_t *ev);
void ignore(uint8_t type);
void mappingnotify(xcb_generic_event_t *ev);
void maprequest(xcb_generic_event_t *ev);
void motionnotify(xcb_generic_event_t *ev);
void mousemotion(Client *c, xcb_button_t button, int mx, int my);
void propertynotify(xcb_generic_event_t *ev);
void unmapnotify(xcb_generic_event_t *ev);
