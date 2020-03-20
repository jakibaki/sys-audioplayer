#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include <switch.h>

#include "mp3.h"

#define ERPT_SAVE_ID 0x80000000000000D1
#define TITLE_ID 0x4200000000000000
#define HEAP_SIZE 0x000320000

// we aren't an applet
u32 __nx_applet_type = AppletType_None;

// setup a fake heap (we don't need the heap anyway)
char fake_heap[HEAP_SIZE];


// we override libnx internals to do a minimal init
void __libnx_initheap(void)
{
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end = fake_heap + HEAP_SIZE;
}

void __attribute__((weak)) __appInit(void)
{
    Result rc;
    svcSleepThread(10000000000L);

    rc = smInitialize();
    if (R_FAILED(rc))
        fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = hidInitialize();
    if (R_FAILED(rc))
        fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));

    rc = fsInitialize();
    if (R_FAILED(rc))
        fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    fsdevMountSdmc();
}

void __attribute__((weak)) __appExit(void)
{
    fsdevUnmountAll();
    fsExit();
    smExit();
    hidExit();
    audoutExit();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    FILE *f = fopen("/log", "w");
    stdout = f;
    stderr = f;
        
    DIR *dir;
    struct dirent *ent;

    dir = opendir("/music");

    while ((ent = readdir(dir)))
    {
        printf(ent->d_name);
        char filename[263];
        snprintf(filename, 263, "/music/%s", ent->d_name);
         // preparations for repeat song Implementation
        //repeats the song amount of times now 100 times preset as test ...
        for(int i = 0; i < 100; i ++)
        {
            playMp3(filename);
        }
        
    }
    closedir(dir);

    fclose(f);

    return 0;
}
