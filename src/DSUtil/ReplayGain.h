/*
 * (C) 2026 see Authors.txt
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

#include <cmath>
#include <locale.h>
#include <stdlib.h>
#include "DSMPropertyBag.h"

// Property names used to pass ReplayGain tags from splitters to the player (IDSMPropertyBag).
// Values are stored as the original tag text, e.g. "-6.50 dB" or "0.988525".
#define RG_PROP_TRACK_GAIN L"REPLAYGAIN_TRACK_GAIN"
#define RG_PROP_TRACK_PEAK L"REPLAYGAIN_TRACK_PEAK"
#define RG_PROP_ALBUM_GAIN L"REPLAYGAIN_ALBUM_GAIN"
#define RG_PROP_ALBUM_PEAK L"REPLAYGAIN_ALBUM_PEAK"

// Parses a ReplayGain value such as "-6.50 dB", "+1.2dB" or "0.988525".
// Locale independent; a decimal comma is accepted as well.
inline bool ParseReplayGainValue(CStringW str, double& value)
{
	static const _locale_t c_locale = _create_locale(LC_NUMERIC, "C");

	str.Trim();
	str.Replace(L',', L'.');
	if (str.IsEmpty()) {
		return false;
	}

	wchar_t* end = nullptr;
	const double v = _wcstod_l(str.GetString(), &end, c_locale);
	if (end == str.GetString() || !std::isfinite(v)) {
		return false;
	}

	value = v;
	return true;
}

// Stores a tag in the property bag if it is a ReplayGain tag.
// Also accepts Opus R128_TRACK_GAIN / R128_ALBUM_GAIN (Q7.8 dB relative to -23 LUFS),
// converted to the ReplayGain reference level (-18 LUFS); REPLAYGAIN_* tags take precedence.
// Returns true if the key was recognized.
inline bool SetReplayGainProperty(IDSMPropertyBag* pPB, CStringW key, CStringW value)
{
	if (!pPB) {
		return false;
	}

	key.Trim();
	key.MakeUpper();
	value.Trim();
	if (value.IsEmpty()) {
		return false;
	}

	if (key == RG_PROP_TRACK_GAIN || key == RG_PROP_TRACK_PEAK
			|| key == RG_PROP_ALBUM_GAIN || key == RG_PROP_ALBUM_PEAK) {
		pPB->SetProperty(key, value);
		return true;
	}

	LPCWSTR rgKey = nullptr;
	if (key == L"R128_TRACK_GAIN") {
		rgKey = RG_PROP_TRACK_GAIN;
	} else if (key == L"R128_ALBUM_GAIN") {
		rgKey = RG_PROP_ALBUM_GAIN;
	}

	if (rgKey) {
		CComBSTR existing;
		if (SUCCEEDED(pPB->GetProperty(rgKey, &existing)) && existing.Length()) {
			return true;
		}

		const int q78 = _wtoi(value);
		CStringW str;
		str.Format(L"%.2f dB", q78 / 256.0 + 5.0);
		pPB->SetProperty(rgKey, str);
		return true;
	}

	return false;
}
