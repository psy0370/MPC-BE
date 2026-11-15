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

#include <afxcmn.h>
#include <afxwin.h>
#include <upnp.h>
#include <ExtLib/ui/ResizableLib/ResizableDialog.h>

// CDlnaBrowserDlg dialog
// Minimal DLNA/UPnP AV client (DMP): discovers MediaServer devices,
// browses their ContentDirectory and plays items via their HTTP URLs.

class CDlnaBrowserDlg : public CResizableDialog
{
private:
	enum { IDD = IDD_DLNA_BROWSER };

	enum {
		COL_NAME = 0,
		COL_DURATION,
		COL_COUNT
	};

	struct Server {
		CStringW udn;
		CStringW name;
		CComPtr<IUPnPService> pContentDirectory;
	};

	struct Entry {
		enum type_t {
			up,
			server,
			container,
			item,
		} type = item;
		int      serverIdx = -1;
		CStringW id;
		CStringW title;
		CStringW url;
		REFERENCE_TIME duration = 0;
	};

	struct Level {
		CStringW id;
		CStringW title;
	};

	class CFinderCallback;
	friend class CFinderCallback;

	CListCtrl m_list;
	CEdit     m_PathEdit;

	CComPtr<IUPnPDeviceFinder> m_pFinder;
	CFinderCallback* m_pCallback = nullptr;
	LONG m_lFindData = 0;
	bool m_bSearching = false;

	std::vector<Server> m_servers;
	int m_curServer = -1;
	std::vector<Level> m_levels; // folder stack of the current server
	std::vector<Entry> m_entries;

public:
	CDlnaBrowserDlg(CWnd* pParent = nullptr);
	virtual ~CDlnaBrowserDlg();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	void StartSearch();
	void StopSearch();
	void OnDeviceAdded(IUPnPDevice* pDevice);
	void OnDeviceRemoved(LPCWSTR udn);
	void OnSearchComplete();

	HRESULT Browse(IUPnPService* pService, const CStringW& objectId, std::vector<Entry>& entries);
	static void ParseDidl(const CStringW& didl, std::vector<Entry>& entries);

	void ShowServers();
	void ShowFolder();
	void FillList();
	void UpdatePath();
	void GoUp();
	void Activate(int nItem);
	void PlayItem(size_t index);

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedUp();
	afx_msg void OnBnClickedRefresh();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
};
