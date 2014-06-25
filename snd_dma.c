/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

    $Id: snd_dma.c,v 1.48 2007-09-26 21:51:34 tonik Exp $
*/
// snd_dma.c -- main control for any streaming sound output device

#include "quakedef.h"
#include "qsound.h"
#include "utils.h"
#define SELF_SOUND 0xFFEFFFFF // [EZH] Fan told me 0xFFEFFFFF is damn cool value for it :P

#ifdef _WIN32
#include "movie.h" //joe: capturing audio
#endif

static void OnChange_s_khz (cvar_t *var, char *string, qbool *cancel);
static void S_Play_f (void);
static void S_PlayVol_f (void);
static void S_MuteSound_f (void);
static void S_SoundList_f (void);
static void S_Update_ ();
static void S_StopAllSounds_f (void);
static void S_Register_LatchCvars(void);

void S_RawClear(void);
void S_RawAudio(int sourceid, byte *data, unsigned int speed, unsigned int samples, unsigned int channelsnum, unsigned int width);

// =======================================================================
// Internal sound data & structures
// =======================================================================

channel_t	channels[MAX_CHANNELS];
unsigned int	total_channels;

int		snd_blocked = 0;

qbool		snd_initialized = false;
qbool		snd_started = false;

int		soundtime;
int		paintedtime;

volatile dma_t	sn;
volatile dma_t	*shm = NULL;


static vec3_t	listener_origin;
static vec3_t	listener_forward;
static vec3_t	listener_right;
static vec3_t	listener_up;
#define sound_nominal_clip_dist 1000.0

// during registration it is possible to have more sounds
// than could actually be referenced during gameplay,
// because we don't want to free anything until we are
// sure we won't need it.

#define MAX_SFX (MAX_SOUNDS*2) // 256 * 2 = 512

static sfx_t	*known_sfx = NULL; // hunk allocated [MAX_SFX]
static int	num_sfx;

static qbool	sound_spatialized = false;

static sfx_t	*ambient_sfx[NUM_AMBIENTS];

// ====================================================================
// User-setable variables
// ====================================================================


cvar_t bgmvolume = {"bgmvolume", "1"}; // CD music volume
cvar_t s_volume = {"volume", "0.7", CVAR_ARCHIVE};
cvar_t s_raw_volume = {"s_raw_volume", "1"};
cvar_t s_nosound = {"s_nosound", "0"};
cvar_t s_precache = {"s_precache", "1"};
cvar_t s_loadas8bit = {"s_loadas8bit", "0"};
cvar_t s_ambientlevel = {"s_ambientlevel", "0.3"};
cvar_t s_ambientfade = {"s_ambientfade", "100"};
cvar_t s_noextraupdate = {"s_noextraupdate", "0"};
cvar_t s_show = {"s_show", "0"};
cvar_t s_mixahead = {"s_mixahead", "0.1"};
cvar_t s_swapstereo = {"s_swapstereo", "0"};
cvar_t s_linearresample = {"s_linearresample", "0", CVAR_LATCH};
cvar_t s_linearresample_stream = {"s_linearresample_stream", "0"};
cvar_t s_khz = {"s_khz", "11", CVAR_NONE, OnChange_s_khz}; // If > 11, default sounds are noticeably different.

static void S_SoundInfo_f (void)
{
	if (!shm) {
		Com_Printf ("sound system not started\n");
		return;
	}
	Com_Printf("%5d speakers\n", shm->format.channels);
#if defined(__linux__) || defined(__FreeBSD__)
	// shm->sampleframes not set/used in ALSA/Pulseaudio
	// OSS(legacy) still sets it, but it isn't used outside of OSS(legacy).
	// So don't read sampleframes from shm, but calculate it instead.
	Com_Printf("%5d frames\n", shm->samples / shm->format.channels); 
#else
	Com_Printf("%5d frames\n", shm->sampleframes);
#endif
	Com_Printf("%5d samples\n", shm->samples);
	Com_Printf("%5d samplepos\n", shm->samplepos);
	Com_Printf("%5d samplebits\n", shm->format.width * 8);
	Com_Printf("%5d speed\n", shm->format.speed);
	Com_Printf("%p dma buffer\n", shm->buffer);
	Com_Printf("%5u total_channels\n", total_channels);
}


static qbool S_Startup (void)
{
	if (!snd_initialized)
		return false;

	shm = &sn;
	memset((void *)shm, 0, sizeof(*shm));

	S_RawClear();
	S_Register_LatchCvars();

///////////////////////////////////////////////
// Sound driver choosing. Linux/FreeBSD only

	if (!SNDDMA_Init()) {
		Com_Printf ("S_Startup: SNDDMA_Init failed.\n");
		shm = NULL;
		snd_started = false;
		sound_spatialized = false;
		return false;
	}

	ambient_sfx[AMBIENT_WATER] = S_PrecacheSound ("ambience/water1.wav");
	ambient_sfx[AMBIENT_SKY] = S_PrecacheSound ("ambience/wind2.wav");
	S_StopAllSounds (true);

	snd_started = true;
	return true;
}

void S_Shutdown (void)
{
	if (!shm)
		return;

	Cache_Flush(); // dimman: Moved this line and next here from S_Restart_f
	S_StopAllSounds (true);

	SNDDMA_Shutdown();

	shm = NULL;
	snd_started = false;
	sound_spatialized = false;
}

static void S_Restart_f (void)
{
	Com_DPrintf("Restarting sound system....\n");
	S_Shutdown();
	S_Startup();
}

static void OnChange_s_khz (cvar_t *var, char *string, qbool *cancel) {
	if (shm && shm->format.speed / 1000 != Q_atoi(string)) {
		Cbuf_AddText("s_restart\n");
	}
}

static void S_Register_RegularCvarsAndCommands(void)
{
	Cvar_SetCurrentGroup(CVAR_GROUP_SOUND);
	Cvar_Register(&bgmvolume);
	Cvar_Register(&s_volume);
	Cvar_Register(&s_raw_volume);
	Cvar_Register(&s_nosound);
	Cvar_Register(&s_precache);
	Cvar_Register(&s_loadas8bit);
	Cvar_Register(&s_khz);
	Cvar_Register(&s_ambientlevel);
	Cvar_Register(&s_ambientfade);
	Cvar_Register(&s_noextraupdate);
	Cvar_Register(&s_show);
	Cvar_Register(&s_mixahead);
	Cvar_Register(&s_swapstereo);
	Cvar_Register(&s_linearresample_stream);

	Cvar_ResetCurrentGroup();

	// compatibility with old configs
	Cmd_AddLegacyCommand("nosound", "s_nosound");
	Cmd_AddLegacyCommand("precache", "s_precache");
	Cmd_AddLegacyCommand("loadas8bit", "s_loadas8bit");
	Cmd_AddLegacyCommand("ambient_level", "s_ambientlevel");
	Cmd_AddLegacyCommand("ambient_fade", "s_ambientfade");
	Cmd_AddLegacyCommand("snd_noextraupdate", "s_noextraupdate");
	Cmd_AddLegacyCommand("snd_show", "s_show");
	Cmd_AddLegacyCommand("_snd_mixahead", "s_mixahead");
	Cmd_AddLegacyCommand("snd_restart", "s_restart"); // and snd_restart a legacy command

	Cmd_AddCommand("s_restart", S_Restart_f); // dimman: made s_restart the actual command
	Cmd_AddCommand("mutesound", S_MuteSound_f);
	Cmd_AddCommand("play", S_Play_f);
	Cmd_AddCommand("playvol", S_PlayVol_f);
	Cmd_AddCommand("stopsound", S_StopAllSounds_f);
	Cmd_AddCommand("soundlist", S_SoundList_f);
	Cmd_AddCommand("soundinfo", S_SoundInfo_f);
}

static void S_Register_LatchCvars(void)
{
	Cvar_SetCurrentGroup(CVAR_GROUP_SOUND);

	Cvar_Register(&s_linearresample);

	Cvar_ResetCurrentGroup();
}

void S_Init (void)
{
	Com_DPrintf("\n[sound] Initialization\n");
	if (snd_initialized) { //whoops
		Com_Printf_State (PRINT_INFO, "[sound] Sound is already initialized!\n");
		return;
	}
	if (COM_CheckParm("-nosound")) {
		Cmd_AddLegacyCommand ("play", ""); // just suppress warnings
		return;
	}	
	if (host_memsize < 0x800000) {
		Cvar_Set (&s_loadas8bit, "1");
		Com_Printf ("[sound] Not enough memory. Loading all sounds as 8bit\n");
	}

	S_Register_RegularCvarsAndCommands();
	S_Register_LatchCvars();
	SND_InitScaletable ();

	known_sfx = (sfx_t *) Hunk_AllocName (MAX_SFX * sizeof(sfx_t), "sfx_t");
	num_sfx = 0;

	snd_initialized = true;

	S_Startup();
}

// =======================================================================
// Load a sound
// =======================================================================

static sfx_t *S_FindName (char *name)
{
	int i;
	sfx_t *sfx;

	if (!snd_initialized || !snd_started)
		return NULL;

	if (!name)
		Sys_Error ("S_FindName: NULL");

	if (strlen(name) >= MAX_QPATH) {
		Com_Printf ("S_FindName: sound name too long (%s)\n", name);
		return NULL;
	}

	// see if already loaded
	for (i = 0; i < num_sfx; i++) {
		if (!strcmp(known_sfx[i].name, name))
			return &known_sfx[i];
	}

	if (num_sfx == MAX_SFX)
		Sys_Error ("S_FindName: out of sfx_t");

	sfx = &known_sfx[i];
	strlcpy (sfx->name, name, sizeof (sfx->name));

	num_sfx++;

	return sfx;
}

sfx_t *S_PrecacheSound (char *name)
{
	sfx_t *sfx;
	if (!snd_initialized || !snd_started || s_nosound.value)
		return NULL;

	if (name == NULL || name[0] == 0)
		return NULL;

	sfx = S_FindName (name);
	if (sfx == NULL)
		return NULL;

	// cache it in
	if (s_precache.value)
		S_LoadSound (sfx);

	return sfx;
}

//=============================================================================

// picks a channel based on priorities, empty slots, number of channels
static channel_t *SND_PickChannel (int entnum, int entchannel)
{
	int ch_idx, first_to_die, life_left;

	// Check for replacement sound, or find the best one to replace
	first_to_die = -1;
	life_left = 0x7fffffff;
	for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++) {
		if (entchannel != 0		// channel 0 never overrides
		        && channels[ch_idx].entnum == entnum
		        && (channels[ch_idx].entchannel == entchannel || entchannel == -1) ) {
			// always override sound from same entity
			first_to_die = ch_idx;
			break;
		}

		// don't let monster sounds override player sounds
		if (channels[ch_idx].entnum == cl.playernum+1 && entnum != cl.playernum+1 && channels[ch_idx].sfx)
			continue;

		if (channels[ch_idx].end - paintedtime < life_left) {
			life_left = channels[ch_idx].end - paintedtime;
			first_to_die = ch_idx;
		}
	}

	if (first_to_die == -1)
		return NULL;

	if (channels[first_to_die].sfx)
		channels[first_to_die].sfx = NULL;

	return &channels[first_to_die];
}

// spatializes a channel
static void SND_Spatialize (channel_t *ch)
{
	vec_t dot, dist, lscale, rscale, scale;
	vec3_t source_vec;

	// anything coming from the view entity will always be full volume
	if ((ch->entnum == cl.playernum + 1) || (ch->entnum == SELF_SOUND)) {
		ch->leftvol = ch->master_vol;
		ch->rightvol = ch->master_vol;
		return;
	}

	// calculate stereo seperation and distance attenuation
	VectorSubtract(ch->origin, listener_origin, source_vec);

	dist = VectorNormalize(source_vec) * ch->dist_mult;
	dot = DotProduct(listener_right, source_vec);

	if (shm->format.channels == 1) {
		rscale = 1.0;
		lscale = 1.0;
	} else {
		rscale = 1.0 + dot;
		lscale = 1.0 - dot;
	}

	// add in distance effect
	scale = (1.0 - dist) * rscale;
	ch->rightvol = (int) (ch->master_vol * scale);
	if (ch->rightvol < 0)
		ch->rightvol = 0;

	scale = (1.0 - dist) * lscale;
	ch->leftvol = (int) (ch->master_vol * scale);
	if (ch->leftvol < 0)
		ch->leftvol = 0;
}

// =======================================================================
// Start a sound effect
// =======================================================================

void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
	channel_t *target_chan, *check;
	sfxcache_t *sc;
	int ch_idx, skip;

	if (!shm || !sfx || s_nosound.value)
		return;

	// pick a channel to play on
	target_chan = SND_PickChannel(entnum, entchannel);
	if (!target_chan)
		return;

	// spatialize
	memset (target_chan, 0, sizeof(*target_chan));
	VectorCopy(origin, target_chan->origin);
	target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
	target_chan->master_vol = (int) (fvol * 255);
	target_chan->entnum = entnum;
	target_chan->entchannel = entchannel;
	SND_Spatialize(target_chan);

	if (!target_chan->leftvol && !target_chan->rightvol)
		return; // not audible at all

	// new channel
	sc = S_LoadSound (sfx);
	if (!sc) {
		target_chan->sfx = NULL;
		return; // couldn't load the sound's data
	}

	target_chan->sfx = sfx;
	target_chan->pos = 0.0;
	target_chan->end = paintedtime + (int) sc->total_length;

	// if an identical sound has also been started this frame, offset the pos
	// a bit to keep it from just making the first one louder
	check = &channels[NUM_AMBIENTS];
	for (ch_idx=NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++, check++) {
		if (check == target_chan)
			continue;
		if (check->sfx == sfx && !check->pos) {
			skip = rand () % (int)(0.1 * shm->format.speed);
			if (skip >= target_chan->end)
				skip = target_chan->end - 1;
			target_chan->pos += skip;
			target_chan->end -= skip;
			break;
		}
	}
}

void S_StopSound (int entnum, int entchannel)
{
	unsigned int i;

	for (i = 0; i < MAX_DYNAMIC_CHANNELS; i++) {
		if (channels[i].entnum == entnum && channels[i].entchannel == entchannel) {
			channels[i].end = 0;
			channels[i].sfx = NULL;
			return;
		}
	}
}

void S_StopAllSounds (qbool clear)
{
	int i;

	if (!shm)
		return;

	total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; // no statics

	for (i = 0; i < MAX_CHANNELS; i++) {
		if (channels[i].sfx)
			channels[i].sfx = NULL;
	}

	memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));

	if (clear)
		S_ClearBuffer ();
}

static void S_StopAllSounds_f (void)
{
	S_StopAllSounds (true);
}


void S_ClearBuffer (void)
{
	int clear;

	if (!shm || !shm->buffer)
		return;

	clear = (shm->format.width == 2) ? 0x80 : 0;
	memset(shm->buffer, clear, shm->bufferlength);
}

void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
	channel_t *ss;
	sfxcache_t *sc;

	if (!shm || !sfx || s_nosound.value)
		return;

	if (total_channels == MAX_CHANNELS) {
		Com_Printf ("total_channels == MAX_CHANNELS\n");
		return;
	}

	ss = &channels[total_channels];
	total_channels++;

	sc = S_LoadSound (sfx);
	if (!sc)
		return;

	if (sc->loopstart == -1) {
		Com_Printf ("Sound %s not looped\n", sfx->name);
		return;
	}

	ss->sfx = sfx;
	VectorCopy (origin, ss->origin);
	ss->master_vol = (int) vol;
	ss->dist_mult = (attenuation/64) / sound_nominal_clip_dist;
	ss->end = paintedtime + (int) sc->total_length;

	SND_Spatialize (ss);
}

//=============================================================================

static void S_UpdateAmbientSounds (void)
{
	struct cleaf_s *leaf;
	int vol;
	int ambient_channel;
	channel_t *chan;

	if (cls.state != ca_active)
		return;

	leaf = CM_PointInLeaf (listener_origin);
	if (!CM_Leafnum(leaf) || !s_ambientlevel.value) {
		for (ambient_channel = 0 ; ambient_channel< NUM_AMBIENTS ; ambient_channel++)
			channels[ambient_channel].sfx = NULL;
		return;
	}

	for (ambient_channel = 0 ; ambient_channel< NUM_AMBIENTS ; ambient_channel++) {
		chan = &channels[ambient_channel];
		chan->sfx = ambient_sfx[ambient_channel];

		vol = (int) (s_ambientlevel.value * CM_LeafAmbientLevel(leaf, ambient_channel));
		if (vol < 8)
			vol = 0;

		// don't adjust volume too fast
		if (chan->master_vol < vol) {
			chan->master_vol += Q_rint (cls.frametime * s_ambientfade.value);
			if (chan->master_vol > vol)
				chan->master_vol = vol;
		} else if (chan->master_vol > vol) {
			chan->master_vol -= Q_rint (cls.frametime * s_ambientfade.value);
			if (chan->master_vol < vol)
				chan->master_vol = vol;
		}

		chan->leftvol = chan->rightvol = chan->master_vol;
	}
}

//Called once each time through the main loop
void S_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	unsigned int i, j, total;
	static unsigned int printed_total = 0;
	channel_t *ch, *combine;

	if (!snd_initialized || !snd_started || (snd_blocked > 0) || !shm)
		return;

	VectorCopy(origin, listener_origin);
	VectorCopy(forward, listener_forward);
	VectorCopy(right, listener_right);
	VectorCopy(up, listener_up);

	// update general area ambient sound sources
	S_UpdateAmbientSounds ();

	combine = NULL;

	// update spatialization for static and dynamic sounds
	ch = channels + NUM_AMBIENTS;
	for (i = NUM_AMBIENTS; i < total_channels; i++, ch++) {
		if (!ch->sfx)
			continue;
		SND_Spatialize(ch); // respatialize channel
		if (!ch->leftvol && !ch->rightvol)
			continue;

		// try to combine static sounds with a previous channel of the same
		// sound effect so we don't mix five torches every frame

		if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS) {
			// see if it can just use the last one
			if (combine && combine->sfx == ch->sfx) {
				combine->leftvol += ch->leftvol;
				combine->rightvol += ch->rightvol;
				ch->leftvol = ch->rightvol = 0;
				continue;
			}
			// search for one
			combine = channels+MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
			for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i; j++, combine++)
				if (combine->sfx == ch->sfx)
					break;

			if (j == total_channels) {
				combine = NULL;
			} else {
				if (combine != ch) {
					combine->leftvol += ch->leftvol;
					combine->rightvol += ch->rightvol;
					ch->leftvol = ch->rightvol = 0;
				}
				continue;
			}
		}
	}

	sound_spatialized = true;

	// debugging output
	if (s_show.value) {
		total = 0;
		ch = channels;

		for (i = 0; i < total_channels; i++, ch++)
			if (ch->sfx && (ch->leftvol || ch->rightvol)) {
#if defined(DEBUG) || defined(_DEBUG)
				if (s_show.value == 2)
					Com_Printf ("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name); // s_show 2
#endif
				total++;
			}

		Print_flags[Print_current] |= PR_TR_SKIP;
		
		if (total != printed_total) { // This if statement is needed so we don't get spammed by the message
			Com_Printf ("%i sound(s) playing\n", total); // s_show 1
			printed_total = total;
		}
	}

	// mix some sound
	S_Update_();
}

static void GetSoundtime (void)
{
	int samplepos, fullsamples;
	static int buffers, oldsamplepos;

	//joe: capturing audio
#ifdef _WIN32
	if (Movie_GetSoundtime())
		return;
#endif

	fullsamples = shm->sampleframes;
	samplepos = SNDDMA_GetDMAPos();

	if (samplepos < oldsamplepos) {
		buffers++; // buffer wrapped

		if (paintedtime > 0x40000000) {
			// time to chop things off to avoid 32 bit limits
			buffers = 0;
			paintedtime = fullsamples;
			S_StopAllSounds (true);
		}
	}

	oldsamplepos = samplepos;

	soundtime = buffers * fullsamples + samplepos / shm->format.channels;
}

void S_ExtraUpdate (void)
{

	//joe: capturing audio
#ifdef _WIN32
	if (Movie_IsCapturing() && movie_is_avi)
		return;
#endif

	if (s_noextraupdate.value || !sound_spatialized)
		return; // don't pollute timings

	S_Update_();
}

static void S_Update_ (void)
{
	unsigned int endtime;

	if (!shm || (snd_blocked > 0))
		return;

	// Updates DMA time
	GetSoundtime();

	// check to make sure that we haven't overshot
	if (paintedtime < soundtime) {
		//Com_Printf ("S_Update_ : overflow\n");
		paintedtime = soundtime;
	}


	// mix ahead of current position
	endtime = soundtime + (unsigned int) (s_mixahead.value * shm->format.speed);
	endtime = min(endtime, (unsigned int)(soundtime + shm->sampleframes));

        SNDDMA_BeginPainting ();

	S_PaintChannels (endtime);

	SNDDMA_Submit ();
}

/*
===============================================================================
console functions
===============================================================================
*/

static void S_Play_f (void)
{
	int i;
	char name[256];
	sfx_t *sfx;

	if (!snd_initialized || !snd_started || s_nosound.value)
		return;

	for (i = 1; i < Cmd_Argc(); i++) {
		strlcpy (name, Cmd_Argv(i), sizeof (name));
		COM_DefaultExtension (name, ".wav");
		sfx = S_PrecacheSound(name);
		S_StartSound(SELF_SOUND, 0, sfx, listener_origin, 1.0, 0.0);
	}
}

static void S_PlayVol_f (void)
{
	int i;
	float vol;
	char name[256];
	sfx_t *sfx;

	if (!snd_initialized || !snd_started || s_nosound.value)
		return;

	for (i = 1; i < Cmd_Argc(); i += 2) {
		strlcpy (name, Cmd_Argv(i), sizeof (name));
		COM_DefaultExtension (name, ".wav");
		sfx = S_PrecacheSound(name);
		vol = Q_atof(Cmd_Argv(i + 1));
		// ezhfan:
		// pnum+1 changed to SELF_SOUND to make sound not to disappear
		S_StartSound(SELF_SOUND, 0, sfx, listener_origin, vol, 0.0);
	}
}

static void S_MuteSound_f(void)
{
	static float old_volume;
	static int is_muted;

	if (is_muted == 0) {
		old_volume = s_volume.value;
		Cvar_SetValue(&s_volume, 0);
		Com_Printf("%s\n", CharsToBrownStatic("Mute"));
		is_muted = 1;
	}
	else if (is_muted == 1) {
		Cvar_SetValue(&s_volume, old_volume);
		Com_Printf("%s %.2f\n", CharsToBrownStatic("Volume is now"), s_volume.value);
		is_muted = 0;
	}
}

static void S_SoundList_f (void)
{
	int i, total = 0;
	unsigned int size;
	sfx_t *sfx;
	sfxcache_t *sc;

	for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++) {
		sc = (sfxcache_t *) Cache_Check (&sfx->cache);
		if (!sc)
			continue;
		size = sc->total_length * sc->format.width * (sc->format.channels);
		total += size;
		if (sc->loopstart >= 0)
			Com_Printf ("L");
		else
			Com_Printf (" ");
		Com_Printf ("(%2db) %6i : %s\n",sc->format.width*8,  size, sfx->name);
	}
	Com_Printf ("Total resident: %i\n", total);
}

void S_LocalSound (char *sound)
{
	sfx_t *sfx;

	if (!snd_initialized || !snd_started || s_nosound.value)
		return;

	if (!(sfx = S_PrecacheSound (sound))) {
		Com_Printf ("S_LocalSound: can't cache %s\n", sound);
		return;
	}

	S_StartSound (cl.playernum+1, -1, sfx, vec3_origin, 1, 0);
}

void S_LocalSoundWithVol(char *sound, float volume)
{
	sfx_t *sfx;

	clamp(volume, 0, 1.0);

	if (!snd_initialized || !snd_started || s_nosound.value)
		return;

	if (!(sfx = S_PrecacheSound (sound))) {
		Com_Printf ("S_LocalSound: can't cache %s\n", sound);
		return;
	}

	S_StartSound (cl.playernum+1, -1, sfx, vec3_origin, volume, 1);
}

//===================================================================
// Streaming audio.
// This is useful when there is one source, and the sound is to be played with no attenuation.
//===================================================================

#define MAX_RAW_CACHE (1024 * 32) // have no idea which size it actually should be.

typedef struct
{
	qbool			inuse;
	int				id;
	sfx_t			sfx;
} streaming_t;

static void S_RawClearStream(streaming_t *s);

#define MAX_RAW_SOURCES (MAX_CLIENTS+1)

streaming_t s_streamers[MAX_RAW_SOURCES] = {{0}};

void S_RawClear(void)
{
	int i;
	streaming_t *s;

	for (s = s_streamers, i = 0; i < MAX_RAW_SOURCES; i++, s++)
	{
		S_RawClearStream(s);
	}

	memset(s_streamers, 0, sizeof(s_streamers));
}

// Stop playing particular stream and make it free.
static void S_RawClearStream(streaming_t *s)
{
	int i;
	sfxcache_t * currentcache;

	if (!s)
		return;

	// get current cache if any.
	currentcache = (sfxcache_t *)Cache_Check(&s->sfx.cache);
	if (currentcache)
	{
		currentcache->loopstart = -1;	//stop mixing it
	}

	// remove link on sfx from the channels array.
	for (i = 0; i < total_channels; i++)
	{
		if (channels[i].sfx == &s->sfx)
		{
			channels[i].sfx = NULL;
			break;
		}
	}

	// free cache.
	if (s->sfx.cache.data)
		Cache_Free(&s->sfx.cache);

	// clear whole struct.
	memset(s, 0, sizeof(*s));
}

// Searching for free slot or re-use previous one with the same sourceid.
static streaming_t * S_RawGetFreeStream(int sourceid)
{
	int i;
	streaming_t *s, *free = NULL;

	for (s = s_streamers, i = 0; i < MAX_RAW_SOURCES; i++, s++)
	{
		if (!s->inuse)
		{
			if (!free)
			{
				free = s;	// found free stream slot.
			}

			continue;
		}

		if (s->id == sourceid)
		{
			return s; // re-using slot.
		}
	}

	return free;
}

// Streaming audio.
// This is useful when there is one source, and the sound is to be played with no attenuation.
void S_RawAudio(int sourceid, byte *data, 
				unsigned int speed, unsigned int samples, unsigned int channelsnum, unsigned int width)
{
	unsigned int	i;
	int				newsize;
	int				prepadl;
	int				spare;
	int				outsamples;
	double			speedfactor;
	sfxcache_t *	currentcache;
	streaming_t *	s;

	// search for free slot or re-use previous one with the same sourceid.
	s = S_RawGetFreeStream(sourceid);

	if (!s)
	{
		Com_DPrintf("No free audio streams or stream not found\n");
		return;
	}

	// empty data mean someone tell us to shut up particular slot.
	if (!data)
	{
		S_RawClearStream(s);
		return;
	}

	// attempting to add new stream
	if (!s->inuse)
	{
		sfxcache_t * newcache;

		// clear whole struct.
		memset(s, 0, sizeof(*s));
		// allocate cache.
		newsize = MAX_RAW_CACHE; //sizeof(sfxcache_t)

		if (newsize < sizeof(sfxcache_t))
			Sys_Error("MAX_RAW_CACHE too small %d", newsize);

		newcache = Cache_Alloc(&s->sfx.cache, newsize, "rawaudio");
		if (!newcache)
		{
			Com_DPrintf("Cache_Alloc failed\n");
			return;
		}

		s->inuse = true;
		s->id = sourceid;
//		strcpy(s->sfx.name, ""); // FIXME: probably we should put some specific tag name here?
		s->sfx.cache.data = newcache;
		newcache->format.speed = shm->format.speed;
		newcache->format.channels = channelsnum;
		newcache->format.width = width;
		newcache->loopstart = -1;
		newcache->total_length = 0;
		newcache = NULL;

//		Com_Printf("Added new raw stream\n");
	}

	// get current cache if any.
	currentcache = (sfxcache_t *)Cache_Check(&s->sfx.cache);
	if (!currentcache)
	{
		Com_DPrintf("Cache_Check failed\n");
		S_RawClearStream(s);
		return;
	}

	if (   currentcache->format.speed != shm->format.speed
		|| currentcache->format.channels != channelsnum
		|| currentcache->format.width != width)
	{
		currentcache->format.speed = shm->format.speed;
		currentcache->format.channels = channelsnum;
		currentcache->format.width = width;
//		newcache->loopstart = -1;
		currentcache->total_length = 0;
//		Com_Printf("Restarting raw stream\n");
	}

	speedfactor	= (double)speed/shm->format.speed;
	outsamples = samples/speedfactor;

	prepadl = 0x7fffffff;
	// FIXME: qqshka: I have no idea that spike is doing here, really. WTF is prepadl??? PITCHSHIFT ???
	// spike: make sure that we have a prepad.
	for (i = 0; i < total_channels; i++)
	{
		if (channels[i].sfx == &s->sfx)
		{
			if (prepadl > (channels[i].pos/*>>PITCHSHIFT*/))
				prepadl = (channels[i].pos/*>>PITCHSHIFT*/);
			break;
		}
	}

	if (prepadl == 0x7fffffff)
	{
		if (s_show.integer)
			Com_Printf("Wasn't playing\n");
		prepadl = 0;
		spare = 0;
		if (spare > shm->format.speed)
		{
			Com_DPrintf("Sacrificed raw sound stream\n");
			spare = 0;	//too far out. sacrifice it all
		}
	}
	else
	{
		if (prepadl < 0)
			prepadl = 0;
		spare = currentcache->total_length - prepadl;
		if (spare < 0)	//remaining samples since last time
			spare = 0;

		if (spare > shm->format.speed * 2) // more than 2 seconds of sound
		{
			Com_DPrintf("Sacrificed raw sound stream\n");
			spare = 0;	//too far out. sacrifice it all
		}
	}

	newsize = sizeof(sfxcache_t) + (spare + outsamples) * currentcache->format.channels * currentcache->format.width;
	if (newsize >= MAX_RAW_CACHE)
	{
		Com_DPrintf("Cache buffer overflowed\n");
		S_RawClearStream(s);
		return;
	}
	// move along spare/remaning samples in the begging of the buffer.
	memmove(currentcache->data, 
		currentcache->data + prepadl * currentcache->format.channels * currentcache->format.width,
							   spare * currentcache->format.channels * currentcache->format.width);

	currentcache->total_length = spare + outsamples;

	// resample.
	{
		extern cvar_t s_linearresample_stream;
		short *outpos = (short *)(currentcache->data + spare * currentcache->format.channels * currentcache->format.width);
		SND_ResampleStream(data,
			speed,
			width,
			channelsnum,
			samples,
			outpos,
			shm->format.speed,
			currentcache->format.width,
			currentcache->format.channels,
			s_linearresample_stream.integer
			);
	}

	currentcache->loopstart = -1;//currentcache->total_length;

	for (i = 0; i < total_channels; i++)
	{
		if (channels[i].sfx == &s->sfx)
		{
#if 0
			// FIXME: qqshka: hrm, should it be just like this all the time??? I think it should.
			channels[i].pos = 0;
			channels[i].end = paintedtime + currentcache->total_length;
#else
			channels[i].pos -= prepadl; // * channels[i].rate;
			channels[i].end += outsamples;
			channels[i].master_vol = (int) (s_raw_volume.value * 255); // this should changed volume on alredy playing sound.

			if (channels[i].end < paintedtime)
			{
				channels[i].pos = 0;
				channels[i].end = paintedtime + currentcache->total_length;
			}
#endif
			break;
		}
	}

	//this one wasn't playing, lets start it then.
	if (i == total_channels)
	{
//		Com_DPrintf("start sound\n");
		/*slight delay to try to avoid frame rate/etc stops/starts*/
//		S_StartSoundCard(si, -1, 0, &s->sfx, r_origin, 1, 32767, -shm->format.speed*0.02, 0);
		S_StartSound(SELF_SOUND, 0, &s->sfx, r_origin, s_raw_volume.value, 0);
	}
}
