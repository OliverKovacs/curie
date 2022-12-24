#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "portaudio.h"

// #define SAMPLE_RATE  (17932) // Test failure to open with this value.
#define SAMPLE_RATE         (44100)
#define FRAMES_PER_BUFFER   (512)
#define NUM_SECONDS         (6)
#define TIME_LIMIT          (false)
// #define DITHER_FLAG         (paDitherOff)
#define DITHER_FLAG         (0)

// Select sample format.
#if 1
#define PA_SAMPLE_TYPE  paFloat32
#define CPP_SAMPLE_TYPE float
#define SAMPLE_SIZE (4)
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 0
#define PA_SAMPLE_TYPE  paInt16
#define CPP_SAMPLE_TYPE int16_t
#define SAMPLE_SIZE (2)
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
// #define PA_SAMPLE_TYPE  paInt24
// #define CPP_SAMPLE_TYPE 
// #define SAMPLE_SIZE (3)
// #define SAMPLE_SILENCE  (0)
// #define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
#define CPP_SAMPLE_TYPE int8_t
#define SAMPLE_SIZE (1)
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
#define CPP_SAMPLE_TYPE uint8_t
#define SAMPLE_SIZE (1)
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

int main(int argc, char *argv[]) {
    int opt;

    bool debug = false;
    bool echo = false;

    while ((opt = getopt(argc, argv, ":de")) != -1) {
        switch (opt) {
            case 'd':
                debug = true;
                break;
            case 'e':
                echo = true;
                break;
        }
    }

    FILE *fp = fopen("./log", "a");
    fprintf(fp, "lorem ipsum\n");
    fflush(fp);

    PaStreamParameters inputParameters;
    PaStreamParameters outputParameters;
    PaStream *stream = NULL;
    PaError err;
    const PaDeviceInfo* inputInfo;
    const PaDeviceInfo* outputInfo;
    char *sampleBlock = NULL;
    size_t numBytes;
    size_t numChannels;
    
    err = Pa_Initialize();
    if (err != paNoError) goto error2;

    inputParameters.device = Pa_GetDefaultInputDevice();    // default input device
    inputInfo = Pa_GetDeviceInfo(inputParameters.device);

    outputParameters.device = Pa_GetDefaultOutputDevice();  // default output device
    outputInfo = Pa_GetDeviceInfo(outputParameters.device);
    
    if (debug) {
        printf("Input device # %d.\n", inputParameters.device);
        printf("    Name: %s\n", inputInfo->name);
        printf("      LL: %g s\n", inputInfo->defaultLowInputLatency);
        printf("      HL: %g s\n", inputInfo->defaultHighInputLatency);

        printf("Output device # %d.\n", outputParameters.device);
        printf("   Name: %s\n", outputInfo->name);
        printf("     LL: %g s\n", outputInfo->defaultLowOutputLatency);
        printf("     HL: %g s\n", outputInfo->defaultHighOutputLatency);
    }
    
    numChannels = inputInfo->maxInputChannels < outputInfo->maxOutputChannels
        ? inputInfo->maxInputChannels
        : outputInfo->maxOutputChannels;

    numChannels = 2;
    
    if (debug) {
        printf("Num channels = %zu.\n", numChannels);
    }

    inputParameters.channelCount = numChannels;
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = inputInfo->defaultHighInputLatency ;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    outputParameters.channelCount = numChannels;
    outputParameters.sampleFormat = PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = outputInfo->defaultHighOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    // -- setup --
    err = Pa_OpenStream(
        &stream,
        &inputParameters,
        &outputParameters,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,          // we won't output out of range samples so don't bother clipping them
        NULL,               // no callback, use blocking API
        NULL);              // no callback, so no callback userData
    if (err != paNoError) goto error2;

    numBytes = FRAMES_PER_BUFFER * numChannels * SAMPLE_SIZE;
    sampleBlock = (char *)malloc(numBytes);
    if (sampleBlock == NULL) {
        printf("Could not allocate record array.\n");
        goto error1;
    }
    memset(sampleBlock, SAMPLE_SILENCE, numBytes);

    float *floatBlock = (float *)sampleBlock;

    err = Pa_StartStream(stream);
    if (err != paNoError) goto error1;
    
    if (debug) {
        printf("Start: %lu\n", (uint64_t)time(NULL));
        if (TIME_LIMIT) {
            printf("Will run %d seconds.\n", NUM_SECONDS);
        }
        else {
            printf("Will run indefinitely.\n");
        }
        fflush(stdout);
    }

    // recorded channel
    size_t channel = 0;
    
    uint64_t count = 0;
    bool state = 0;
    float positive = 0.5;
    float negative = -0.5;

    for (size_t i = 0; !TIME_LIMIT || i < (NUM_SECONDS * SAMPLE_RATE) / FRAMES_PER_BUFFER; ++i) {
        // You may get underruns or overruns if the output is not primed by PortAudio.
        if (echo) {
            err = Pa_WriteStream(stream, sampleBlock, FRAMES_PER_BUFFER);
            if (err) goto xrun;
        }
        err = Pa_ReadStream(stream, sampleBlock, FRAMES_PER_BUFFER);
        if (err) goto xrun;
        for (size_t j = 0; j < FRAMES_PER_BUFFER; j++) {
            float f = floatBlock[j * numChannels + channel];
            if (!state && f > positive) state = true;
            if (state && f < negative) {
                state = false;
                count++;
                if (debug) {
                    printf("\r%lu", count);
                }
                else {
                    printf("\n");
                }
                fflush(stdout);
            }
            // if (f > 0.50) printf("%*.2f\n", 10, f);
        }
    }

    printf("Wire off.\n");
    fflush(stdout);

    err = Pa_StopStream(stream);
    if (err != paNoError) goto error1;

    free(sampleBlock);

    Pa_Terminate();

    fclose(fp);
    return 0;

xrun:
    fprintf(stderr, "err = %d\n", err);
    fflush(stdout);

    if (stream) {
        Pa_AbortStream(stream);
        Pa_CloseStream(stream);
    }
    free(sampleBlock);
    Pa_Terminate();
    if (err & paInputOverflow) {
        fprintf(stderr, "Input Overflow.\n");
    }
    if (err & paOutputUnderflow) {
        fprintf(stderr, "Output Underflow.\n");
    }
    fclose(fp);
    return -2;
error1:
    free(sampleBlock);
error2:
    if (stream) {
        Pa_AbortStream(stream);
        Pa_CloseStream(stream);
    }
    Pa_Terminate();
    fprintf(stderr, "An error occurred while using the portaudio stream\n");
    fprintf(stderr, "Error number: %d\n", err);
    fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));

    fclose(fp);
    return -1;
}
