/***********************************************************************
Copyright (c) 2006-2012, Skype Limited. All rights reserved. 
Redistribution and use in source and binary forms, with or without 
modification, (subject to the limitations in the disclaimer below) 
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright 
notice, this list of conditions and the following disclaimer in the 
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific 
contributors, may be used to endorse or promote products derived from 
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED 
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND 
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/


/*****************************/
/* Silk encoder test program */
/*****************************/

#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE    1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SKP_Silk_SDK_API.h"

/* Define codec specific settings */
#define MAX_BYTES_PER_FRAME     250 // Equals peak bitrate of 100 kbps 
#define MAX_INPUT_FRAMES        5
#define FRAME_LENGTH_MS         20
#define MAX_API_FS_KHZ          48

#ifdef _SYSTEM_IS_BIG_ENDIAN
/* Function to convert a little endian int16 to a */
/* big endian int16 or vica verca                 */
void swap_endian(
    SKP_int16       vec[],              /*  I/O array of */
    SKP_int         len                 /*  I   length      */
)
{
    SKP_int i;
    SKP_int16 tmp;
    SKP_uint8 *p1, *p2;

    for( i = 0; i < len; i++ ){
        tmp = vec[ i ];
        p1 = (SKP_uint8 *)&vec[ i ]; p2 = (SKP_uint8 *)&tmp;
        p1[ 0 ] = p2[ 1 ]; p1[ 1 ] = p2[ 0 ];
    }
}
#endif

#if (defined(_WIN32) || defined(_WINCE))

#include <windows.h>    /* timer */

#else    // Linux or Mac
#include <sys/time.h>
#endif

#ifdef _WIN32

unsigned long GetHighResolutionTime() /* O: time in usec*/
{
    /* Returns a time counter in microsec	*/
    /* the resolution is platform dependent */
    /* but is typically 1.62 us resolution  */
    LARGE_INTEGER lpPerformanceCount;
    LARGE_INTEGER lpFrequency;
    QueryPerformanceCounter(&lpPerformanceCount);
    QueryPerformanceFrequency(&lpFrequency);
    return (unsigned long) ((1000000 * (lpPerformanceCount.QuadPart)) / lpFrequency.QuadPart);
}

#else    // Linux or Mac
unsigned long GetHighResolutionTime() /* O: time in usec*/
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return((tv.tv_sec*1000000)+(tv.tv_usec));
}
#endif // _WIN32

static void print_usage(char *argv[]) {
    printf("Usage: %s in.pcm out.bit [settings]\n", argv[0]);
    printf("\nin.pcm  : Speech input to encoder [16-bit unsigned PCM]");
    printf("\nout.bit : Bitstream output from encoder\n");
    printf("\nVersion:20200819    Build By Lin for mirai");
    printf("\nGithub: https://github.com/LXY1226/silk-encoder\n");
    printf("\nAdvanced settings:");
    printf("\n-isr <Hz>            : Input sampling rate [8,12,16,24,32,44.1,48]kHz, default: 24000");
    printf("\n-osr <Hz>            : Output sampling rate [8,12,16,24]kHz, default: 24000");
    printf("\n-packetlength <ms>   : Packet interval in ms, default: 20");
    printf("\n-rate <bps>          : Target bitrate; default: Auto-Calculated by file length");
    printf("\n-loss <perc>         : Uplink loss estimate, in percent (0-100); default: 0");
    printf("\n-inbandFEC <flag>    : Enable inband FEC usage (0/1); default: 0");
    printf("\n-complexity <comp>   : Set complexity, 0: low, 1: medium, 2: high; default: 2");
    printf("\n-DTX <flag>          : Enable DTX (0/1); default: 0");
    printf("\n-quiet               : Print only some basic values");
    printf("\n");
}

int main(int argc, char *argv[]) {
    unsigned long time;
    double filetime;
    size_t counter;
    SKP_int32 estPackets, totPackets, ret;
//    SKP_int32 k, args, estPackets, totPackets, totActPackets, ret;
    SKP_int16 nBytes;
//    double sumBytes, sumActBytes, avg_rate, act_rate, nrg;
    SKP_uint8 payload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES];
    SKP_int16 in[FRAME_LENGTH_MS * MAX_API_FS_KHZ * MAX_INPUT_FRAMES];
//    char speechInFileName[150], bitOutFileName[150];
    FILE *bitOutFile, *speechInFile;
    SKP_int32 encSizeBytes;
    void *psEnc;
#ifdef _SYSTEM_IS_BIG_ENDIAN
    SKP_int16 nBytes_LE;
#endif

    /* default settings */
    SKP_int32 smplsSinceLastPacket, packetSize_ms = 20;
//    SKP_int32 packetLoss_perc = 0;
#if LOW_COMPLEXITY_ONLY
    SKP_int32 complexity_mode = 0;
#else
    SKP_int32 complexity_mode = 2;
#endif
    SKP_int32 DTX_enabled = 0, INBandFEC_enabled = 0, quiet = 0;
    SKP_SILK_SDK_EncControlStruct encControl = {
            .packetSize = 20,
            .API_sampleRate = 24000,
            .maxInternalSampleRate = 24000,
            .bitRate = 25000,
            .useInBandFEC = 0,
            .useDTX = 0,
            .packetLossPercentage = 0,
            .complexity = 2,
    }; // Struct for input to encoder
    SKP_SILK_SDK_EncControlStruct encStatus;  // Struct for status of encoder

    if (argc < 3) {
        print_usage(argv);
        exit(0);
    }

    /* Open files */
    speechInFile = fopen(argv[1], "rb");
    if (speechInFile == NULL) {
        printf("Error: could not open input file %s\n", argv[1]);
        exit(0);
    }
    bitOutFile = fopen(argv[2], "wb");
    if (bitOutFile == NULL) {
        printf("Error: could not open output file %s\n", argv[2]);
        exit(0);
    }

    /* get arguments */
    SKP_int32 args = 3;
    while (args < argc) {
        if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-isr") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.API_sampleRate);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-Fs_maxInternal") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.maxInternalSampleRate);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-packetlength") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.packetSize);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-rate") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.bitRate);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-loss") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.packetLossPercentage);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-complexity") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.complexity);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-inbandFEC") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.useInBandFEC);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-DTX") == 0) {
            sscanf(argv[args + 1], "%d", &encControl.useDTX);
            args += 2;
        } else if (SKP_STR_CASEINSENSITIVE_COMPARE(argv[args], "-quiet") == 0) {
            quiet = 1;
            args++;
        } else {
            printf("Error: unrecognized setting: %s\n\n", argv[args]);
            print_usage(argv);
            exit(1);
        }
    }

    /* If no max internal is specified, set to minimum of API fs and 24 kHz */
    if (encControl.API_sampleRate < encControl.maxInternalSampleRate) {
        encControl.maxInternalSampleRate = encControl.API_sampleRate;
    }


    if (encControl.bitRate) {
        fseek(speechInFile, 0, SEEK_END);
        filetime = (double) ftell(speechInFile) / (double) (encControl.API_sampleRate * 16 / 8);
        estPackets = filetime * 50;
        encControl.bitRate = (1 << 20 << 3) / filetime;
        fseek(speechInFile, 0, SEEK_SET);
    }

    /* Print options */
    if (!quiet) {
        printf("**** Silk Encoder v%s ** %d bit ****\n", SKP_Silk_SDK_get_version(), (int) sizeof(void *) * 8);
        printf("Input sampling rate:    %d Hz\n", encControl.API_sampleRate);
        printf("Output sampling rate:   %d Hz\n", encControl.maxInternalSampleRate);
        printf("Packet interval:        %d ms\n", encControl.packetSize);
        printf("Inband FEC used:        %d\n", encControl.useInBandFEC);
        printf("DTX used:               %d\n", encControl.useDTX);
        printf("Complexity:             %d\n", complexity_mode);
    }

    /* Add Silk header to stream */
    static const char Silk_header[] = "#!SILK_V3";
    fwrite(Silk_header, sizeof(char), strlen(Silk_header), bitOutFile);

    /* Create Encoder */
    ret = SKP_Silk_SDK_Get_Encoder_Size(&encSizeBytes);
    if (ret) {
        printf("\nError: SKP_Silk_create_encoder returned %d\n", ret);
        exit(0);
    }

    psEnc = malloc(encSizeBytes);


    /* Set Encoder parameters */
    encControl.packetSize = (packetSize_ms * encControl.API_sampleRate) / 1000;

    if (encControl.API_sampleRate > MAX_API_FS_KHZ * 1000 || encControl.API_sampleRate < 0) {
        printf("\nError: API sampling rate = %d out of range, valid range 8000 - 48000 \n \n",
               encControl.API_sampleRate);
        exit(0);
    }

    reEncode:
    /* Reset Encoder */
    ret = SKP_Silk_SDK_InitEncoder(psEnc, &encStatus);
    if (ret) {
        printf("\nError: SKP_Silk_reset_encoder returned %d\n", ret);
        exit(0);
    }
    if (!quiet)
        printf("Target bitrate:         %d bps\n", encControl.bitRate);
    totPackets = 0;
//    totActPackets = 0;
//    sumActBytes = 0.0;
//    smplsSinceLastPacket = 0;
    time = GetHighResolutionTime();
    SKP_int32 frameSizeReadFromFile_ms = 20;
    while (1) {
        /* Read input from file */
        counter = fread(in, sizeof(SKP_int16), (frameSizeReadFromFile_ms * encControl.API_sampleRate) / 1000,
                        speechInFile);
#ifdef _SYSTEM_IS_BIG_ENDIAN
        swap_endian( in, counter );
#endif
        if ((SKP_int) counter < ((frameSizeReadFromFile_ms * encControl.API_sampleRate) / 1000))
            break;

        /* max payload size */
        nBytes = MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES;

        /* Silk Encoder */
        ret = SKP_Silk_SDK_Encode(psEnc, &encControl, in, (SKP_int16) counter, payload, &nBytes);
        if (ret) {
            printf("\nSKP_Silk_Encode returned %d", ret);
            exit(1);
        }

        /* Get packet size */
//        packetSize_ms = (SKP_int) ((1000 * (SKP_int32) encControl.packetSize) / encControl.API_sampleRate);

//        smplsSinceLastPacket += (SKP_int) counter;

//        if (((1000 * smplsSinceLastPacket) / encControl.API_sampleRate) == packetSize_ms) {
        /* Sends a dummy zero size packet in case of DTX period  */
        /* to make it work with the decoder test program.        */
        /* In practice should be handled by RTP sequence numbers */
        totPackets++;
//            nrg = 0.0;
//            for (k = 0; k < (SKP_int) counter; k++) {
//                nrg += in[k] * (double) in[k];
//            }
//            if ((nrg / (SKP_int) counter) > 1e3) {
//                sumActBytes += nBytes;
//                totActPackets++;
//            }

        /* Write payload size */
#ifdef _SYSTEM_IS_BIG_ENDIAN
        nBytes_LE = nBytes;
        swap_endian( &nBytes_LE, 1 );
        fwrite( &nBytes_LE, sizeof( SKP_int16 ), 1, bitOutFile );
#else
        fwrite(&nBytes, sizeof(SKP_int16), 1, bitOutFile);
#endif

        /* Write payload */
        fwrite(payload, sizeof(SKP_uint8), nBytes, bitOutFile);

//            smplsSinceLastPacket = 0;

        if (!quiet)
            fprintf(stderr, "\rPackets encoded:        %d/%d", totPackets, estPackets);
//        }
    }
    time = GetHighResolutionTime() - time;
    if (!quiet)
        printf("\nTime for encoding:      %.3f s (%.3fx)", 1e-6 * time,
               1e6 * filetime / time);
#define FILE_MAX_SIZE (1U << 20)
    if (ftell(bitOutFile) > FILE_MAX_SIZE) {
        printf("\n* file size exceeded... lowering bitrate\n");
//        encControl.bitRate -= (ftell(bitOutFile) - FILE_MAX_SIZE) / filetime;
        encControl.bitRate -= 1024;
        fseek(speechInFile, 0, SEEK_SET);
        fseek(bitOutFile, 0, SEEK_SET);
        goto reEncode;
    }

    /* Free Encoder */
    free(psEnc);

    float avg_rate = ftell(bitOutFile) * 8 / filetime;
    fclose(speechInFile);
    fclose(bitOutFile);

//    filetime = totPackets * 1e-3 * packetSize_ms;

//    act_rate = 8.0 / packetSize_ms * sumActBytes / totActPackets;
    if (!quiet) {
        printf("\nFile length:            %.3f s", filetime);
        printf("\nOverall bitrate:        %.0f bps\n", avg_rate);
    }
    return 0;
}
