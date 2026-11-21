/*
 * (C) 2017-2024 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#define SFMT_UINT8  (1<<0) // not used
#define SFMT_INT16  (1<<1)
#define SFMT_INT24  (1<<2)
#define SFMT_INT32  (1<<3)
#define SFMT_FLOAT  (1<<4)
#define SFMT_DOUBLE (1<<4) // not used

#define SFMT_MASK (SFMT_INT16|SFMT_INT24|SFMT_INT32|SFMT_FLOAT)

enum {
	SPK_MONO = 0,
	SPK_STEREO,
	SPK_4_0,
	SPK_5_0,
	SPK_5_1,
	SPK_7_1
};

// graphic equalizer: 63 Hz, 250 Hz, 1 kHz, 4 kHz, 16 kHz
#define AUDIO_EQ_BANDS     5
#define AUDIO_EQ_GAIN_MAX  12.0
#define AUDIO_EQ_GAIN_MIN -12.0

interface __declspec(uuid("CEDB2890-53AE-4231-91A3-B0AAFCD1DBDE"))
IAudioSwitcherFilter :
public IUnknown {
	STDMETHOD(SetChannelMixer) (bool bMixer, int nLayout) PURE;
	STDMETHOD(SetBassRedirect) (bool bBassRedirect) PURE;
	STDMETHOD(SetLevels) (double dCenterLevel_dB, double dSurroundLevel_dB) PURE;
	STDMETHOD(SetAudioGain) (double dGain_dB) PURE;
	STDMETHOD(SetAutoVolumeControl) (bool bAutoVolumeControl, bool bNormBoost, int iNormLevel, int iNormRealeaseTime) PURE;
	STDMETHOD(SetOutputFormats) (int iSampleFormat) PURE;
	STDMETHOD_(REFERENCE_TIME, GetAudioTimeShift) () PURE;
	STDMETHOD(SetAudioTimeShift) (REFERENCE_TIME rtAudioTimeShift) PURE;
	STDMETHOD(SetAudioFilter1)(const char* str_filter) PURE;
	STDMETHOD_(int, GetAudioFilterState)() PURE;
	STDMETHOD(SetAudioFiltersNotForStereo) (bool bDisableForStereo) PURE;
	STDMETHOD(SetReplayGain) (double dGain_dB) PURE; // per-file gain, applied together with SetAudioGain
	STDMETHOD(SetEqualizer) (bool bEnable, bool bPreventClip, const double* pGains_dB, int nBands) PURE; // nBands gains (AUDIO_EQ_BANDS max), applied in real time; bPreventClip lowers the level by the peak boost
};
