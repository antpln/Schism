#ifndef GUEST_TASKS_H
#define GUEST_TASKS_H

#include "guest_stubs.h"
#include "guest_api.h"

void guest_task_counter(u64 guest_id, struct guest_task_result *out);
void guest_task_memwalk(u64 guest_id, struct guest_task_result *out);
void guest_task_report(u64 guest_id, const struct guest_task_result *out);

#endif /* GUEST_TASKS_H */
