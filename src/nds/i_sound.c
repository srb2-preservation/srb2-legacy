// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief NDS interface for sound

// uses stb_vorbis to decode OGG files and maxmod to play the PCM
// obviously because of that this only works on OGG files (most mods already use them)

#include "../i_sound.h"
#include "../s_sound.h"
#include "stb_vorbis.c"

#include <maxmod9.h>

stb_vorbis *vorbis = NULL;
stb_vorbis_info info;
mm_stream stream;

UINT8 sound_started = 0;

static mm_word StreamCallback(mm_word length, mm_addr dest, mm_stream_formats format)
{
    INT32 samples_per_channel = length * info.channels;
    INT32 read = stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, dest, samples_per_channel);

    return length;
}

void *I_GetSfx(sfxinfo_t *sfx)
{
	(void)sfx;
	return NULL;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	(void)sfx;
}

void I_StartupSound(void){}

void I_ShutdownSound(void){}

void I_UpdateSound(void){}

//
//  SFX I/O
//

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	(void)id;
	(void)vol;
	(void)sep;
	(void)pitch;
	(void)priority;
	(void)channel;
	return -1;
}

void I_StopSound(INT32 handle)
{
	(void)handle;
}

boolean I_SoundIsPlaying(INT32 handle)
{
	(void)handle;
	return false;
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	(void)handle;
	(void)vol;
	(void)sep;
	(void)pitch;
}

void I_SetSfxVolume(UINT8 volume)
{
	(void)volume;
}

/// ------------------------
//  MUSIC SYSTEM
/// ------------------------

void I_InitMusic(void)
{
    mmInitNoSoundbank();
}

void I_ShutdownMusic(void){}

/// ------------------------
//  MUSIC PROPERTIES
/// ------------------------

musictype_t I_SongType(void)
{
	return MU_NONE;
}

boolean I_SongPlaying(void)
{
	return false;
}

boolean I_SongPaused(void)
{
	return false;
}

/// ------------------------
//  MUSIC EFFECTS
/// ------------------------

boolean I_SetSongSpeed(float speed)
{
	(void)speed;
	return false;
}

/// ------------------------
//  MUSIC SEEKING
/// ------------------------

UINT32 I_GetSongLength(void)
{
	return 0;
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
        (void)looppoint;
        return false;
}

UINT32 I_GetSongLoopPoint(void)
{
	return 0;
}

boolean I_SetSongPosition(UINT32 position)
{
    (void)position;
    return false;
}

UINT32 I_GetSongPosition(void)
{
    return 0;
}

/// ------------------------
//  MUSIC PLAYBACK
/// ------------------------

boolean I_LoadSong(char *data, size_t len)
{
	if (!cv_gamedigimusic.value) { return true; }

    int error;
    vorbis = stb_vorbis_open_memory(data, len, &error, NULL);

	// if it failed somehow
    if (!vorbis)
        return false;

    info = stb_vorbis_get_info(vorbis);

    return true;
}

void I_UnloadSong(void)
{
	if (!cv_gamedigimusic.value) { return; }

    mmStreamClose();

    if (vorbis)
    {
        stb_vorbis_close(vorbis);
        vorbis = NULL;
    }
}

boolean I_PlaySong(boolean looping)
{
	(void)looping;
	if (!cv_gamedigimusic.value) { return true; }

	// stop currently playing music
	I_StopSong();

	// set stream properties
    stream.sampling_rate = info.sample_rate;
    stream.buffer_length = 1024;
    stream.callback = StreamCallback;
    stream.format = (info.channels > 1) ? MM_STREAM_16BIT_STEREO : MM_STREAM_16BIT_MONO;
    stream.timer = MM_TIMER3;
    stream.manual = false;

    mmStreamOpen(&stream);

    return true;
}

void I_StopSong(void)
{
    mmStreamClose();
}

void I_PauseSong(void)
{
}

void I_ResumeSong(void)
{
}

void I_SetMusicVolume(UINT8 volume)
{
	(void)volume;
}

boolean I_SetSongTrack(INT32 track)
{
	(void)track;
	return false;
}

/// ------------------------
//  MUSIC FADING
/// ------------------------

void I_SetInternalMusicVolume(UINT8 volume)
{
	(void)volume;
}

void I_StopFadingSong(void)
{
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	(void)target_volume;
	(void)source_volume;
	(void)ms;
	return false;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	(void)target_volume;
	(void)ms;
	return false;
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	(void)ms;
	return false;
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
        (void)ms;
        (void)looping;
        return false;
}
