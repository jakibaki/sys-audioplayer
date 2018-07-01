#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <mpg123.h>

#include <switch.h>

#define ERPT_SAVE_ID 0x80000000000000D1
#define TITLE_ID 0x4200000000000000
#define HEAP_SIZE 0x000040000


// we aren't an applet
u32 __nx_applet_type = AppletType_None;

// setup a fake heap (we don't need the heap anyway)
char fake_heap[HEAP_SIZE];

void fatalLater(Result err)
{
    Handle srv;

    while (R_FAILED(smGetServiceOriginal(&srv, smEncodeName("fatal:u"))))
    {
        // wait one sec and retry
        svcSleepThread(1000000000L);
    }

    // fatal is here time, fatal like a boss
    IpcCommand c;
    ipcInitialize(&c);
    ipcSendPid(&c);
    struct
    {
        u64 magic;
        u64 cmd_id;
        u64 result;
        u64 unknown;
    } * raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 1;
    raw->result = err;
    raw->unknown = 0;

    ipcDispatch(srv);
    svcCloseHandle(srv);
}

// we override libnx internals to do a minimal init
void __libnx_initheap(void)
{
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end = fake_heap + HEAP_SIZE;
}

void __appInit(void)
{
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
    rc = audoutInitialize();
    if (R_FAILED(rc))
        fatalLater(rc);
    rc = audoutStartAudioOut();
    if (R_FAILED(rc))
        fatalLater(rc);
}

void __appExit(void)
{
    fsdevUnmountAll();
    fsExit();
    smExit();
    audoutExit();
}




#define BUF_COUNT 2

static size_t			buffSize;
static mpg123_handle	*mh = NULL;
static uint32_t			rate;
static uint8_t			channels;

AudioOutBuffer*         audout_released_buf;

AudioOutBuffer          audiobuf[BUF_COUNT];
u8*                     buffData[BUF_COUNT];
int                     curBuf = 0;
#define swapbuf (curBuf = (curBuf+1)%(BUF_COUNT))



int initMp3(const char* file)
{
	int err = 0;
	int encoding = 0;

	if((err = mpg123_init()) != MPG123_OK)
		return err;

    mpg123_pars* pars = mpg123_new_pars(&err);
    mpg123_par(pars, MPG123_FORCE_RATE, audoutGetSampleRate(), 0);
    mpg123_par(pars, MPG123_FORCE_STEREO, 1, 0);

	if((mh = mpg123_parnew(pars, NULL, &err)) == NULL)
	{
		printf("Error: %s\n", mpg123_plain_strerror(err));
		return err;
	}



	if(mpg123_open(mh, file) != MPG123_OK ||
			mpg123_getformat(mh, (long *) &rate, (int *) &channels, &encoding) != MPG123_OK)
	{
		printf("Trouble with mpg123: %s\n", mpg123_strerror(mh));
		return -1;
	}

	/*
	 * Ensure that this output format will not change (it might, when we allow
	 * it).
	 */
	mpg123_format_none(mh);
	mpg123_format(mh, rate, channels, encoding);

	/*
	 * Buffer could be almost any size here, mpg123_outblock() is just some
	 * recommendation. The size should be a multiple of the PCM frame size.
	 */
	buffSize = mpg123_outblock(mh) * 16;

    for(int curBuf = 0; curBuf < BUF_COUNT; curBuf++) {
        buffData[curBuf] = memalign(0x1000, buffSize);
    }

	return 0;
}

/**
 * Get sampling rate of MP3 file.
 *
 * \return	Sampling rate.
 */
uint32_t rateMp3(void)
{
	return rate;
}

/**
 * Get number of channels of MP3 file.
 *
 * \return	Number of channels for opened file.
 */
uint8_t channelMp3(void)
{
	return channels;
}

/**
 * Decode part of open MP3 file.
 *
 * \param buffer	Decoded output.
 * \return			Samples read for each channel.
 */
uint64_t decodeMp3(void* buffer)
{
	size_t done = 0;
	mpg123_read(mh, buffer, buffSize, &done);
	return done / (sizeof(int16_t));
}

/**
 * Free MP3 decoder.
 */
void exitMp3(void)
{
    for(int curBuf = 0; curBuf < BUF_COUNT; curBuf++) {
        free(buffData[curBuf]);
    }
	mpg123_close(mh);
	mpg123_delete(mh);
	mpg123_exit();
}

int fillBuf() {
    int count = decodeMp3(buffData[curBuf]);
    if(count == 0)
        return count;
    audiobuf[curBuf].next = 0;
    audiobuf[curBuf].buffer = buffData[curBuf];
    audiobuf[curBuf].buffer_size = buffSize;
    audiobuf[curBuf].data_size = buffSize;
    audiobuf[curBuf].data_offset = 0;
    audoutAppendAudioOutBuffer(&audiobuf[curBuf]);
    swapbuf;
    return count;
}


void playMp3(char* file) {
    initMp3(file);

    u32 released_count = 0;
    
    for(int curBuf = 0; curBuf < BUF_COUNT/2; curBuf++)
        fillBuf();

    
    int lastFill = 1;
    while(appletMainLoop() && lastFill)
    {
        for(int curBuf = 0; curBuf < BUF_COUNT/2; curBuf++)
            lastFill = fillBuf();
        for(int curBuf = 0; curBuf < BUF_COUNT/2; curBuf++)
            audoutWaitPlayFinish(&audout_released_buf, &released_count, U64_MAX);
    }

    exitMp3();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    FILE *f = fopen("/log", "w");

    stdout = f;
    stderr = f;

    playMp3("/test.mp3");
    playMp3("/test2.mp3");
    

    fclose(f);

    return 0;
}
