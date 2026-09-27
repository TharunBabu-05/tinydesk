/*
 * event.c - fixed-size ring buffer of input events.
 */
#include "tinydesk/td_input.h"

static td_event_t s_queue[TD_EVENT_QUEUE_SIZE];
static int s_head;    /* next event to pop */
static int s_count;

static bool is_motion(const td_event_t *ev)
{
    return ev->type == TD_EV_MOUSE &&
           (ev->action == TD_MOUSE_DRAG || ev->action == TD_MOUSE_MOVE);
}

bool td_event_push(const td_event_t *ev)
{
    /* Merge consecutive motion events: only the latest position matters. */
    if (s_count > 0 && is_motion(ev)) {
        td_event_t *last = &s_queue[(s_head + s_count - 1) % TD_EVENT_QUEUE_SIZE];
        if (is_motion(last) && last->button == ev->button) {
            *last = *ev;
            return true;
        }
    }
    if (s_count == TD_EVENT_QUEUE_SIZE) return false;
    s_queue[(s_head + s_count) % TD_EVENT_QUEUE_SIZE] = *ev;
    s_count++;
    return true;
}

bool td_event_pop(td_event_t *ev)
{
    if (s_count == 0) return false;
    *ev = s_queue[s_head];
    s_head = (s_head + 1) % TD_EVENT_QUEUE_SIZE;
    s_count--;
    return true;
}

int td_event_count(void) { return s_count; }

void td_event_clear(void)
{
    s_head = 0;
    s_count = 0;
}
