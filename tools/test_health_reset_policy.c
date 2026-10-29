// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "health_reset_policy.h"
int main(void) {
 assert(health_required_fresh(59000000,0,false,0,false,0));
 assert(!health_required_fresh(60000000,0,false,0,false,0));
 assert(health_required_fresh(120000000,0,true,119000000,true,119000000));
 assert(!health_required_fresh(120000000,0,true,60000000,true,119000000));
 assert(!health_required_fresh(120000000,0,true,119000000,true,60000000));
 // No cloud/USB suspend inputs: offline/overnight are healthy if workers run.
 assert(health_required_fresh(INT64_C(86400000000),0,true,INT64_C(86399000000),true,INT64_C(86399000000)));
 puts("Watchdog gate: startup, missing/stalled workers, healthy and overnight/offline scheduling passed.");
}
