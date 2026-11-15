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

#include "stdafx.h"
#include <atlsafe.h>
#include <MsXml.h>
#include "MainFrm.h"
#include "DlnaBrowserDlg.h"
#include "controls/DarkTheme.h"

#define DLNA_MEDIASERVER_TYPE    L"urn:schemas-upnp-org:device:MediaServer:1"
#define DLNA_CDS_SERVICE_ID      L"urn:upnp-org:serviceId:ContentDirectory"
#define DLNA_CDS_SERVICE_TYPE    L"urn:schemas-upnp-org:service:ContentDirectory:"
#define DLNA_BROWSE_PAGE_SIZE    200

#define IDS_R_DLG_DLNA           L"Dialogs\\DlnaBrowser"

//
// CFinderCallback - receives asynchronous SSDP search results.
// Called on the thread that started the search (the UI thread).
//

class CDlnaBrowserDlg::CFinderCallback : public IUPnPDeviceFinderCallback
{
	LONG m_cRef = 1;
	CDlnaBrowserDlg* m_pDlg;

public:
	CFinderCallback(CDlnaBrowserDlg* pDlg) : m_pDlg(pDlg) {}

	void Detach() { m_pDlg = nullptr; }

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		CheckPointer(ppv, E_POINTER);
		if (riid == IID_IUnknown || riid == __uuidof(IUPnPDeviceFinderCallback)) {
			*ppv = static_cast<IUPnPDeviceFinderCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override
	{
		return InterlockedIncrement(&m_cRef);
	}
	STDMETHODIMP_(ULONG) Release() override
	{
		const LONG cRef = InterlockedDecrement(&m_cRef);
		if (cRef == 0) {
			delete this;
		}
		return cRef;
	}

	// IUPnPDeviceFinderCallback
	STDMETHODIMP DeviceAdded(LONG lFindData, IUPnPDevice* pDevice) override
	{
		if (m_pDlg && pDevice) {
			m_pDlg->OnDeviceAdded(pDevice);
		}
		return S_OK;
	}
	STDMETHODIMP DeviceRemoved(LONG lFindData, BSTR bstrUDN) override
	{
		if (m_pDlg && bstrUDN) {
			m_pDlg->OnDeviceRemoved(bstrUDN);
		}
		return S_OK;
	}
	STDMETHODIMP SearchComplete(LONG lFindData) override
	{
		if (m_pDlg) {
			m_pDlg->OnSearchComplete();
		}
		return S_OK;
	}
};

//
// helpers
//

static CComPtr<IUPnPService> FindContentDirectory(IUPnPDevice* pDevice)
{
	CComPtr<IUPnPServices> pServices;
	if (FAILED(pDevice->get_Services(&pServices)) || !pServices) {
		return nullptr;
	}

	CComPtr<IUPnPService> pService;
	if (SUCCEEDED(pServices->get_Item(CComBSTR(DLNA_CDS_SERVICE_ID), &pService)) && pService) {
		return pService;
	}

	// some servers use a non-standard service id, look up by service type
	CComPtr<IUnknown> pUnk;
	if (FAILED(pServices->get__NewEnum(&pUnk)) || !pUnk) {
		return nullptr;
	}
	CComQIPtr<IEnumVARIANT> pEnum(pUnk);
	if (!pEnum) {
		return nullptr;
	}

	CComVariant var;
	while (pEnum->Next(1, &var, nullptr) == S_OK) {
		if (var.vt == VT_DISPATCH || var.vt == VT_UNKNOWN) {
			CComQIPtr<IUPnPService> pSvc(var.vt == VT_DISPATCH ? (IUnknown*)var.pdispVal : var.punkVal);
			CComBSTR type;
			if (pSvc && SUCCEEDED(pSvc->get_ServiceTypeIdentifier(&type)) && type
					&& wcsncmp(type, DLNA_CDS_SERVICE_TYPE, wcslen(DLNA_CDS_SERVICE_TYPE)) == 0) {
				return CComPtr<IUPnPService>(pSvc);
			}
		}
		var.Clear();
	}

	return nullptr;
}

static bool GetOutArg(SAFEARRAY* psa, LONG idx, CComVariant& var)
{
	var.Clear();
	return SUCCEEDED(SafeArrayGetElement(psa, &idx, &var));
}

static CStringW GetNodeText(IXMLDOMNode* pNode)
{
	CComBSTR text;
	if (SUCCEEDED(pNode->get_text(&text)) && text) {
		return CStringW(text);
	}
	return L"";
}

static CStringW GetNodeAttr(IXMLDOMNode* pNode, LPCWSTR name)
{
	CComQIPtr<IXMLDOMElement> pElem(pNode);
	CComVariant var;
	if (pElem && pElem->getAttribute(CComBSTR(name), &var) == S_OK && var.vt == VT_BSTR) {
		return CStringW(var.bstrVal);
	}
	return L"";
}

static CStringW GetNodeBaseName(IXMLDOMNode* pNode)
{
	CComBSTR name;
	if (SUCCEEDED(pNode->get_baseName(&name)) && name) {
		return CStringW(name);
	}
	return L"";
}

// "H+:MM:SS[.F+]" -> REFERENCE_TIME
static REFERENCE_TIME ParseDuration(const CStringW& str)
{
	unsigned h = 0, m = 0;
	double s = 0.0;
	if (swscanf_s(str, L"%u:%u:%lf", &h, &m, &s) == 3) {
		return (REFERENCE_TIME)((h * 3600.0 + m * 60.0 + s) * 10000000.0);
	}
	return 0;
}

static CStringW FormatDuration(REFERENCE_TIME rt)
{
	const UINT sec = (UINT)(rt / 10000000);
	CStringW str;
	str.Format(L"%u:%02u:%02u", sec / 3600, (sec / 60) % 60, sec % 60);
	return str;
}

// CDlnaBrowserDlg dialog

CDlnaBrowserDlg::CDlnaBrowserDlg(CWnd* pParent)
	: CResizableDialog(CDlnaBrowserDlg::IDD, pParent)
{
}

CDlnaBrowserDlg::~CDlnaBrowserDlg()
{
	// WM_DESTROY handler is not called from the base class destructor
	StopSearch();
}

void CDlnaBrowserDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_EDIT1, m_PathEdit);
	DDX_Control(pDX, IDC_LIST1, m_list);
}

BEGIN_MESSAGE_MAP(CDlnaBrowserDlg, CResizableDialog)
	ON_BN_CLICKED(IDC_BUTTON1, OnBnClickedUp)
	ON_BN_CLICKED(IDC_BUTTON2, OnBnClickedRefresh)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST1, OnDblclkList)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

// CDlnaBrowserDlg message handlers

BOOL CDlnaBrowserDlg::OnInitDialog()
{
	__super::OnInitDialog();

	AddAnchor(IDC_BUTTON1, TOP_LEFT);
	AddAnchor(IDC_BUTTON2, TOP_LEFT);
	AddAnchor(IDC_EDIT1, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_LIST1, TOP_LEFT, BOTTOM_RIGHT);

	m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	m_list.InsertColumn(COL_NAME, ResStr(IDS_DLNA_COL_NAME));
	m_list.InsertColumn(COL_DURATION, ResStr(IDS_DLNA_COL_DURATION), LVCFMT_RIGHT);

	EnableSaveRestore(IDS_R_DLG_DLNA);

	CRect rc;
	m_list.GetClientRect(&rc);
	const int durWidth = m_list.GetStringWidth(L"00:00:00000") + 16;
	m_list.SetColumnWidth(COL_DURATION, durWidth);
	m_list.SetColumnWidth(COL_NAME, std::max(100, rc.Width() - durWidth - GetSystemMetrics(SM_CXVSCROLL)));

	DarkTheme::ThemeDialog(GetSafeHwnd());

	StartSearch();

	return TRUE;
}

BOOL CDlnaBrowserDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_list.GetSafeHwnd()) {
		if (pMsg->wParam == VK_RETURN) {
			Activate(m_list.GetNextItem(-1, LVNI_SELECTED));
			return TRUE;
		}
		if (pMsg->wParam == VK_BACK) {
			GoUp();
			return TRUE;
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

void CDlnaBrowserDlg::StartSearch()
{
	StopSearch();

	m_servers.clear();
	m_curServer = -1;
	m_levels.clear();

	if (!m_pFinder && FAILED(m_pFinder.CoCreateInstance(__uuidof(UPnPDeviceFinder)))) {
		ShowServers();
		return;
	}

	m_pCallback = new CFinderCallback(this);

	HRESULT hr = m_pFinder->CreateAsyncFind(CComBSTR(DLNA_MEDIASERVER_TYPE), 0, m_pCallback, &m_lFindData);
	if (SUCCEEDED(hr)) {
		hr = m_pFinder->StartAsyncFind(m_lFindData);
		if (FAILED(hr)) {
			m_pFinder->CancelAsyncFind(m_lFindData);
		}
	}

	if (SUCCEEDED(hr)) {
		m_bSearching = true;
	} else {
		m_lFindData = 0;
		m_pCallback->Detach();
		m_pCallback->Release();
		m_pCallback = nullptr;
	}

	ShowServers();
}

void CDlnaBrowserDlg::StopSearch()
{
	if (m_pFinder && m_lFindData) {
		m_pFinder->CancelAsyncFind(m_lFindData);
	}
	m_lFindData = 0;
	m_bSearching = false;

	if (m_pCallback) {
		m_pCallback->Detach();
		m_pCallback->Release();
		m_pCallback = nullptr;
	}
}

void CDlnaBrowserDlg::OnDeviceAdded(IUPnPDevice* pDevice)
{
	CComBSTR udn, name;
	if (FAILED(pDevice->get_UniqueDeviceName(&udn)) || !udn) {
		return;
	}
	for (const auto& server : m_servers) {
		if (server.udn == (LPCWSTR)udn) {
			return;
		}
	}

	CComPtr<IUPnPService> pCds = FindContentDirectory(pDevice);
	if (!pCds) {
		return;
	}

	pDevice->get_FriendlyName(&name);

	Server server;
	server.udn = udn;
	server.name = name ? CStringW(name) : server.udn;
	server.pContentDirectory = pCds;
	m_servers.emplace_back(server);

	if (m_curServer < 0) {
		ShowServers();
	}
}

void CDlnaBrowserDlg::OnDeviceRemoved(LPCWSTR udn)
{
	for (size_t i = 0; i < m_servers.size(); i++) {
		if (m_servers[i].udn == udn) {
			if (m_curServer == (int)i) {
				m_curServer = -1;
				m_levels.clear();
			} else if (m_curServer > (int)i) {
				m_curServer--;
			}
			m_servers.erase(m_servers.begin() + i);

			if (m_curServer < 0) {
				ShowServers();
			}
			return;
		}
	}
}

void CDlnaBrowserDlg::OnSearchComplete()
{
	// the async find keeps reporting devices that appear later,
	// so the search itself stays active until it is cancelled
	m_bSearching = false;
	if (m_curServer < 0) {
		UpdatePath();
	}
}

HRESULT CDlnaBrowserDlg::Browse(IUPnPService* pService, const CStringW& objectId, std::vector<Entry>& entries)
{
	CheckPointer(pService, E_POINTER);

	const CComBSTR action(L"Browse");
	UINT start = 0;

	for (;;) {
		CComSafeArray<VARIANT> args(6);
		args.SetAt(0, CComVariant(objectId));
		args.SetAt(1, CComVariant(L"BrowseDirectChildren"));
		args.SetAt(2, CComVariant(L"*"));
		args.SetAt(3, CComVariant(start, VT_UI4));
		args.SetAt(4, CComVariant((UINT)DLNA_BROWSE_PAGE_SIZE, VT_UI4));
		args.SetAt(5, CComVariant(L""));

		CComVariant vIn;
		vIn.vt = VT_ARRAY | VT_VARIANT;
		vIn.parray = args.Detach();

		CComVariant vOut, vRet;
		HRESULT hr = pService->InvokeAction(action, vIn, &vOut, &vRet);
		if (FAILED(hr)) {
			return hr;
		}
		if (vOut.vt != (VT_ARRAY | VT_VARIANT) || !vOut.parray) {
			return E_UNEXPECTED;
		}

		CComVariant vResult, vReturned, vTotal;
		if (!GetOutArg(vOut.parray, 0, vResult) || FAILED(vResult.ChangeType(VT_BSTR))) {
			return E_UNEXPECTED;
		}
		UINT returned = 0, total = 0;
		if (GetOutArg(vOut.parray, 1, vReturned) && SUCCEEDED(vReturned.ChangeType(VT_UI4))) {
			returned = vReturned.ulVal;
		}
		if (GetOutArg(vOut.parray, 2, vTotal) && SUCCEEDED(vTotal.ChangeType(VT_UI4))) {
			total = vTotal.ulVal;
		}

		ParseDidl(CStringW(vResult.bstrVal), entries);

		start += returned;
		if (returned == 0 || start >= total) {
			break;
		}
	}

	return S_OK;
}

void CDlnaBrowserDlg::ParseDidl(const CStringW& didl, std::vector<Entry>& entries)
{
	CComPtr<IXMLDOMDocument> pDoc;
	if (FAILED(pDoc.CoCreateInstance(CLSID_DOMDocument))) {
		return;
	}
	pDoc->put_async(VARIANT_FALSE);
	pDoc->put_validateOnParse(VARIANT_FALSE);
	pDoc->put_resolveExternals(VARIANT_FALSE);

	VARIANT_BOOL bSuccess = VARIANT_FALSE;
	if (FAILED(pDoc->loadXML(CComBSTR(didl), &bSuccess)) || bSuccess != VARIANT_TRUE) {
		return;
	}

	CComPtr<IXMLDOMElement> pRoot;
	if (FAILED(pDoc->get_documentElement(&pRoot)) || !pRoot) {
		return;
	}

	CComPtr<IXMLDOMNode> pNode;
	pRoot->get_firstChild(&pNode);
	while (pNode) {
		const CStringW nodeName = GetNodeBaseName(pNode);
		const bool bContainer = (nodeName == L"container");

		if (bContainer || nodeName == L"item") {
			Entry entry;
			entry.type = bContainer ? Entry::container : Entry::item;
			entry.id = GetNodeAttr(pNode, L"id");

			CStringW upnpClass, resUrl, resFallback;
			REFERENCE_TIME resDuration = 0, resFallbackDuration = 0;

			CComPtr<IXMLDOMNode> pChild;
			pNode->get_firstChild(&pChild);
			while (pChild) {
				const CStringW childName = GetNodeBaseName(pChild);
				if (childName == L"title") {
					entry.title = GetNodeText(pChild);
				}
				else if (childName == L"class") {
					upnpClass = GetNodeText(pChild);
				}
				else if (childName == L"res" && resUrl.IsEmpty()) {
					const CStringW protocolInfo = GetNodeAttr(pChild, L"protocolInfo");
					if (protocolInfo.Left(9).CompareNoCase(L"http-get:") == 0) {
						const CStringW url = GetNodeText(pChild).Trim();
						const REFERENCE_TIME duration = ParseDuration(GetNodeAttr(pChild, L"duration"));
						// prefer the original (non-transcoded) resource
						if (protocolInfo.Find(L"DLNA.ORG_CI=1") < 0) {
							resUrl = url;
							resDuration = duration;
						}
						else if (resFallback.IsEmpty()) {
							resFallback = url;
							resFallbackDuration = duration;
						}
					}
				}

				CComPtr<IXMLDOMNode> pNext;
				pChild->get_nextSibling(&pNext);
				pChild = pNext;
			}

			if (bContainer) {
				if (entry.id.GetLength()) {
					entries.emplace_back(entry);
				}
			}
			else {
				if (resUrl.IsEmpty()) {
					resUrl = resFallback;
					resDuration = resFallbackDuration;
				}
				const bool bPlayable = StartsWith(upnpClass, L"object.item.videoItem")
									|| StartsWith(upnpClass, L"object.item.audioItem");
				if (bPlayable && resUrl.GetLength()) {
					entry.url = resUrl;
					entry.duration = resDuration;
					entries.emplace_back(entry);
				}
			}
		}

		CComPtr<IXMLDOMNode> pNext;
		pNode->get_nextSibling(&pNext);
		pNode = pNext;
	}
}

void CDlnaBrowserDlg::ShowServers()
{
	m_curServer = -1;
	m_levels.clear();
	m_entries.clear();

	for (size_t i = 0; i < m_servers.size(); i++) {
		Entry entry;
		entry.type = Entry::server;
		entry.serverIdx = (int)i;
		entry.title = m_servers[i].name;
		m_entries.emplace_back(entry);
	}

	FillList();
	UpdatePath();
}

void CDlnaBrowserDlg::ShowFolder()
{
	if (m_curServer < 0 || m_curServer >= (int)m_servers.size() || m_levels.empty()) {
		ShowServers();
		return;
	}

	std::vector<Entry> entries;
	HRESULT hr;
	{
		CWaitCursor wait;
		hr = Browse(m_servers[m_curServer].pContentDirectory, m_levels.back().id, entries);
	}

	if (FAILED(hr)) {
		CStringW msg;
		msg.Format(ResStr(IDS_DLNA_BROWSE_FAILED), hr);
		AfxMessageBox(msg, MB_ICONERROR);

		m_levels.pop_back();
		if (m_levels.empty()) {
			ShowServers();
		}
		return;
	}

	m_entries.clear();
	Entry up;
	up.type = Entry::up;
	up.title = L"..";
	m_entries.emplace_back(up);
	m_entries.insert(m_entries.end(), entries.begin(), entries.end());

	FillList();
	UpdatePath();
}

void CDlnaBrowserDlg::FillList()
{
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();

	const CStringW folder = ResStr(IDS_DLNA_FOLDER);

	for (size_t i = 0; i < m_entries.size(); i++) {
		const auto& entry = m_entries[i];

		const int n = m_list.InsertItem((int)i, entry.title);
		m_list.SetItemData(n, (DWORD_PTR)i);

		if (entry.type == Entry::container) {
			m_list.SetItemText(n, COL_DURATION, folder);
		}
		else if (entry.type == Entry::item && entry.duration > 0) {
			m_list.SetItemText(n, COL_DURATION, FormatDuration(entry.duration));
		}
	}

	if (m_list.GetItemCount() > 0) {
		m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}

	m_list.SetRedraw(TRUE);
	m_list.Invalidate();

	GetDlgItem(IDC_BUTTON1)->EnableWindow(m_curServer >= 0);
}

void CDlnaBrowserDlg::UpdatePath()
{
	CStringW path;

	if (m_curServer < 0) {
		path = ResStr(m_bSearching && m_servers.empty() ? IDS_DLNA_SEARCHING : IDS_DLNA_SERVERS);
	}
	else {
		path = m_servers[m_curServer].name;
		// level 0 is the server root
		for (size_t i = 1; i < m_levels.size(); i++) {
			path.AppendFormat(L" / %s", m_levels[i].title.GetString());
		}
	}

	m_PathEdit.SetWindowTextW(path);
}

void CDlnaBrowserDlg::GoUp()
{
	if (m_curServer < 0) {
		return;
	}

	if (m_levels.size() > 1) {
		m_levels.pop_back();
		ShowFolder();
	}
	else {
		ShowServers();
	}
}

void CDlnaBrowserDlg::Activate(int nItem)
{
	if (nItem < 0) {
		return;
	}

	const size_t index = (size_t)m_list.GetItemData(nItem);
	if (index >= m_entries.size()) {
		return;
	}

	const Entry entry = m_entries[index];

	switch (entry.type) {
	case Entry::up:
		GoUp();
		break;
	case Entry::server:
		m_curServer = entry.serverIdx;
		m_levels.clear();
		m_levels.push_back({ L"0", entry.title }); // "0" is the root container
		ShowFolder();
		break;
	case Entry::container:
		m_levels.push_back({ entry.id, entry.title });
		ShowFolder();
		break;
	case Entry::item:
		PlayItem(index);
		break;
	}
}

void CDlnaBrowserDlg::PlayItem(size_t index)
{
	// put all playable items of the current folder into the playlist
	// and start playback from the selected one
	CFileItemList fis;
	int selIdx = 0;

	for (size_t i = 0; i < m_entries.size(); i++) {
		const auto& entry = m_entries[i];
		if (entry.type == Entry::item) {
			if (i == index) {
				selIdx = (int)fis.size();
			}
			fis.emplace_back(entry.url, entry.title, entry.duration);
		}
	}

	if (fis.empty()) {
		return;
	}

	// close (hide) the dialog like a file open dialog;
	// it keeps the current folder for the next time it is shown
	ShowWindow(SW_HIDE);

	auto pFrame = AfxGetMainFrame();
	pFrame->m_wndPlaylistBar.Open(fis);
	pFrame->m_wndPlaylistBar.SetSelIdx(selIdx, true);
	pFrame->OpenCurPlaylistItem();
}

void CDlnaBrowserDlg::OnBnClickedUp()
{
	GoUp();
}

void CDlnaBrowserDlg::OnBnClickedRefresh()
{
	if (m_curServer >= 0) {
		ShowFolder();
	}
	else {
		StartSearch();
	}
}

void CDlnaBrowserDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	Activate(((NM_LISTVIEW*)pNMHDR)->iItem);

	*pResult = 0;
}

void CDlnaBrowserDlg::OnDestroy()
{
	StopSearch();

	__super::OnDestroy();
}
