#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>

#include <switch.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#define ERPT_SAVE_ID 0x80000000000000D1
#define TITLE_ID 0x4200000000000000
#define HEAP_SIZE 0x000040000
#define M_PI 3.14159265358979323846


#define SAMPLERATE 48000
#define CHANNELCOUNT 2
#define FRAMERATE (1000 / 30)
#define SAMPLECOUNT (SAMPLERATE / FRAMERATE)
#define BYTESPERSAMPLE 2

void fill_audio_buffer(void* audio_buffer, size_t offset, size_t size, int frequency) {
    if (audio_buffer == NULL) return;
    
    u32* dest = (u32*) audio_buffer;
    for (size_t i = 0; i < size; i++) {
        // This is a simple sine wave, with a frequency of `frequency` Hz, and an amplitude 30% of maximum.
        s16 sample = 0.3 * 0x7FFF * sin(frequency * (2 * M_PI) * (offset + i) / SAMPLERATE);

        // Stereo samples are interleaved: left and right channels.
        dest[i] = (sample << 16) | (sample & 0xffff);
    }
}


// we aren't an applet
u32 __nx_applet_type = AppletType_None;

// setup a fake heap (we don't need the heap anyway)
char   fake_heap[HEAP_SIZE];

void fatalLater(Result err) {
    Handle srv;

    while (R_FAILED(smGetServiceOriginal(&srv, smEncodeName("fatal:u")))) {
        // wait one sec and retry
        svcSleepThread(1000000000L);
    }

    // fatal is here time, fatal like a boss    
    IpcCommand c;
    ipcInitialize(&c);
    ipcSendPid(&c);
    struct {
        u64 magic;
        u64 cmd_id;
        u64 result;
        u64 unknown;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 1;
    raw->result = err;
    raw->unknown = 0;

    ipcDispatch(srv);
    svcCloseHandle(srv);
}

// we override libnx internals to do a minimal init
void __libnx_initheap(void) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end   = fake_heap + HEAP_SIZE;
}

void __appInit(void) {
    Result rc;
    svcSleepThread(5000000000L);

    rc = smInitialize();
    if (R_FAILED(rc))
        fatalLater(rc);
    rc = fsInitialize();
    if (R_FAILED(rc))
        fatalLater(rc);
    rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        fatalLater(rc);

}

void __appExit(void) {
    fsdevUnmountAll();
    fsExit();
    smExit();
}



int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    FILE* f = fopen("/log", "w");
    if(f == NULL) {
        fatalSimple(12);
    }

    stdout = f;
    stderr = f;

    Result rc = 0;

    printf("Init successful... Waiting for 15 seconds now for audio...\n");

    AudioOutBuffer audout_buf;
    AudioOutBuffer *audout_released_buf;
    
    // Make sure the sample buffer size is aligned to 0x1000 bytes.
    u32 data_size = (SAMPLECOUNT * CHANNELCOUNT * BYTESPERSAMPLE);
    u32 buffer_size = (data_size + 0xfff) & ~0xfff;
    
    // Allocate the buffer.
    u8* out_buf_data = memalign(0x1000, buffer_size);
    
    // Ensure buffers were properly allocated.
    if (out_buf_data == NULL)
    {
        rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        printf("Failed to allocate sample data buffers\n");
        fclose(f);
        fatalSimple(rc);
    }
    
    if (R_SUCCEEDED(rc))
        memset(out_buf_data, 0, buffer_size);
    

    rc = audoutInitialize();
    printf("audoutInitialize() returned 0x%x\n", rc);
    if(R_FAILED(rc)) {
        fclose(f);
        fatalSimple(rc);
    }
    printf("Sample rate: 0x%x\n", audoutGetSampleRate());
    printf("Channel count: 0x%x\n", audoutGetChannelCount());
    printf("PCM format: 0x%x\n", audoutGetPcmFormat());
    printf("Device state: 0x%x\n", audoutGetDeviceState());
    
    // Start audio playback.
    rc = audoutStartAudioOut();
    printf("audoutStartAudioOut() returned 0x%x\n", rc);
    if(R_FAILED(rc)) {
        fclose(f);
        fatalSimple(rc);
    }

    for(;;) {
        fill_audio_buffer(out_buf_data, 0, data_size, 220);
        audout_buf.next = NULL;
        audout_buf.buffer = out_buf_data;
        audout_buf.buffer_size = buffer_size;
        audout_buf.data_size = data_size;
        audout_buf.data_offset = 0;
        
        // Prepare pointer for the released buffer.
        audout_released_buf = NULL;
        
        // Play the buffer.
        rc = audoutPlayBuffer(&audout_buf, &audout_released_buf);
        
        if (R_FAILED(rc)) {
            printf("audoutPlayBuffer() returned 0x%x\n", rc);
            fclose(f);
            fatalSimple(rc);
        }
        svcSleepThread(1000000000L);
    }


    fclose(f);
    return 0;
}
