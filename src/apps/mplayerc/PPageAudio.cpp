/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2026 see Authors.txt
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

#include "stdafx.h"
#include "filters/renderer/MpcAudioRenderer/MpcAudioRenderer.h"
#include "filters/switcher/AudioSwitcher/IAudioSwitcherFilter.h"
#include "ComPropertySheet.h"
#include "PPageAudio.h"
#include "MainFrm.h"
#include "clsids.h"

static_assert(APP_AUDIOEQ_BANDS == AUDIO_EQ_BANDS);

static const struct {
	UINT nameId;
	int gains[APP_AUDIOEQ_BANDS]; // dB: 63 Hz, 250 Hz, 1 kHz, 4 kHz, 16 kHz
} s_EqPresets[] = {
	{ IDS_EQ_PRESET_FLAT,    {  0,  0,  0,  0,  0 } },
	{ IDS_EQ_PRESET_ROCK,    { +5, +2, -2, +2, +6 } },
	{ IDS_EQ_PRESET_CLASSIC, {  0, +2, +4, +1, +2 } },
	{ IDS_EQ_PRESET_POPS,    { -1, +1, +4, +2, +1 } },
};
#define EQ_PRESET_CUSTOM ((int)std::size(s_EqPresets)) // combo item data of "Custom"

// CPPageAudio dialog

IMPLEMENT_DYNAMIC(CPPageAudio, CPPageBase)
CPPageAudio::CPPageAudio()
	: CPPageBase(CPPageAudio::IDD, CPPageAudio::IDD)
{
}

CPPageAudio::~CPPageAudio()
{
}

void CPPageAudio::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_AUDRND_COMBO, m_iAudioRendererCtrl);
	DDX_Control(pDX, IDC_COMBO1, m_iSecAudioRendererCtrl);
	DDX_Control(pDX, IDC_BUTTON1, m_audRendPropButton);
	DDX_Control(pDX, IDC_CHECK1, m_DualAudioOutput);

	DDX_Control(pDX, IDC_SLIDER1, m_volumectrl);
	DDX_Control(pDX, IDC_SLIDER2, m_balancectrl);
	DDX_Slider(pDX, IDC_SLIDER1, m_nVolume);
	DDX_Slider(pDX, IDC_SLIDER2, m_nBalance);

	DDX_Check(pDX, IDC_CHECK2, m_fAutoloadAudio);
	DDX_Text(pDX, IDC_EDIT4, m_sAudioPaths);
	DDX_Check(pDX, IDC_CHECK3, m_fPrioritizeExternalAudio);

	DDX_Control(pDX, IDC_CHECK_EQ, m_chkEq);
	DDX_Control(pDX, IDC_CHECK_EQ_PREVENTCLIP, m_chkEqPreventClip);
	DDX_Control(pDX, IDC_COMBO_EQPRESET, m_cmbEqPreset);
	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		DDX_Control(pDX, IDC_SLIDER_EQ1 + i, m_sldEq[i]);
		DDX_Control(pDX, IDC_STATIC_EQ1 + i, m_stcEq[i]);
	}
}

BOOL CPPageAudio::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == 'C' || pMsg->wParam == 'c') && GetKeyState(VK_CONTROL) < 0) {
		if (pMsg->hwnd == m_iAudioRendererCtrl.m_hWnd) {
			CStringW text;
			m_iAudioRendererCtrl.GetWindowText(text);
			CopyStringToClipboard(this->m_hWnd, text);
			return TRUE;
		}
	}

	return CPPageBase::PreTranslateMessage(pMsg);
}

BEGIN_MESSAGE_MAP(CPPageAudio, CPPageBase)
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_BN_CLICKED(IDC_CHECK_EQ, OnEqCheck)
	ON_BN_CLICKED(IDC_CHECK_EQ_PREVENTCLIP, OnEqPreventClipCheck)
	ON_CBN_SELCHANGE(IDC_COMBO_EQPRESET, OnEqPresetChange)
	ON_CBN_SELCHANGE(IDC_AUDRND_COMBO, OnAudioRendererChange)
	ON_BN_CLICKED(IDC_BUTTON1, OnAudioRenderPropClick)
	ON_BN_CLICKED(IDC_CHECK1, OnDualAudioOutputCheck)
	ON_STN_DBLCLK(IDC_STATIC_BALANCE, OnBalanceTextDblClk)
	ON_BN_CLICKED(IDC_BUTTON2, OnBnClickedResetAudioPaths)
	ON_NOTIFY_EX(TTN_NEEDTEXTW, 0, OnToolTipNotify)
END_MESSAGE_MAP()

// CPPageAudio message handlers

BOOL CPPageAudio::OnInitDialog()
{
	__super::OnInitDialog();

	SetCursor(m_hWnd, IDC_AUDRND_COMBO, IDC_HAND);

	CAppSettings& s = AfxGetAppSettings();
	CString str;

	m_iAudioRendererCtrl.SetRedraw(FALSE);
	m_iSecAudioRendererCtrl.SetRedraw(FALSE);

	m_audioDevices.emplace_back(audioDeviceInfo_t{ AUDRNDT_MPC, CLSID_MpcAudioRenderer, GUID_NULL, AUDRNDT_MPC });
	str.Format(L"DirectSound: %s", ResStr(IDS_PPAGE_OUTPUT_SYS_DEF));
	m_audioDevices.emplace_back(audioDeviceInfo_t{ str, CLSID_DSoundRender, GUID_NULL, ""});

	// other DirectSound devices
	BeginEnumSysDev(CLSID_AudioRendererCategory, pMoniker) {
		CComPtr<IPropertyBag> pPB;
		if (SUCCEEDED(pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPB))) {
			CComVariant var;
			// skip WaveOut
			if (pPB->Read(CComBSTR(L"WaveOutId"), &var, nullptr) == S_OK) {
				continue;
			}

			audioDeviceInfo_t audioDevice;

			var.Clear();
			if (pPB->Read(CComBSTR(L"CLSID"), &var, nullptr) == S_OK && var.vt == VT_BSTR) {
				audioDevice.clsid = GUIDFromCString(var.bstrVal);
			} else {
				continue;
			}

			var.Clear();
			if (pPB->Read(CComBSTR(L"DSGuid"), &var, nullptr) == S_OK && var.vt == VT_BSTR) {
				if (CString(var.bstrVal) == "{00000000-0000-0000-0000-000000000000}") {
					// skip Default DirectSound Device
					continue;
				}
				audioDevice.dsGuid = GUIDFromCString(var.bstrVal);
			}

			var.Clear();
			if (pPB->Read(CComBSTR(L"FriendlyName"), &var, nullptr) == S_OK && var.vt == VT_BSTR) {
				audioDevice.friendlyName = var.bstrVal;
				if (audioDevice.clsid == CLSID_MpcAudioRenderer) {
					audioDevice.friendlyName.AppendFormat(L" (%s)", ResStr(IDS_EXTERNAL));
				}
			} else {
				continue;
			}

			LPOLESTR olestr = nullptr;
			if (FAILED(pMoniker->GetDisplayName(0, 0, &olestr))) {
				continue;
			}
			audioDevice.displayName = olestr;
			CoTaskMemFree(olestr);

			m_audioDevices.emplace_back(audioDevice);
		}
	}
	EndEnumSysDev;

	m_audioDevices.emplace_back(audioDeviceInfo_t{ AUDRNDT_NULL_COMP, CLSID_NullAudioRenderer, GUID_NULL, AUDRNDT_NULL_COMP });
	m_audioDevices.emplace_back(audioDeviceInfo_t{ AUDRNDT_NULL_UNCOMP, CLSID_NullUAudioRenderer, GUID_NULL, AUDRNDT_NULL_UNCOMP });

	m_iAudioRenderer = 0;
	for (size_t idx = 0; idx < m_audioDevices.size(); idx++) {
		if (s.strAudioRendererDisplayName == m_audioDevices[idx].displayName) {
			m_iAudioRenderer = idx;
			break;
		}
	}

	m_iSecAudioRenderer = (m_iAudioRenderer == 0) ? 1 : 0;
	for (size_t idx = 0; idx < m_audioDevices.size(); idx++) {
		if (s.strAudioRendererDisplayName2 == m_audioDevices[idx].displayName) {
			if (idx != m_iAudioRenderer) {
				m_iSecAudioRenderer = idx;
			}
			break;
		}
	}

	DLog(L"Audio devices:");
	for (size_t i = 0; i < m_audioDevices.size(); i++) {
		const auto& audioDevice = m_audioDevices[i];
		DLog(L"%s, %s, %s, %s", CStringFromGUID(audioDevice.clsid), CStringFromGUID(audioDevice.dsGuid), audioDevice.displayName, audioDevice.friendlyName);

		m_iAudioRendererCtrl.AddString(audioDevice.friendlyName);
		if (i != m_iAudioRenderer) {
			AddStringData(m_iSecAudioRendererCtrl, audioDevice.friendlyName, i);
		}
	}
	m_iAudioRendererCtrl.SetCurSel(m_iAudioRenderer);
	SelectByItemData(m_iSecAudioRendererCtrl, m_iSecAudioRenderer);

	CorrectComboListWidth(m_iAudioRendererCtrl);
	m_iAudioRendererCtrl.SetRedraw(TRUE);
	m_iAudioRendererCtrl.Invalidate();
	m_iAudioRendererCtrl.UpdateWindow();

	m_iSecAudioRendererCtrl.SetDroppedWidth(m_iAudioRendererCtrl.GetDroppedWidth());
	m_iSecAudioRendererCtrl.SetRedraw(TRUE);
	m_iSecAudioRendererCtrl.Invalidate();
	m_iSecAudioRendererCtrl.UpdateWindow();

	m_DualAudioOutput.SetCheck(s.fDualAudioOutput);
	OnDualAudioOutputCheck();

	m_volumectrl.SetRange(0, 100);
	m_volumectrl.SetTicFreq(10);
	m_balancectrl.SetRange(-100, 100);
	m_balancectrl.SetLineSize(2);
	m_balancectrl.SetPageSize(2);
	m_balancectrl.SetTicFreq(20);
	m_nVolume = m_oldVolume = s.nVolume;
	m_nBalance = s.nBalance;

	m_fAutoloadAudio           = s.fAutoloadAudio;
	m_fPrioritizeExternalAudio = s.fPrioritizeExternalAudio;
	m_sAudioPaths              = s.strAudioPaths;

	// equalizer
	for (int i = 0; i < (int)std::size(s_EqPresets); i++) {
		AddStringData(m_cmbEqPreset, ResStr(s_EqPresets[i].nameId), i);
	}
	AddStringData(m_cmbEqPreset, ResStr(IDS_EQ_PRESET_CUSTOM), EQ_PRESET_CUSTOM);

	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		// a vertical trackbar has its minimum at the top, so the position is the inverted gain
		m_sldEq[i].SetRange(-APP_AUDIOEQ_GAIN_MAX, -APP_AUDIOEQ_GAIN_MIN);
		m_sldEq[i].SetTicFreq(6);
		m_sldEq[i].SetLineSize(1);
		m_sldEq[i].SetPageSize(3);
		SetEqGain(i, s.iAudioEqGains[i]);
	}
	m_chkEq.SetCheck(s.bAudioEqEnabled ? BST_CHECKED : BST_UNCHECKED);
	m_chkEqPreventClip.SetCheck(s.bAudioEqPreventClip ? BST_CHECKED : BST_UNCHECKED);
	UpdateEqLabels();
	UpdateEqPresetSel();
	UpdateEqControlsState();

	UpdateData(FALSE);

	OnAudioRendererChange();

	CreateToolTip();

	return TRUE;
}

BOOL CPPageAudio::OnApply()
{
	UpdateData();

	CAppSettings& s = AfxGetAppSettings();

	m_iAudioRenderer = m_iAudioRendererCtrl.GetCurSel();
	m_iSecAudioRenderer = (int)GetCurItemData(m_iSecAudioRendererCtrl);

	s.strAudioRendererDisplayName  = m_audioDevices[m_iAudioRenderer].displayName;
	s.strAudioRendererDisplayName2 = m_audioDevices[m_iSecAudioRenderer].displayName;
	s.fDualAudioOutput             = !!m_DualAudioOutput.GetCheck();

	s.nVolume = m_oldVolume = m_nVolume;
	s.nBalance = m_nBalance;

	s.fAutoloadAudio = !!m_fAutoloadAudio;
	s.fPrioritizeExternalAudio = !!m_fPrioritizeExternalAudio;
	s.strAudioPaths = m_sAudioPaths;

	s.bAudioEqEnabled = !!m_chkEq.GetCheck();
	s.bAudioEqPreventClip = !!m_chkEqPreventClip.GetCheck();
	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		s.iAudioEqGains[i] = GetEqGain(i);
	}
	ApplyEqualizer(s.bAudioEqEnabled, s.bAudioEqPreventClip, s.iAudioEqGains);

	return __super::OnApply();
}

void CPPageAudio::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (*pScrollBar == m_volumectrl) {
		UpdateData();
		AfxGetMainFrame()->m_wndToolBar.Volume = m_nVolume;
	}
	else if (*pScrollBar == m_balancectrl) {
		UpdateData();
		AfxGetMainFrame()->SetBalance(m_nBalance);
	}

	SetModified();

	__super::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CPPageAudio::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar) {
		for (const auto& sld : m_sldEq) {
			if (pScrollBar->m_hWnd == sld.m_hWnd) {
				UpdateEqLabels();
				UpdateEqPresetSel();
				ApplyEqualizerFromControls(); // real time
				SetModified();
				break;
			}
		}
	}

	__super::OnVScroll(nSBCode, nPos, pScrollBar);
}

int CPPageAudio::GetEqGain(int band)
{
	return -m_sldEq[band].GetPos();
}

void CPPageAudio::SetEqGain(int band, int gain_dB)
{
	m_sldEq[band].SetPos(-std::clamp(gain_dB, APP_AUDIOEQ_GAIN_MIN, APP_AUDIOEQ_GAIN_MAX));
}

void CPPageAudio::UpdateEqLabels()
{
	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		const int gain = GetEqGain(i);
		CString str;
		str.Format(gain ? L"%+d dB" : L"%d dB", gain);
		m_stcEq[i].SetWindowTextW(str);
	}
}

void CPPageAudio::UpdateEqPresetSel()
{
	int preset = EQ_PRESET_CUSTOM;
	for (int p = 0; p < (int)std::size(s_EqPresets); p++) {
		bool match = true;
		for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
			if (GetEqGain(i) != s_EqPresets[p].gains[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			preset = p;
			break;
		}
	}
	SelectByItemData(m_cmbEqPreset, preset);
}

void CPPageAudio::UpdateEqControlsState()
{
	const BOOL bEnable = !!m_chkEq.GetCheck();
	m_cmbEqPreset.EnableWindow(bEnable);
	m_chkEqPreventClip.EnableWindow(bEnable);
	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		m_sldEq[i].EnableWindow(bEnable);
		m_stcEq[i].EnableWindow(bEnable);
	}
}

void CPPageAudio::ApplyEqualizer(bool bEnable, bool bPreventClip, const int* pGains_dB)
{
	if (CComQIPtr<IAudioSwitcherFilter> pASF = AfxGetMainFrame()->m_pSwitcherFilter.p) {
		double gains[AUDIO_EQ_BANDS];
		std::copy(pGains_dB, pGains_dB + AUDIO_EQ_BANDS, gains);
		pASF->SetEqualizer(bEnable, bPreventClip, gains, AUDIO_EQ_BANDS);
	}
}

void CPPageAudio::ApplyEqualizerFromControls()
{
	int gains[APP_AUDIOEQ_BANDS];
	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		gains[i] = GetEqGain(i);
	}
	ApplyEqualizer(!!m_chkEq.GetCheck(), !!m_chkEqPreventClip.GetCheck(), gains);
}

void CPPageAudio::OnEqCheck()
{
	UpdateEqControlsState();
	ApplyEqualizerFromControls();
	SetModified();
}

void CPPageAudio::OnEqPreventClipCheck()
{
	ApplyEqualizerFromControls();
	SetModified();
}

void CPPageAudio::OnEqPresetChange()
{
	const int preset = (int)GetCurItemData(m_cmbEqPreset);
	if (preset < 0 || preset >= EQ_PRESET_CUSTOM) {
		return; // "Custom" keeps the current slider positions
	}

	for (int i = 0; i < APP_AUDIOEQ_BANDS; i++) {
		SetEqGain(i, s_EqPresets[preset].gains[i]);
	}
	UpdateEqLabels();
	ApplyEqualizerFromControls();
	SetModified();
}

void CPPageAudio::ShowPPage(CUnknown* (WINAPI * CreateInstance)(LPUNKNOWN lpunk, HRESULT* phr))
{
	if (!CreateInstance) {
		return;
	}

	HRESULT hr;
	CUnknown* pObj = CreateInstance(nullptr, &hr);

	if (!pObj) {
		return;
	}

	CComPtr<IUnknown> pUnk = (IUnknown*)(INonDelegatingUnknown*)pObj;

	if (SUCCEEDED(hr)) {
		if (CComQIPtr<ISpecifyPropertyPages> pSPP = pUnk.p) {
			CComPropertySheet ps(ResStr(IDS_PROPSHEET_PROPERTIES), this);
			ps.AddPages(pSPP);
			ps.DoModal();
		}
	}
}

void CPPageAudio::OnAudioRendererChange()
{
	UpdateData();

	m_iAudioRenderer = m_iAudioRendererCtrl.GetCurSel();

	BOOL flag = FALSE;
	auto& displayName = m_audioDevices[m_iAudioRenderer].displayName;
	if (displayName == AUDRNDT_MPC) {
		flag = TRUE;
	} else {
		BeginEnumSysDev(CLSID_AudioRendererCategory, pMoniker) {
			LPOLESTR olestr = nullptr;
			if (FAILED(pMoniker->GetDisplayName(0, 0, &olestr))) {
				continue;
			}

			CStringW str(olestr);
			CoTaskMemFree(olestr);

			if (str == m_audioDevices[m_iAudioRenderer].displayName) {
				CComPtr<IBaseFilter> pBF;
				HRESULT hr = pMoniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&pBF));
				if (SUCCEEDED(hr)) {
					if (CComQIPtr<ISpecifyPropertyPages> pSPP = pBF.p) {
						flag = TRUE;
						break;
					}
				}
			}
		}
		EndEnumSysDev
	}

	m_audRendPropButton.EnableWindow(flag);

	m_iSecAudioRenderer = static_cast<int>(GetCurItemData(m_iSecAudioRendererCtrl));
	m_iSecAudioRendererCtrl.ResetContent();

	if (m_iSecAudioRenderer == m_iAudioRenderer) {
		m_iSecAudioRenderer = (m_iAudioRenderer == 0) ? 1 : 0;
	}

	for (size_t i = 0; i < m_audioDevices.size(); i++) {
		const auto& audioDevice = m_audioDevices[i];
		if (i != m_iAudioRenderer) {
			AddStringData(m_iSecAudioRendererCtrl, audioDevice.friendlyName, i);
		}
	}

	SelectByItemData(m_iSecAudioRendererCtrl, m_iSecAudioRenderer);

	SetModified();
}

void CPPageAudio::OnAudioRenderPropClick()
{
	auto& displayName = m_audioDevices[m_iAudioRenderer].displayName;

	if (displayName == AUDRNDT_MPC) {
		ShowPPage(CreateInstance<CMpcAudioRenderer>);
	} else {
		BeginEnumSysDev(CLSID_AudioRendererCategory, pMoniker) {
			LPOLESTR olestr = nullptr;
			if (FAILED(pMoniker->GetDisplayName(0, 0, &olestr))) {
				continue;
			}

			CStringW str(olestr);
			CoTaskMemFree(olestr);

			if (str == displayName) {
				CComPtr<IBaseFilter> pBF;
				HRESULT hr = pMoniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&pBF));
				if (SUCCEEDED(hr)) {
					if (CComQIPtr<ISpecifyPropertyPages> pSPP = pBF.p) {
						CComPropertySheet ps(ResStr(IDS_PROPSHEET_PROPERTIES), this);
						ps.AddPages(pSPP);
						ps.DoModal();
					}
				}
				break;
			}
		}
		EndEnumSysDev
	}
}

void CPPageAudio::OnDualAudioOutputCheck()
{
	m_iSecAudioRendererCtrl.EnableWindow(!!m_DualAudioOutput.GetCheck());

	SetModified();
}

void CPPageAudio::OnBalanceTextDblClk()
{
	// double click on text "Balance" resets the balance to zero
	m_nBalance = 0;
	m_balancectrl.SetPos(m_nBalance);

	AfxGetMainFrame()->SetBalance(m_nBalance);

	SetModified();
}

void CPPageAudio::OnBnClickedResetAudioPaths()
{
	CAppSettings& s = AfxGetAppSettings();

	m_sAudioPaths = DEFAULT_AUDIO_PATHS;
	m_sAudioPaths.Append(ResStr(IDS_ADDITIONAL_AUDIO_PATHS));

	UpdateData(FALSE);

	SetModified();
}

void CPPageAudio::OnCancel()
{
	CAppSettings& s = AfxGetAppSettings();

	if (m_nVolume != m_oldVolume) {
		AfxGetMainFrame()->m_wndToolBar.Volume = m_oldVolume;    //not very nice solution
	}

	if (m_nBalance != s.nBalance) {
		AfxGetMainFrame()->SetBalance(s.nBalance);
	}

	// revert the equalizer changes that were applied in real time
	ApplyEqualizer(s.bAudioEqEnabled, s.bAudioEqPreventClip, s.iAudioEqGains);

	__super::OnCancel();
}

BOOL CPPageAudio::OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
	TOOLTIPTEXTW* pTTT = (TOOLTIPTEXTW*)pNMHDR;

	UINT_PTR nID = pNMHDR->idFrom;

	if (pTTT->uFlags & TTF_IDISHWND) {
		nID = ::GetDlgCtrlID((HWND)nID);
	}

	if (nID == 0) {
		return FALSE;
	}

	static CString strTipText;

	if (nID == IDC_SLIDER1) {
		strTipText.Format(L"%d%%", m_nVolume);
	}
	else if (nID == IDC_SLIDER2) {
		if (m_nBalance > 0) {
			strTipText.Format(L"R +%d%%", m_nBalance);
		}
		else if (m_nBalance < 0) {
			strTipText.Format(L"L +%d%%", -m_nBalance);
		}
		else { //if (m_nBalance == 0)
			strTipText = L"L = R";
		}
	}
	else {
		return FALSE;
	}

	pTTT->lpszText = (LPWSTR)(LPCWSTR)strTipText;

	*pResult = 0;

	return TRUE;
}

