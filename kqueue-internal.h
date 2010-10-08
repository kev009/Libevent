
#ifndef KQUEUE_INTERNAL_H
#define KQUEUE_INTERNAL_H

int _event_kq_notify_base(struct event_base *base);
int _event_kq_add_notify_event(struct event_base *base);

#endif
