#ifndef GUEST_API_H
#define GUEST_API_H

#include "types.h"

struct guest_task_result
{
    u64 id;
    char desc[32];
    u64 data0;
    u64 data1;
};

#endif /* GUEST_API_H */
