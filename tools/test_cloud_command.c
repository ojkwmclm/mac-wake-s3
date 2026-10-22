#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cloud_command.h"
static void check(const char *json, const char *owner, bool retained, bool dup, cloud_command_t expected)
{ assert(cloud_parse_command(json, strlen(json), owner, retained, dup) == expected); }
int main(void)
{
    const char *wake="{\"fromDevice\":\"owner\",\"data\":{\"btn-wakeup\":\"tap\"}}";
    check(wake,"owner",false,false,CLOUD_WAKE);
    check(wake,"other",false,false,CLOUD_IGNORE);
    check(wake,"owner",true,false,CLOUD_IGNORE);
    check(wake,"owner",false,true,CLOUD_IGNORE);
    check(wake,"",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"get\":\"state\"}}","owner",false,false,CLOUD_GET_STATE);
    check("{\"fromDevice\":\"owner\",\"data\":\"{\\\"btn-wakeup\\\":\\\"tap\\\"}\"}","owner",false,false,CLOUD_WAKE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-wakeup\":\"off\"}}","owner",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"text\":\"H\"}}","owner",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-abc\":\"tap\"}}","owner",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-wakeup\":\"tap\"}}garbage","owner",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":\"{\\\"btn-wakeup\\\":\\\"tap\\\"}garbage\"}","owner",false,false,CLOUD_IGNORE);
    check("{\"data\":{\"btn-wakeup\":\"tap\"}}","owner",false,false,CLOUD_IGNORE);
    check("not json","owner",false,false,CLOUD_IGNORE);
    char large[1026];memset(large,' ',sizeof(large));
    assert(cloud_parse_command(large,sizeof(large),"owner",false,false)==CLOUD_IGNORE);
    const char *sleep="{\"fromDevice\":\"owner\",\"data\":{\"btn-sleep\":\"tap\"}}";
    check(sleep,"owner",false,false,CLOUD_SLEEP);
    check(sleep,"other",false,false,CLOUD_IGNORE);
    check(sleep,"owner",true,false,CLOUD_IGNORE);
    check(sleep,"owner",false,true,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-sleep\":\"press\"}}","owner",false,false,CLOUD_SLEEP);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-sleep\":\"pressup\"}}","owner",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-sleep\":\"on\"}}","owner",false,false,CLOUD_IGNORE);
    check("{\"fromDevice\":\"owner\",\"data\":{\"btn-wakeup\":\"tap\",\"btn-sleep\":\"tap\"}}","owner",false,false,CLOUD_IGNORE);
    puts("23 cloud command boundary tests passed.");
}
