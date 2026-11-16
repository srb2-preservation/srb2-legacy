// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2014-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief SDL Mixer interface for sound

#include "../doomdef.h"

#if defined(HAVE_SDL) && defined(HAVE_MIXER) && SOUND==SOUND_MIXER

#include "../sounds.h"
#include "../s_sound.h"
#include "../i_sound.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../byteptr.h"
#include "../m_argv.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#ifdef HAVE_GME
#include "gme/gme.h"
#define GME_TREBLE 5.0f
#define GME_BASS 1.0f

#ifdef HAVE_ZLIB
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 0
#endif

#include "zlib.h"
#endif // HAVE_ZLIB
#endif // HAVE_GME

#define SAMPLERATE 44100

// SDL_mixer converts everything to the device's format on its own. This is the
// format of the audio the game generates itself (sound effects, GME, OpenMPT).
static const SDL_AudioSpec mixer_spec = {SDL_AUDIO_S16, 2, SAMPLERATE};

// Number of sound effect tracks; the game manages its own channels on top of these.
#define SFX_TRACKS 256

#ifdef HAVE_OPENMPT
#include "libopenmpt/libopenmpt.h"
#endif

/// ------------------------
/// Audio Declarations
/// ------------------------

UINT8 sound_started = false;

static MIX_Mixer *mixer;
static MIX_Track *sfx_tracks[SFX_TRACKS]; // created on first use
static MIX_Track *music_track;
static SDL_AudioStream *music_stream; // carries GME and OpenMPT output to music_track

static MIX_Audio *music;
static musictype_t music_type; // type of the song in music
static UINT8 music_volume, sfx_volume, internal_volume;
static Sint64 loop_point; // in sample frames of the song
static boolean songpaused;
static boolean is_looping;

// fading
static boolean is_fading;
static UINT8 fading_source;
static UINT8 fading_target;
static UINT32 fading_timer;
static UINT32 fading_duration;
static SDL_TimerID fading_id;
static void (*fading_callback)(void);

#ifdef HAVE_GME
static Music_Emu *gme;
static UINT16 current_track;
#endif

#ifdef HAVE_OPENMPT
int mod_err = OPENMPT_ERROR_OK;
static const char *mod_err_str;
static UINT16 current_subsong;
#endif

static void var_cleanup(void)
{
	loop_point = 0;
	fading_source = fading_target =\
	 fading_timer = fading_duration = 0;

	songpaused = is_looping =\
	 is_fading = false;

	fading_callback = NULL;

	internal_volume = 100;
}

#if defined (HAVE_GME) && defined (HAVE_ZLIB)
static const char* get_zlib_error(int zErr)
{
	switch (zErr)
	{
		case Z_ERRNO:
			return "Z_ERRNO";
		case Z_STREAM_ERROR:
			return "Z_STREAM_ERROR";
		case Z_DATA_ERROR:
			return "Z_DATA_ERROR";
		case Z_MEM_ERROR:
			return "Z_MEM_ERROR";
		case Z_BUF_ERROR:
			return "Z_BUF_ERROR";
		case Z_VERSION_ERROR:
			return "Z_VERSION_ERROR";
		default:
			return "unknown error";
	}
}
#endif

/// ------------------------
/// Audio System
/// ------------------------

void I_StartupSound(void)
{
	I_Assert(!sound_started);

	// EE inits audio first so we're following along.
	if (SDL_WasInit(SDL_INIT_AUDIO) == SDL_INIT_AUDIO)
	{
		CONS_Debug(DBG_DETAILED, "SDL Audio already started\n");
		return;
	}
	else if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		CONS_Alert(CONS_ERROR, "Error initializing SDL Audio: %s\n", SDL_GetError());
		// call to start audio failed -- we do not have it
		return;
	}

	var_cleanup();

	music = NULL;
	music_volume = sfx_volume = 0;

	if (!MIX_Init())
	{
		CONS_Alert(CONS_ERROR, "Error starting SDL_Mixer: %s\n", SDL_GetError());
		// call to start audio failed -- we do not have it
		return;
	}

	mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixer_spec);
	if (mixer)
		music_track = MIX_CreateTrack(mixer);
	if (!mixer || !music_track)
	{
		CONS_Alert(CONS_ERROR, "Error starting SDL_Mixer: %s\n", SDL_GetError());
		// call to start audio failed -- we do not have it
		if (mixer)
			MIX_DestroyMixer(mixer);
		mixer = NULL;
		MIX_Quit();
		return;
	}

	const int SDLmixlinked = MIX_Version();

	I_OutputMsg("Compiled for SDL_mixer version: %d.%d.%d\n",
				SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_MICRO_VERSION);
	I_OutputMsg("Linked with SDL_mixer version: %d.%d.%d\n",
				SDL_VERSIONNUM_MAJOR(SDLmixlinked), SDL_VERSIONNUM_MINOR(SDLmixlinked), SDL_VERSIONNUM_MICRO(SDLmixlinked));

#ifdef HAVE_OPENMPT
	const char *openmptstr;
	openmptstr = openmpt_get_string("library_version");
	I_OutputMsg("libopenmpt version: %s\n", openmptstr);
	openmpt_free_string(openmptstr);
	openmptstr = openmpt_get_string("build");
	I_OutputMsg("libopenmpt build date: %s\n", openmptstr);
	openmpt_free_string(openmptstr);
#endif

	sound_started = true;
	songpaused = false;
}

void I_ShutdownSound(void)
{
	if (!sound_started)
		return; // not an error condition
	sound_started = false;

	// Destroying the mixer destroys its tracks too. The stream goes after it,
	// so that nothing can be reading from the stream anymore.
	MIX_DestroyMixer(mixer);
	mixer = NULL;
	music_track = NULL;
	memset(sfx_tracks, 0, sizeof(sfx_tracks));
	if (music_stream)
	{
		SDL_DestroyAudioStream(music_stream);
		music_stream = NULL;
	}
	MIX_Quit();

	SDL_QuitSubSystem(SDL_INIT_AUDIO);

#ifdef HAVE_GME
	if (gme)
		gme_delete(gme);
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		openmpt_module_destroy(openmpt_mhandle);
#endif
}

void I_UpdateSound(void)
{
}

/// ------------------------
/// SFX
/// ------------------------

// Hands raw audio in mixer_spec format over to the mixer, which keeps a copy of its own.
static MIX_Audio *raw2audio(void *data, size_t len)
{
	MIX_Audio *audio = MIX_LoadRawAudio(mixer, data, len, &mixer_spec);
	Z_Free(data);
	return audio;
}

// this is as fast as I can possibly make it.
// sorry. more asm needed.
static MIX_Audio *ds2chunk(void *stream)
{
	UINT16 ver,freq;
	UINT32 samples, i, newsamples;
	UINT8 *sound;

	SINT8 *s;
	INT16 *d;
	INT16 o;
	fixed_t step, frac;

	// lump header
	ver = READUINT16(stream); // sound version format?
	if (ver != 3) // It should be 3 if it's a doomsound...
		return NULL; // onos! it's not a doomsound!
	freq = READUINT16(stream);
	samples = READUINT32(stream);

	// convert from signed 8bit ???hz to signed 16bit 44100hz.
	switch(freq)
	{
	case 44100:
		if (samples >= UINT32_MAX>>2)
			return NULL; // would wrap, can't store.
		newsamples = samples;
		break;
	case 22050:
		if (samples >= UINT32_MAX>>3)
			return NULL; // would wrap, can't store.
		newsamples = samples<<1;
		break;
	case 11025:
		if (samples >= UINT32_MAX>>4)
			return NULL; // would wrap, can't store.
		newsamples = samples<<2;
		break;
	default:
		frac = (44100 << FRACBITS) / (UINT32)freq;
		if (!(frac & 0xFFFF)) // other solid multiples (change if FRACBITS != 16)
			newsamples = samples * (frac >> FRACBITS);
		else // strange and unusual fractional frequency steps, plus anything higher than 44100hz.
			newsamples = FixedMul(FixedDiv(samples, freq), 44100) + 1; // add 1 to counter truncation.
		if (newsamples >= UINT32_MAX>>2)
			return NULL; // would and/or did wrap, can't store.
		break;
	}
	sound = Z_Malloc(newsamples<<2, PU_SOUND, NULL); // samples * frequency shift * bytes per sample * channels

	s = (SINT8 *)stream;
	d = (INT16 *)sound;

	i = 0;
	switch(freq)
	{
	case 44100: // already at the same rate? well that makes it simple.
		while(i++ < samples)
		{
			o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
			*d++ = o; // left channel
			*d++ = o; // right channel
		}
		break;
	case 22050: // unwrap 2x
		while(i++ < samples)
		{
			o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
		}
		break;
	case 11025: // unwrap 4x
		while(i++ < samples)
		{
			o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
			*d++ = o; // left channel
			*d++ = o; // right channel
		}
		break;
	default: // convert arbitrary hz to 44100.
		step = 0;
		frac = ((UINT32)freq << FRACBITS) / 44100 + 1; //Add 1 to counter truncation.
		while (i < samples)
		{
			o = (INT16)(*s+0x80)<<8; // changed signedness and shift up to 16 bits
			while (step < FRACUNIT) // this is as fast as I can make it.
			{
				*d++ = o; // left channel
				*d++ = o; // right channel
				step += frac;
			}
			do {
				i++; s++;
				step -= FRACUNIT;
			} while (step >= FRACUNIT);
		}
		break;
	}

	// return Mixer audio.
	return raw2audio(sound, (size_t)((UINT8*)d-sound));
}

void *I_GetSfx(sfxinfo_t *sfx)
{
	void *lump;
	MIX_Audio *chunk;
	SDL_IOStream *rw;
#ifdef HAVE_GME
	Music_Emu *emu;
	gme_info_t *info;
#endif

	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);
	sfx->length = W_LumpLength(sfx->lumpnum);

	if (!mixer)
		return NULL;

	lump = W_CacheLumpNum(sfx->lumpnum, PU_SOUND);

	// convert from standard DoomSound format.
	chunk = ds2chunk(lump);
	if (chunk)
	{
		Z_Free(lump);
		return chunk;
	}

	// Not a doom sound? Try something else.
#ifdef HAVE_GME
	// VGZ format
	if (((UINT8 *)lump)[0] == 0x1F
		&& ((UINT8 *)lump)[1] == 0x8B)
	{
#ifdef HAVE_ZLIB
		UINT8 *inflatedData;
		size_t inflatedLen;
		z_stream stream;
		int zErr; // Somewhere to handle any error messages zlib tosses out

		memset(&stream, 0x00, sizeof (z_stream)); // Init zlib stream
		// Begin the inflation process
		inflatedLen = *(UINT32 *)lump + (sfx->length-4); // Last 4 bytes are the decompressed size, typically
		inflatedData = (UINT8 *)Z_Malloc(inflatedLen, PU_SOUND, NULL); // Make room for the decompressed data
		stream.total_in = stream.avail_in = sfx->length;
		stream.total_out = stream.avail_out = inflatedLen;
		stream.next_in = (UINT8 *)lump;
		stream.next_out = inflatedData;

		zErr = inflateInit2(&stream, 32 + MAX_WBITS);
		if (zErr == Z_OK) // We're good to go
		{
			zErr = inflate(&stream, Z_FINISH);
			if (zErr == Z_STREAM_END)
			{
				// Run GME on new data
				if (!gme_open_data(inflatedData, inflatedLen, &emu, 44100))
				{
					short *mem;
					UINT32 len;
					gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

					Z_Free(inflatedData); // GME supposedly makes a copy for itself, so we don't need this lying around
					Z_Free(lump); // We're done with the uninflated lump now, too.

					gme_start_track(emu, 0);
					gme_set_equalizer(emu, &eq);
					gme_track_info(emu, &info, 0);

					len = (info->play_length * 441 / 10) << 2;
					mem = Z_Malloc(len, PU_SOUND, 0);
					gme_play(emu, len >> 1, mem);
					gme_free_info(info);
					gme_delete(emu);

					return raw2audio(mem, len);
				}
			}
			else
				CONS_Alert(CONS_ERROR,"Encountered %s when running inflate: %s\n", get_zlib_error(zErr), stream.msg);
			(void)inflateEnd(&stream);
		}
		else // Hold up, zlib's got a problem
			CONS_Alert(CONS_ERROR,"Encountered %s when running inflateInit: %s\n", get_zlib_error(zErr), stream.msg);
		Z_Free(inflatedData); // GME didn't open jack, but don't let that stop us from freeing this up
#else
		return NULL; // No zlib support
#endif
	}
	// Try to read it as a GME sound
	else if (!gme_open_data(lump, sfx->length, &emu, 44100))
	{
		short *mem;
		UINT32 len;
		gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

		Z_Free(lump);

		gme_start_track(emu, 0);
		gme_set_equalizer(emu, &eq);
		gme_track_info(emu, &info, 0);

		len = (info->play_length * 441 / 10) << 2;
		mem = Z_Malloc(len, PU_SOUND, 0);
		gme_play(emu, len >> 1, mem);
		gme_free_info(info);
		gme_delete(emu);

		return raw2audio(mem, len);
	}
#endif

	// Try to load it as a WAVE or OGG using Mixer.
	// It gets decoded in full right away, so the lump isn't needed afterwards.
	rw = SDL_IOFromConstMem(lump, sfx->length);
	chunk = rw ? MIX_LoadAudio_IO(mixer, rw, true, true) : NULL;
	Z_Free(lump);

	return chunk; // NULL if we haven't been able to get anything
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	// The audio is reference counted, so a track that is still playing it keeps it alive.
	if (sfx->data)
		MIX_DestroyAudio(sfx->data);
	sfx->data = NULL;
	sfx->lumpnum = LUMPERROR;
}

static MIX_Track *get_sfx_track(INT32 handle)
{
	if (!mixer || handle < 0 || handle >= SFX_TRACKS)
		return NULL;

	if (!sfx_tracks[handle])
		sfx_tracks[handle] = MIX_CreateTrack(mixer);

	return sfx_tracks[handle];
}

static void set_sfx_params(MIX_Track *track, UINT8 vol, UINT8 sep)
{
	UINT8 volume = (((UINT16)vol + 1) * (UINT16)sfx_volume) / 62; // (256 * 31) / 62 == 127
	MIX_StereoGains gains;

	gains.left = min((UINT16)(0xff-sep)<<1, 0xff) / 255.0f;
	gains.right = min((UINT16)(sep)<<1, 0xff) / 255.0f;

	MIX_SetTrackGain(track, volume / 128.0f);
	MIX_SetTrackStereo(track, &gains);
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	MIX_Track *track = get_sfx_track(channel);

	(void)pitch; // pitch is not used
	(void)priority; // priority and channel management is handled by SRB2...

	if (!track || !S_sfx[id].data)
		return -1;

	if (!MIX_SetTrackAudio(track, S_sfx[id].data))
		return -1;

	set_sfx_params(track, vol, sep);

	if (!MIX_PlayTrack(track, 0))
		return -1;

	return channel;
}

void I_StopSound(INT32 handle)
{
	MIX_Track *track = get_sfx_track(handle);
	if (track)
		MIX_StopTrack(track, 0);
}

boolean I_SoundIsPlaying(INT32 handle)
{
	MIX_Track *track = get_sfx_track(handle);
	return track && MIX_TrackPlaying(track);
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	MIX_Track *track = get_sfx_track(handle);
	if (track)
		set_sfx_params(track, vol, sep);
	(void)pitch;
}

void I_SetSfxVolume(UINT8 volume)
{
	sfx_volume = volume;
}

/// ------------------------
/// Music Utilities
/// ------------------------

static UINT32 get_real_volume(UINT8 volume)
{
	// convert volume to mixer's 128 scale
	// then apply internal_volume as a percentage
	return ((UINT32)volume*128/31) * (UINT32)internal_volume / 100;
}

static void update_music_volume(void)
{
	if (!music_track)
		return;

	// GME and OpenMPT apply the volume to their samples by themselves
	if (music_stream)
		MIX_SetTrackGain(music_track, 1.0f);
	else
		MIX_SetTrackGain(music_track, get_real_volume(music_volume) / 128.0f);
}

// Converts between milliseconds and sample frames of the song in music
static Sint64 music_ms_to_frames(UINT32 ms)
{
	Sint64 frames = MIX_AudioMSToFrames(music, ms);
	return max(frames, 0);
}

static UINT32 music_frames_to_ms(Sint64 frames)
{
	Sint64 ms = frames > 0 ? MIX_AudioFramesToMS(music, frames) : 0;
	return (UINT32)max(ms, 0);
}

// (Re)starts the song in music from this sample frame, with the current loop settings
static boolean play_music_track(Sint64 start_frame)
{
	SDL_PropertiesID props = SDL_CreateProperties();
	boolean played;

	SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, is_looping ? -1 : 0);
	if (music_type != MU_MOD && music_type != MU_MID)
		SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER, loop_point);
	if (start_frame > 0)
		SDL_SetNumberProperty(props, MIX_PROP_PLAY_START_FRAME_NUMBER, start_frame);

	played = MIX_PlayTrack(music_track, props);
	SDL_DestroyProperties(props);

	return played;
}

// SDL_mixer doesn't tell what kind of file it loaded, so look at the data.
static musictype_t detect_music_type(const UINT8 *data, size_t len)
{
	if (len >= 12 && !memcmp(data, "RIFF", 4))
		return !memcmp(data + 8, "RMID", 4) ? MU_MID : MU_WAV;
	if (len >= 4 && !memcmp(data, "MThd", 4))
		return MU_MID;
	if (len >= 4 && !memcmp(data, "OggS", 4))
		return MU_OGG;
	if (len >= 4 && !memcmp(data, "fLaC", 4))
		return MU_FLAC;
	if (len >= 4 && !memcmp(data, "FORM", 4))
		return MU_WAV; // AIFF
	if (len >= 3 && (!memcmp(data, "ID3", 3) || (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)))
		return MU_MP3;

	return MU_MOD; // everything else that loads is a tracker module
}

static UINT32 get_adjusted_position(UINT32 position)
{
	// all in milliseconds
	UINT32 length = I_GetSongLength();
	UINT32 looppoint = I_GetSongLoopPoint();
	if (length)
		return position >= length ? (position % (length-looppoint)) : position;
	else
		return position;
}

static void do_fading_callback(void)
{
	if (fading_callback)
		(*fading_callback)();
	fading_callback = NULL;
}

// SDL2_mixer picked the SoundFont for MIDI from the SDL_SOUNDFONTS environment
// variable. SDL3_mixer only looks in a few default places by itself (and plays
// silence without one), so pass it the first SoundFont from that variable.
static void set_midi_properties(SDL_PropertiesID props)
{
#ifndef MIX_PROP_DECODER_FLUIDSYNTH_SOUNDFONT_PATH_STRING
#define MIX_PROP_DECODER_FLUIDSYNTH_SOUNDFONT_PATH_STRING "SDL_mixer.decoder.fluidsynth.soundfont_path"
#endif
#ifdef _WIN32
	const char *separators = ";";
#else
	const char *separators = ":;";
#endif
	const char *env = SDL_getenv("SDL_SOUNDFONTS");
	char *paths, *path, *state = NULL;

	if (!env || !*env)
		return;

	paths = SDL_strdup(env);
	if (!paths)
		return;

	for (path = SDL_strtok_r(paths, separators, &state); path; path = SDL_strtok_r(NULL, separators, &state))
	{
		if (SDL_GetPathInfo(path, NULL))
		{
			SDL_SetStringProperty(props, MIX_PROP_DECODER_FLUIDSYNTH_SOUNDFONT_PATH_STRING, path);
			break;
		}
	}

	SDL_free(paths);
}

/// ------------------------
/// Music Hooks
/// ------------------------

static Uint32 SDLCALL music_fade(void *param, SDL_TimerID timerid, Uint32 interval)
{
	(void)param;
	(void)timerid;

	if (!is_fading ||
		internal_volume == fading_target ||
		fading_duration == 0)
	{
		I_StopFadingSong();
		do_fading_callback();
		return 0;
	}
	else if (songpaused) // don't decrement timer
		return interval;
	else if ((fading_timer -= 10) <= 0)
	{
		internal_volume = fading_target;
		update_music_volume();
		I_StopFadingSong();
		do_fading_callback();
		return 0;
	}
	else
	{
		UINT8 delta = abs(fading_target - fading_source);
		fixed_t factor = FixedDiv(fading_duration - fading_timer, fading_duration);
		if (fading_target < fading_source)
			internal_volume = max(min(internal_volume, fading_source - FixedMul(delta, factor)), fading_target);
		else if (fading_target > fading_source)
			internal_volume = min(max(internal_volume, fading_source + FixedMul(delta, factor)), fading_target);
		update_music_volume();
		return interval;
	}
}

#ifdef HAVE_GME
static void mix_gme(Uint8 *stream, int len)
{
	int i;
	short *p;

	// no gme? no music.
	if (!gme || gme_track_ended(gme) || songpaused)
		return;

	// play gme into stream
	gme_play(gme, len/2, (short *)stream);

	// Limiter to prevent music from being disorted with some formats
	if (music_volume >= 18)
		music_volume = 18;

	// apply volume to stream
	for (i = 0, p = (short *)stream; i < len / 2; i++, p++)
		*p = ((INT32)*p) * music_volume * internal_volume / 100 / 20;
}
#endif

#ifdef HAVE_OPENMPT
static void mix_openmpt(Uint8 *stream, int len)
{
	int i;
	short *p;

	if (!openmpt_mhandle || songpaused)
		return;

	// Play module into stream
	openmpt_module_read_interleaved_stereo(openmpt_mhandle, SAMPLERATE, len / 4, (short *)stream);

	// Limiter to prevent music from being disorted with some formats
	if (music_volume >= 18)
		music_volume = 18;

	// apply volume to stream
	for (i = 0, p = (short *)stream; i < len / 2; i++, p++)
		*p = ((INT32)*p) * music_volume * internal_volume / 100 / 20;
}
#endif

#if defined (HAVE_GME) || defined (HAVE_OPENMPT)
// SDL_mixer pulls GME and OpenMPT music through an audio stream,
// which asks for more samples with this.
static void SDLCALL music_stream_callback(void *udata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	static INT16 buffer[2048 * 2]; // only the mixer thread gets here

	(void)udata;
	(void)total_amount;

	while (additional_amount > 0)
	{
		// whole sample frames only: 16-bit stereo
		int len = min((additional_amount + 3) & ~3, (int)sizeof(buffer));

		memset(buffer, 0, len);
#ifdef HAVE_GME
		if (gme)
			mix_gme((Uint8 *)buffer, len);
		else
#endif
#ifdef HAVE_OPENMPT
		if (openmpt_mhandle)
			mix_openmpt((Uint8 *)buffer, len);
#endif
		{}

		if (!SDL_PutAudioStreamData(stream, buffer, len))
			break;
		additional_amount -= len;
	}
}

static boolean play_music_stream(void)
{
	SDL_PropertiesID props;
	boolean played;

	if (!music_track)
		return false;

	if (music_stream) // already playing, start over
	{
		MIX_SetTrackAudioStream(music_track, NULL);
		SDL_DestroyAudioStream(music_stream);
	}

	music_stream = SDL_CreateAudioStream(&mixer_spec, &mixer_spec);
	if (!music_stream)
	{
		CONS_Alert(CONS_ERROR, "SDL_CreateAudioStream: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetAudioStreamGetCallback(music_stream, music_stream_callback, NULL);

	// The stream never runs out, it just goes silent when the song is over
	props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false);
	played = MIX_SetTrackAudioStream(music_track, music_stream) && MIX_PlayTrack(music_track, props);
	SDL_DestroyProperties(props);

	if (!played)
	{
		CONS_Alert(CONS_ERROR, "MIX_PlayTrack: %s\n", SDL_GetError());
		MIX_SetTrackAudioStream(music_track, NULL);
		SDL_DestroyAudioStream(music_stream);
		music_stream = NULL;
		return false;
	}

	update_music_volume();
	return true;
}
#endif

/// ------------------------
/// Music System
/// ------------------------

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
	I_UnloadSong();
}

/// ------------------------
/// Music Properties
/// ------------------------

musictype_t I_SongType(void)
{
#ifdef HAVE_GME
	if (gme)
		return MU_GME;
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		return MU_MOD_EX;
#endif
	if (!music)
		return MU_NONE;
	else
		return music_type;
}

boolean I_SongPlaying(void)
{
	return (
#ifdef HAVE_GME
		(I_SongType() == MU_GME && gme) ||
#endif
#ifdef HAVE_OPENMPT
		(I_SongType() == MU_MOD_EX && openmpt_mhandle) ||
#endif
		music != NULL
	);
}

boolean I_SongPaused(void)
{
	return songpaused;
}

/// ------------------------
/// Music Effects
/// ------------------------

boolean I_SetSongSpeed(float speed)
{
	if (speed > 250.0f)
		speed = 250.0f; //limit speed up to 250x
#ifdef HAVE_GME
	if (gme)
	{
		MIX_LockMixer(mixer);
		gme_set_tempo(gme, speed);
		MIX_UnlockMixer(mixer);
		return true;
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		if (speed > 4.0f)
			speed = 4.0f; // Limit this to 4x to prevent crashing, stupid fix but... ~SteelT 27/9/19
#if OPENMPT_API_VERSION_MAJOR < 1 && OPENMPT_API_VERSION_MINOR < 5
		{
			// deprecated in 0.5.0
			char modspd[13];
			sprintf(modspd, "%g", speed);
			openmpt_module_ctl_set(openmpt_mhandle, "play.tempo_factor", modspd);
		}
#else
		openmpt_module_ctl_set_floatingpoint(openmpt_mhandle, "play.tempo_factor", (double)speed);
#endif
		return true;
	}
#else
	(void)speed;
	return false;
#endif
	return false;
}

/// ------------------------
///  MUSIC SEEKING
/// ------------------------

UINT32 I_GetSongLength(void)
{
	INT32 length;

#ifdef HAVE_GME
	if (gme)
	{
		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
			length = 0;
		}
		else
		{
			// reconstruct info->play_length, from GME source
			// we only want intro + 1 loop, not 2
			length = info->length;
			if (length <= 0)
			{
				length = info->intro_length + info->loop_length; // intro + 1 loop
				if (length <= 0)
					length = 150 * 1000; // 2.5 minutes
			}
		}

		gme_free_info(info);
		return max(length, 0);
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		return (UINT32)(openmpt_module_get_duration_seconds(openmpt_mhandle) * 1000.);
	else
#endif
	if (!music || I_SongType() == MU_MOD || I_SongType() == MU_MID)
		return 0;
	else
	{
		Sint64 frames = MIX_GetAudioDuration(music);
		if (frames <= 0) // unknown or infinite
		{
			CONS_Debug(DBG_DETAILED, "Getting music length: SDL_mixer can't tell the length of this song. Needed for seeking.\n");
			return 0;
		}
		return music_frames_to_ms(frames);
	}
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	if (!music || I_SongType() == MU_GME || I_SongType() == MU_MOD || I_SongType() == MU_MID || !is_looping)
		return false;
	else
	{
		UINT32 length = I_GetSongLength();

		if (length > 0)
			looppoint %= length;

		loop_point = music_ms_to_frames(looppoint);

		// SDL_mixer takes the loop point when playback starts,
		// so carry on from where the song is at with the new one.
		if (MIX_TrackPlaying(music_track) || MIX_TrackPaused(music_track))
		{
			play_music_track(MIX_GetTrackPlaybackPosition(music_track));
			if (songpaused)
				MIX_PauseTrack(music_track);
		}
		return true;
	}
}

UINT32 I_GetSongLoopPoint(void)
{
#ifdef HAVE_GME
	if (gme)
	{
		INT32 looppoint;
		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
			looppoint = 0;
		}
		else
			looppoint = info->intro_length > 0 ? info->intro_length : 0;

		gme_free_info(info);
		return max(looppoint, 0);
	}
	else
#endif
	if (!music || I_SongType() == MU_MOD || I_SongType() == MU_MID)
		return 0;
	else
		return music_frames_to_ms(loop_point);
}

boolean I_SetSongPosition(UINT32 position)
{
	UINT32 length;
#ifdef HAVE_GME
	if (gme)
	{
		// this is unstable, so fail silently
		return true;
		// this isn't required technically, but GME thread-locks for a second
		// if you seek too high from the counter
		// length = I_GetSongLength();
		// if (length)
		// 	position = get_adjusted_position(position);

		// MIX_LockMixer(mixer);
		// gme_err_t gme_e = gme_seek(gme, position);
		// MIX_UnlockMixer(mixer);

		// if (gme_e != NULL)
		// {
		// 	CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
		// 	return false;
		// }
		// else
		// 	return true;
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		// This isn't 100% correct because we don't account for loop points because we can't get them.
		// But if you seek past end of song, OpenMPT seeks to 0. So adjust the position anyway.
		openmpt_module_set_position_seconds(openmpt_mhandle, (double)(get_adjusted_position(position)/1000.0L)); // returns new position
		return true;
	}
	else
#endif
	if (!music || I_SongType() == MU_MID)
		return false;
	else if (I_SongType() == MU_MOD)
	{
		// Goes by orders, and SDL_mixer can only pick one when playback starts
		SDL_PropertiesID props = SDL_CreateProperties();
		boolean played;

		SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, is_looping ? -1 : 0);
		SDL_SetNumberProperty(props, MIX_PROP_PLAY_START_ORDER_NUMBER, position);
		played = MIX_PlayTrack(music_track, props);
		SDL_DestroyProperties(props);

		if (played && songpaused)
			MIX_PauseTrack(music_track);
		return played;
	}
	else
	{
		length = I_GetSongLength(); // get it in MS
		if (length)
			position = get_adjusted_position(position);

		// NOTE: This fails on song formats that can't seek,
		// NOT if position input is greater than song length.
		MIX_SetTrackPlaybackPosition(music_track, music_ms_to_frames(position));

		return true;
	}
}

UINT32 I_GetSongPosition(void)
{
#ifdef HAVE_GME
	if (gme)
	{
		INT32 position = gme_tell(gme);

		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
			return position;
		}
		else
		{
			// adjust position, since GME's counter keeps going past loop
			if (info->length > 0)
				position %= info->length;
			else if (info->intro_length + info->loop_length > 0)
				position = position >= (info->intro_length + info->loop_length) ? (position % info->loop_length) : position;
			else
				position %= 150 * 1000; // 2.5 minutes
		}

		gme_free_info(info);
		return max(position, 0);
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		// This will be incorrect if we adjust for length because we can't get loop points.
		// So return unadjusted. See note in SetMusicPosition: we adjust for that.
		return (UINT32)(openmpt_module_get_position_seconds(openmpt_mhandle)*1000.);
		//return get_adjusted_position((UINT32)(openmpt_module_get_position_seconds(openmpt_mhandle)*1000.));
	else
#endif
	if (!music || I_SongType() == MU_MID)
		return 0;
	else if (!MIX_TrackPlaying(music_track) && !MIX_TrackPaused(music_track))
		return 0; // the song is over, or was never started
	else
		return music_frames_to_ms(MIX_GetTrackPlaybackPosition(music_track));
}

/// ------------------------
/// Music Playback
/// ------------------------

boolean I_LoadSong(char *data, size_t len)
{
	const char *key1 = "LOOP";
	const char *key2 = "POINT=";
	const char *key3 = "MS=";
	const size_t key1len = strlen(key1);
	const size_t key2len = strlen(key2);
	const size_t key3len = strlen(key3);

	char *p = data;
	SDL_IOStream *rw;
	SDL_PropertiesID props;
	SDL_AudioSpec songspec;

	if (music
#ifdef HAVE_GME
		|| gme
#endif
#ifdef HAVE_OPENMPT
		|| openmpt_mhandle
#endif
	)
		I_UnloadSong();

	// always do this whether or not a music already exists
	var_cleanup();

#ifdef HAVE_GME
	if ((UINT8)data[0] == 0x1F
		&& (UINT8)data[1] == 0x8B)
	{
#ifdef HAVE_ZLIB
		UINT8 *inflatedData;
		size_t inflatedLen;
		z_stream stream;
		int zErr; // Somewhere to handle any error messages zlib tosses out

		memset(&stream, 0x00, sizeof (z_stream)); // Init zlib stream
		// Begin the inflation process
		inflatedLen = *(UINT32 *)(data + (len-4)); // Last 4 bytes are the decompressed size, typically
		inflatedData = (UINT8 *)Z_Calloc(inflatedLen, PU_MUSIC, NULL); // Make room for the decompressed data
		stream.total_in = stream.avail_in = len;
		stream.total_out = stream.avail_out = inflatedLen;
		stream.next_in = (UINT8 *)data;
		stream.next_out = inflatedData;

		zErr = inflateInit2(&stream, 32 + MAX_WBITS);
		if (zErr == Z_OK) // We're good to go
		{
			zErr = inflate(&stream, Z_FINISH);
			if (zErr == Z_STREAM_END)
			{
				// Run GME on new data
				if (!gme_open_data(inflatedData, inflatedLen, &gme, 44100))
				{
					Z_Free(inflatedData); // GME supposedly makes a copy for itself, so we don't need this lying around
					return true;
				}
			}
			else
				CONS_Alert(CONS_ERROR, "Encountered %s when running inflate: %s\n", get_zlib_error(zErr), stream.msg);
			(void)inflateEnd(&stream);
		}
		else // Hold up, zlib's got a problem
			CONS_Alert(CONS_ERROR, "Encountered %s when running inflateInit: %s\n", get_zlib_error(zErr), stream.msg);
		Z_Free(inflatedData); // GME didn't open jack, but don't let that stop us from freeing this up
		return false;
#else
		CONS_Alert(CONS_ERROR, "Cannot decompress VGZ; no zlib support\n");
		return false;
#endif
	}
	else if (!gme_open_data(data, len, &gme, 44100))
		return true;
#endif

#ifdef HAVE_OPENMPT
	size_t probesize;
	int result;

	if (len > openmpt_probe_file_header_get_recommended_size())
		probesize = openmpt_probe_file_header_get_recommended_size();
	else
		probesize = len;

	result = openmpt_probe_file_header(OPENMPT_PROBE_FILE_HEADER_FLAGS_DEFAULT, data, probesize, len, NULL, NULL, NULL, NULL, NULL, NULL);

	if (result == OPENMPT_PROBE_FILE_HEADER_RESULT_SUCCESS) // We only cared if it succeeded, continue on if not.
	{
		openmpt_mhandle = openmpt_module_create_from_memory2(data, len, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		if (!openmpt_mhandle)
		{
			mod_err = openmpt_module_error_get_last(openmpt_mhandle);
			mod_err_str = openmpt_error_string(mod_err);
			CONS_Alert(CONS_ERROR, "openmpt_module_create_from_memory2: %s\n", mod_err_str);
			return false;
		}
		else
			return true;
	}
#endif

	if (!mixer)
		return false;

	music_type = detect_music_type((const UINT8 *)data, len);

	rw = SDL_IOFromConstMem(data, len);
	if (rw != NULL)
	{
		// Looping is up to the game (see I_PlaySong), not to tags in the song
		props = SDL_CreateProperties();
		SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, rw);
		SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, true);
		SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER, mixer);
		SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_IGNORE_LOOPS_BOOLEAN, true);
		if (music_type == MU_MID)
			set_midi_properties(props);
		music = MIX_LoadAudioWithProperties(props);
		SDL_DestroyProperties(props);
	}
	if (!music)
	{
		CONS_Alert(CONS_ERROR, "MIX_LoadAudio: %s\n", SDL_GetError());
		music_type = MU_NONE;
		return false;
	}

	if (!MIX_GetAudioFormat(music, &songspec))
		songspec.freq = SAMPLERATE;

	// Find the OGG loop point.
	loop_point = 0;

	while ((UINT32)(p - data) < len)
	{
		if (loop_point == 0 && !strncmp(p, key1, key1len))
		{
			p += key1len; // skip LOOP
			if (!strncmp(p, key2, key2len)) // is it LOOPPOINT=?
			{
				p += key2len; // skip POINT=
				// LOOPPOINT works by sample count, but it has always been read as if ALL MUSIC is 44100hz.
				// Songs out there have their loop points set with that in mind, so keep reading it
				// like that and convert the time to the song's actual sample frames.
				loop_point = (Sint64)(((44.1L+atoi(p)) / 44100.0L) * songspec.freq);
			}
			else if (!strncmp(p, key3, key3len)) // is it LOOPMS=?
			{
				p += key3len; // skip MS=
				loop_point = music_ms_to_frames(max(atoi(p), 0)); // LOOPMS works by real time, as miliseconds.
				// Everything that uses LOOPMS will work perfectly with SDL_Mixer.
			}
		}

		if (loop_point != 0) // Got what we needed
			break;
		else // continue searching
			p++;
	}
	return true;
}

void I_UnloadSong(void)
{
	I_StopSong();

#ifdef HAVE_GME
	if (gme)
	{
		gme_delete(gme);
		gme = NULL;
	}
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		openmpt_module_destroy(openmpt_mhandle);
		openmpt_mhandle = NULL;
	}
#endif
	if (music)
	{
		MIX_DestroyAudio(music);
		music = NULL;
	}
	music_type = MU_NONE;
}

boolean I_PlaySong(boolean looping)
{
#ifdef HAVE_GME
	if (gme)
	{
		gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};
#if defined (GME_VERSION) && GME_VERSION >= 0x000603
		if (looping)
			gme_set_autoload_playback_limit(gme, 0);
#endif
		gme_set_equalizer(gme, &eq);
		gme_start_track(gme, 0);
		current_track = 0;
		return play_music_stream();
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		openmpt_module_select_subsong(openmpt_mhandle, 0);
		openmpt_module_set_render_param(openmpt_mhandle, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, cv_modfilter.value);
		if (looping)
			openmpt_module_set_repeat_count(openmpt_mhandle, -1); // Always repeat
		current_subsong = 0;
		return play_music_stream();
	}
	else
#endif
	if (!music)
		return false;

	is_looping = looping;

	if (!MIX_SetTrackAudio(music_track, music) || !play_music_track(0))
	{
		CONS_Alert(CONS_ERROR, "MIX_PlayTrack: %s\n", SDL_GetError());
		is_looping = false;
		return false;
	}

	I_SetMusicVolume(music_volume);

	return true;
}

void I_StopSong(void)
{
	I_StopFadingSong();

#ifdef HAVE_GME
	if (gme)
		current_track = -1;
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		current_subsong = -1;
#endif
	if (music_track)
	{
		MIX_StopTrack(music_track, 0);

		if (music_stream)
		{
			// Once the track has let go of the stream, nothing calls into GME or OpenMPT anymore
			MIX_SetTrackAudioStream(music_track, NULL);
			SDL_DestroyAudioStream(music_stream);
			music_stream = NULL;
		}
	}

	var_cleanup();
}

void I_PauseSong(void)
{
	if (music_track)
		MIX_PauseTrack(music_track);
	songpaused = true;
}

void I_ResumeSong(void)
{
	if (music_track)
		MIX_ResumeTrack(music_track);
	songpaused = false;
}

void I_SetMusicVolume(UINT8 volume)
{
	if (!I_SongPlaying())
		return;

	music_volume = volume;

	update_music_volume();
}

boolean I_SetSongTrack(int track)
{
#ifdef HAVE_GME
	// If the specified track is within the number of tracks playing, then change it
	if (gme)
	{
		if (current_track == track)
			return false;
		MIX_LockMixer(mixer);
		if (track >= 0 && track < gme_track_count(gme)-1)
		{
			gme_err_t gme_e = gme_start_track(gme, track);
			if (gme_e != NULL)
			{
				MIX_UnlockMixer(mixer);
				CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
				return false;
			}
			current_track = track;
			MIX_UnlockMixer(mixer);
			return true;
		}
		MIX_UnlockMixer(mixer);
		return false;
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		if (current_subsong == track)
			return false;
		MIX_LockMixer(mixer);
		if (track >= 0 && track < openmpt_module_get_num_subsongs(openmpt_mhandle))
		{
			openmpt_module_select_subsong(openmpt_mhandle, track);
			current_subsong = track;
			MIX_UnlockMixer(mixer);
			return true;
		}
		MIX_UnlockMixer(mixer);

		return false;
	}
#endif
	(void)track;
	return false;
}

/// ------------------------
/// MUSIC FADING
/// ------------------------

void I_SetInternalMusicVolume(UINT8 volume)
{
	internal_volume = volume;
	if (!I_SongPlaying())
		return;
	update_music_volume();
}

void I_StopFadingSong(void)
{
	if (fading_id)
		SDL_RemoveTimer(fading_id);
	is_fading = false;
	fading_source = fading_target = fading_timer = fading_duration = fading_id = 0;
	// fading_callback is cleaned up in var_cleanup()
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	INT16 volume_delta;

	source_volume = min(source_volume, 100);
	volume_delta = (INT16)(target_volume - source_volume);

	I_StopFadingSong();

	if ((!ms && volume_delta) || M_CheckParm("-nomusicfades"))
	{
		I_SetInternalMusicVolume(target_volume);
		if (callback)
			(*callback)();
		return true;
	}
	else if (!volume_delta)
	{
		if (callback)
			(*callback)();
		return true;
	}

	// Round MS to nearest 10
	// If n - lower > higher - n, then round up
	ms = (ms - ((ms / 10) * 10) > (((ms / 10) * 10) + 10) - ms) ?
		(((ms / 10) * 10) + 10) // higher
		: ((ms / 10) * 10); // lower

	if (!ms)
		I_SetInternalMusicVolume(target_volume);
	else if (source_volume != target_volume)
	{
		fading_id = SDL_AddTimer(10, music_fade, NULL);
		if (fading_id)
		{
			is_fading = true;
			fading_timer = fading_duration = ms;
			fading_source = source_volume;
			fading_target = target_volume;
			fading_callback = callback;

			if (internal_volume != source_volume)
				I_SetInternalMusicVolume(source_volume);
		}
	}

	return is_fading;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	return I_FadeSongFromVolume(target_volume, internal_volume, ms, callback);
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	return I_FadeSongFromVolume(0, internal_volume, ms, &I_StopSong);
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
	if (I_PlaySong(looping))
		return I_FadeSongFromVolume(100, 0, ms, NULL);
	else
		return false;
}
#endif
