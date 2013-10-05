/*
 * OpenAL Loopback Example
 *
 * Copyright (c) 2013 by Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This file contains an example for using the loopback device for custom
 * output handling.
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include <SDL.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "common/alhelpers.h"

#ifndef M_PI
#define M_PI    (3.14159265358979323846)
#endif

typedef struct {
    ALCdevice *Device;
    ALCcontext *Context;

    ALCsizei FrameSize;
} PlaybackInfo;

static LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT;
static LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT;
static LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT;


void SDLCALL RenderSDLSamples(void *userdata, Uint8 *stream, int len)
{
    PlaybackInfo *playback = (PlaybackInfo*)userdata;
    alcRenderSamplesSOFT(playback->Device, stream, len/playback->FrameSize);
}


/* Creates a one second buffer containing a sine wave, and returns the new
 * buffer ID. */
static ALuint CreateSineWave(void)
{
    ALshort data[44100];
    ALuint buffer;
    ALenum err;
    ALuint i;

    for(i = 0;i < 44100;i++)
        data[i] = (ALshort)(sin(i * 441.0 / 44100.0 * 2.0*M_PI)*32767.0);

    /* Buffer the audio data into a new buffer object. */
    buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, data, sizeof(data), 44100);

    /* Check if an error occured, and clean up if so. */
    err = alGetError();
    if(err != AL_NO_ERROR)
    {
        fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
        if(alIsBuffer(buffer))
            alDeleteBuffers(1, &buffer);
        return 0;
    }

    return buffer;
}


int main()
{
    PlaybackInfo playback = { NULL, NULL, 0 };
    SDL_AudioSpec desired, obtained;
    ALuint source, buffer;
    ALCint attrs[16];
    ALenum state;

    /* Print out error if extension is missing. */
    if(!alcIsExtensionPresent(NULL, "ALC_SOFT_loopback"))
    {
        fprintf(stderr, "Error: ALC_SOFT_loopback not supported!\n");
        return 1;
    }

    /* Define a macro to help load the function pointers. */
#define LOAD_PROC(x)  ((x) = alcGetProcAddress(NULL, #x))
    LOAD_PROC(alcLoopbackOpenDeviceSOFT);
    LOAD_PROC(alcIsRenderFormatSupportedSOFT);
    LOAD_PROC(alcRenderSamplesSOFT);
#undef LOAD_PROC

    if(SDL_Init(SDL_INIT_AUDIO) == -1)
    {
        fprintf(stderr, "Failed to init SDL audio: %s\n", SDL_GetError());
        return 1;
    }

    /* Set up SDL audio with our requested format and callback. */
    desired.channels = 2;
    desired.format = AUDIO_S16SYS;
    desired.freq = 44100;
    desired.padding = 0;
    desired.samples = 4096;
    desired.callback = RenderSDLSamples;
    desired.userdata = &playback;
    if(SDL_OpenAudio(&desired, &obtained) != 0)
    {
        SDL_Quit();
        fprintf(stderr, "Failed to open SDL audio: %s\n", SDL_GetError());
        return 1;
    }

    /* Set up our OpenAL attributes based on what we got from SDL. */
    attrs[0] = ALC_FORMAT_CHANNELS_SOFT;
    if(obtained.channels == 1)
        attrs[1] = ALC_MONO_SOFT;
    else if(obtained.channels == 2)
        attrs[1] = ALC_STEREO_SOFT;
    else
    {
        fprintf(stderr, "Unhandled SDL channel count: %d\n", obtained.channels);
        goto error;
    }

    attrs[2] = ALC_FORMAT_TYPE_SOFT;
    if(obtained.format == AUDIO_U8)
        attrs[3] = ALC_UNSIGNED_BYTE_SOFT;
    else if(obtained.format == AUDIO_S8)
        attrs[3] = ALC_BYTE_SOFT;
    else if(obtained.format == AUDIO_U16SYS)
        attrs[3] = ALC_UNSIGNED_SHORT_SOFT;
    else if(obtained.format == AUDIO_S16SYS)
        attrs[3] = ALC_SHORT_SOFT;
    else
    {
        fprintf(stderr, "Unhandled SDL format: 0x%04x\n", obtained.format);
        goto error;
    }

    attrs[4] = ALC_FREQUENCY;
    attrs[5] = obtained.freq;

    attrs[6] = 0; /* end of list */

    /* Initialize OpenAL loopback device, using our format attributes. */
    playback.Device = alcLoopbackOpenDeviceSOFT(NULL);
    if(!playback.Device)
    {
        fprintf(stderr, "Failed to open loopback device!\n");
        goto error;
    }
    /* Make sure the format is supported before setting them on the device. */
    if(alcIsRenderFormatSupportedSOFT(playback.Device, attrs[5], attrs[1], attrs[3]) == ALC_FALSE)
    {
        fprintf(stderr, "Render format not supported: %s, %s, %dhz\n",
                        ChannelsName(attrs[1]), TypeName(attrs[3]), attrs[5]);
        goto error;
    }
    playback.Context = alcCreateContext(playback.Device, attrs);
    if(!playback.Context || alcMakeContextCurrent(playback.Context) == ALC_FALSE)
    {
        fprintf(stderr, "Failed to set an OpenAL audio context\n");
        goto error;
    }
    playback.FrameSize = FramesToBytes(1, attrs[1], attrs[3]);

    /* Start SDL playing. Our callback (thus alcRenderSamplesSOFT) will now
     * start being called regularly to update the AL playback state. */
    SDL_PauseAudio(0);

    /* Load the sound into a buffer. */
    buffer = CreateSineWave();
    if(!buffer)
    {
        SDL_CloseAudio();
        alcDestroyContext(playback.Context);
        alcCloseDevice(playback.Device);
        SDL_Quit();
        return 1;
    }

    /* Create the source to play the sound with. */
    source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, buffer);
    assert(alGetError()==AL_NO_ERROR && "Failed to setup sound source");

    /* Play the sound until it finishes. */
    alSourcePlay(source);
    do {
        Sleep(10);
        alGetSourcei(source, AL_SOURCE_STATE, &state);
    } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);

    /* All done. Delete resources, and close OpenAL. */
    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);

    /* Stop SDL playing. */
    SDL_PauseAudio(1);

    /* Close up OpenAL and SDL. */
    SDL_CloseAudio();
    alcDestroyContext(playback.Context);
    alcCloseDevice(playback.Device);
    SDL_Quit();

    return 0;

error:
    SDL_CloseAudio();
    if(playback.Context)
        alcDestroyContext(playback.Context);
    if(playback.Device)
        alcCloseDevice(playback.Device);
    SDL_Quit();

    return 1;
}