/*
Copyright (C) 1997-2001 Id Software, Inc.

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

*/
// snd_mem.c: sound caching

#include "client.h"
#include "snd_loc.h"

#pragma region ======================= ResampleSfx

static void ResampleSfx(sfx_t *sfx, int inrate, int inwidth, byte *data)
{
	sfxcache_t* sc = sfx->cache;
	if (!sc)
		return;

	const float stepscale = (float)inrate / dma.speed; // This is usually 0.5, 1, or 2
	
	const int outcount = sc->length / stepscale;
	sc->length = outcount;
	if (sc->loopstart != -1)
		sc->loopstart = sc->loopstart / stepscale;

	sc->speed = dma.speed;
	if (s_loadas8bit->value)
		sc->width = 1;
	else
		sc->width = inwidth;

	// Resample / decimate to the current source rate
	if (stepscale == 1 && inwidth == 1 && sc->width == 1)
	{
		// Fast special case
		for (int i = 0; i < outcount; i++)
			((signed char *)sc->data)[i] = (int)(data[i] - 128);
	}
	else
	{
		// General case
		int samplefrac = 0;
		const int fracstep = stepscale * 256;
		for (int i = 0; i < outcount; i++)
		{
			const int srcsample = samplefrac >> 8;
			samplefrac += fracstep;

			int sample;
			if (inwidth == 2)
				sample = ((short *)data)[srcsample];
			else
				sample = (int)(data[srcsample] - 128) << 8;

			if (sc->width == 2)
				((short *)sc->data)[i] = sample;
			else
				((signed char *)sc->data)[i] = sample >> 8;
		}
	}
}

#pragma endregion

#pragma region ======================= S_LoadSound

wavinfo_t GetWavInfo(char *name, byte *wav, int wavlength);

sfxcache_t *S_LoadSound(sfx_t *s)
{
	char namebuffer[MAX_QPATH];
	byte *data;
	char *name;

	if (s->name[0] == '*')
		return NULL;

	// See if still in memory
	sfxcache_t* sc = s->cache;
	if (sc)
		return sc;

	// Load it in
	if (s->truename)
		name = s->truename;
	else
		name = s->name;

	if (name[0] == '#')
		Q_strncpyz(namebuffer, &name[1], sizeof(namebuffer));
	else
		Com_sprintf(namebuffer, sizeof(namebuffer), "sound/%s", name);

	const int size = FS_LoadFile(namebuffer, (void **)&data);

	if (!data)
	{
		Com_DPrintf("Couldn't load sound '%s'\n", namebuffer);
		return NULL;
	}

	const wavinfo_t info = GetWavInfo(s->name, data, size);
	if (info.channels < 1 || info.channels > 2)	//CDawg changed
	{
		Com_Printf("Sound '%s' has unsupported number of channels (%i, supported: 1 or 2)\n", s->name, info.channels);
		FS_FreeFile(data);

		return NULL;
	}

	// Calculate resampled length
	const float stepscale = (float)info.rate / dma.speed;
	int len = info.samples / stepscale;
	len *= info.width * info.channels;

	sc = s->cache = Z_Malloc(len + sizeof(sfxcache_t));
	if (!sc)
	{
		FS_FreeFile(data);
		return NULL;
	}
	
	sc->length = info.samples;
	sc->loopstart = info.loopstart;
	sc->speed = info.rate * info.channels; //CDawg changed
	sc->width = info.width;
	sc->stereo = info.channels;
	sc->music = !strncmp(namebuffer, "music/", 6);

	// Force loopstart if it's a music file
	if (sc->music && sc->loopstart == -1)
		sc->loopstart = 0;

	ResampleSfx(s, sc->speed, sc->width, data + info.dataofs);
	FS_FreeFile(data);

	return sc;
}

#pragma endregion

#pragma region ======================= WAV loading

static byte *data_p;
static byte *iff_end;
static byte *last_chunk;
static byte *iff_data;
static int iff_chunk_len;

static short GetLittleShort(void)
{
	short val = *data_p;
	val += *(data_p + 1) << 8;
	data_p += 2;

	return val;
}

static int GetLittleLong(void)
{
	int val = *data_p;
	val += *(data_p + 1) << 8;
	val += *(data_p + 2) << 16;
	val += *(data_p + 3) << 24;
	data_p += 4;

	return val;
}

static void FindNextChunk(char *name)
{
	while (true)
	{
		data_p = last_chunk;

		if (data_p >= iff_end)
		{
			// Didn't find the chunk
			data_p = NULL;
			return;
		}
		
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		if (iff_chunk_len < 0)
		{
			data_p = NULL;
			return;
		}

		data_p -= 8;
		last_chunk = data_p + 8 + ((iff_chunk_len + 1) & ~1);

		if (!strncmp((char *)data_p, name, 4))
			return;
	}
}

static void FindChunk(char *name)
{
	last_chunk = iff_data;
	FindNextChunk(name);
}

/*static void DumpChunks(void) //mxd. Never used
{
	char str[5];
	
	str[4] = 0;
	data_p = iff_data;

	do
	{
		memcpy(str, data_p, 4);
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		Com_Printf("0x%x : %s (%d)\n", (int)(data_p - 4), str, iff_chunk_len);
		data_p += (iff_chunk_len + 1) & ~1;
	} while (data_p < iff_end);
}*/

static wavinfo_t GetWavInfo(char *name, byte *wav, int wavlength)
{
	wavinfo_t info;
	memset(&info, 0, sizeof(info));

	if (!wav)
		return info;
		
	iff_data = wav;
	iff_end = wav + wavlength;

	// Find "RIFF" chunk
	FindChunk("RIFF");
	if (!(data_p && !strncmp((char *)data_p + 8, "WAVE", 4)))
	{
		Com_Printf("Missing RIFF/WAVE chunks\n");
		return info;
	}

	// Get "fmt " chunk
	iff_data = data_p + 12;

	FindChunk("fmt ");
	if (!data_p)
	{
		Com_Printf("Missing fmt chunk\n");
		return info;
	}

	data_p += 8;
	const int format = GetLittleShort();
	if (format != 1)
	{
		Com_Printf("Microsoft PCM format only\n");
		return info;
	}

	info.channels = GetLittleShort();
	info.rate = GetLittleLong();
	data_p += 6;
	info.width = GetLittleShort() / 8;

	// Get cue chunk
	FindChunk("cue ");
	if (data_p)
	{
		data_p += 32;
		info.loopstart = GetLittleLong();

		// If the next chunk is a LIST chunk, look for a cue length marker
		FindNextChunk("LIST");
		if (data_p)
		{
			if (!strncmp((char *)data_p + 28, "mark", 4))
			{
				// This is not a proper parse, but it works with cooledit...
				data_p += 24;
				const int i = GetLittleLong(); // Samples in loop
				info.samples = info.loopstart + i;
			}
		}
	}
	else
	{
		info.loopstart = -1;
	}

	// Find data chunk
	FindChunk("data");
	if (!data_p)
	{
		Com_Printf("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	const int samples = GetLittleLong() / info.width;

	if (info.samples)
	{
		if (samples < info.samples)
			Com_Error(ERR_DROP, "Sound '%s' has a bad loop length", name);
	}
	else
	{
		info.samples = samples;
	}

	info.dataofs = data_p - wav;
	
	return info;
}

#pragma endregion