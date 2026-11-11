// ClipboardViewer.cpp : implementation file
//

#include "stdafx.h"
#include "cp_main.h"
#include "ClipboardViewer.h"
#include "Misc.h"
#include "..\Shared\Tokenizer.h"
#include "WildCardMatch.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CClipboardViewer

CClipboardViewer::CClipboardViewer(CCopyThread* pHandler) :
	m_hNextClipboardViewer(0),
	m_bCalling_SetClipboardViewer(false),
	m_pHandler(pHandler),
	m_bPinging(false),
	m_bIsConnected(false),
	m_bConnect(false),
	m_dwLastCopy(0),
	m_connectOnStartup(true)
{
	m_activeWindowTitle = _T("");
	m_activeWindow = _T("");
}

CClipboardViewer::~CClipboardViewer()
{
}


BEGIN_MESSAGE_MAP(CClipboardViewer, CWnd)
	//{{AFX_MSG_MAP(CClipboardViewer)
	ON_WM_CREATE()
	ON_WM_CHANGECBCHAIN()
	ON_WM_DRAWCLIPBOARD()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_SETCONNECT, OnSetConnect)
	ON_MESSAGE(WM_CLIPBOARDUPDATE, OnClipboardChange)
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CClipboardViewer message handlers
void CClipboardViewer::Create()
{
	CString strParentClass = AfxRegisterWndClass(0);
	CWnd::CreateEx(0, strParentClass, _T("Ditto Clipboard Viewer"), 0, -1, -1, 0, 0, 0, 0);

	if(m_connectOnStartup && !m_bIsConnected)
	{
		SetConnect(true);
	}
}

// connects as a clipboard viewer
void CClipboardViewer::Connect()
{
	if(m_bIsConnected)
	{
		m_bConnect = true;
		SetEnsureConnectedTimer();
		return;
	}

	Log(_T("Connect to Clipboard"));
	m_bCalling_SetClipboardViewer = true;

	bool connected = false;
	bool useSetClipboardWnd = true;

	if(IsVista())
	{
		HMODULE hUser32 = LoadLibrary(_T("USER32.dll"));
		if(hUser32)
		{
			typedef BOOL (__stdcall *AddClipFormatListener)(HWND hwnd);
			AddClipFormatListener addListener = (AddClipFormatListener)GetProcAddress(hUser32, "AddClipboardFormatListener");
			if(addListener)
			{
				Log(_T("Connecting to clipboard with function AddClipboardFormatListener"));
				if(addListener(m_hWnd))
				{
					connected = true;
					useSetClipboardWnd = false;
				}
				else
				{
					Log(StrF(_T("AddClipboardFormatListener failed, error=%lu; falling back to SetClipboardViewer"), GetLastError()));
				}
			}
			FreeLibrary(hUser32);
		}
	}

	if(useSetClipboardWnd)
	{
		Log(_T("Connecting to clipboard with function SetClipboardViewer"));
		SetLastError(ERROR_SUCCESS);
		m_hNextClipboardViewer = CWnd::SetClipboardViewer();
		DWORD error = GetLastError();
		// SetClipboardViewer may legitimately return NULL when this window is the end of the chain.
		connected = (m_hNextClipboardViewer != NULL || error == ERROR_SUCCESS);
		if(!connected)
		{
			Log(StrF(_T("SetClipboardViewer failed, error=%lu"), error));
		}
	}

	m_bCalling_SetClipboardViewer = false;
	m_bIsConnected = connected;
	m_bConnect = true;

	if(m_bIsConnected)
	{
		SetEnsureConnectedTimer();
	}
	else
	{
		// Retry quickly after a transient shell/clipboard initialization failure.
		SetTimer(TIMER_ENSURE_VIEWER_IN_CHAIN, 2000, NULL);
	}
}

void CClipboardViewer::SetEnsureConnectedTimer()
{
	SetTimer(TIMER_ENSURE_VIEWER_IN_CHAIN, ONE_MINUTE*5, NULL);
}

// disconnects as a clipboard viewer
void CClipboardViewer::Disconnect(bool bSendPing)
{
	Log(_T("Disconnect From Clipboard"));

	KillTimer(TIMER_ENSURE_VIEWER_IN_CHAIN);
	KillTimer(TIMER_PING);

	if(!m_bIsConnected)
	{
		m_bConnect = false;
		m_bPinging = false;
		return;
	}

	bool removed = false;
	bool tryOldWay = true;

	if(IsVista())
	{
		HMODULE hUser32 = LoadLibrary(_T("USER32.dll"));
		if(hUser32)
		{
			typedef BOOL (__stdcall *RemoveClipFormatListener)(HWND hwnd);
			RemoveClipFormatListener removeListener = (RemoveClipFormatListener)GetProcAddress(hUser32, "RemoveClipboardFormatListener");
			if(removeListener)
			{
				Log(_T("Disconnecting from clipboard with function RemoveClipboardFormatListener"));
				if(removeListener(m_hWnd))
				{
					removed = true;
					tryOldWay = false;
				}
				else
				{
					Log(StrF(_T("RemoveClipboardFormatListener failed, error=%lu"), GetLastError()));
				}
			}
			FreeLibrary(hUser32);
		}
	}

	if(tryOldWay && m_hNextClipboardViewer != NULL)
	{
		Log(_T("Disconnecting from clipboard with function ChangeClipboardChain"));
		for(int attempt = 0; attempt < 2 && !removed; ++attempt)
		{
			removed = CWnd::ChangeClipboardChain(m_hNextClipboardViewer) == TRUE;
			if(!removed)
			{
				Log(StrF(_T("Error disconnecting from clipboard chain, attempt=%d, error=%lu"), attempt + 1, GetLastError()));
				Sleep(20);
			}
		}
	}

	m_hNextClipboardViewer = 0;
	m_bConnect = false;
	m_bIsConnected = false;
	m_bPinging = false;
	if(bSendPing)
		SendPing();
}

void CClipboardViewer::SendPing()
{
	if(CGetSetOptions::m_bEnsureConnectToClipboard)
	{
		bool opened = false;
		for(int attempt = 0; attempt < 5 && !opened; ++attempt)
		{
			opened = OpenClipboard() == TRUE;
			if(!opened && attempt < 4)
			{
				Sleep(15);
			}
		}

		if(opened)
		{
			m_bPinging = true;
			SetClipboardData(theApp.m_PingFormat, NewGlobalP("Ditto Ping", sizeof("Ditto Ping")));
			SetClipboardData(theApp.m_cfIgnoreClipboard, NewGlobalP("Ignore", sizeof("Ignore")));

			SetTimer(TIMER_PING, 2000, NULL);
			CloseClipboard();
		}
		else
		{
			Log(StrF(_T("SendPing could not open clipboard after retries, error=%lu"), GetLastError()));
		}
	}
}

void CClipboardViewer::SetConnect(bool bConnect)
{
	m_bConnect = bConnect;
	if(bConnect)
	{
		if(m_bIsConnected == false)
		{
			Connect();
		}
		else
		{
			SendPing();
		}
	}
	else
	{
		Disconnect();
	}
}

/////////////////////////////////////////////////////////////////////////////
// CClipboardViewer message handlers

int CClipboardViewer::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if(CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;
	
	//Set up the clip board viewer
	if(m_connectOnStartup)
	{
		Connect();
	}
	
	return 0;
}

void CClipboardViewer::OnDestroy()
{
	Disconnect();
	CWnd::OnDestroy();
}

void CClipboardViewer::OnChangeCbChain(HWND hWndRemove, HWND hWndAfter) 
{
	Log(_T("OnChangeCbChain"));
	
	// If the next window is closing, repair the chain. 
	if(m_hNextClipboardViewer == hWndRemove)
    {
		m_hNextClipboardViewer = hWndAfter;
    }
    // Otherwise, pass the message to the next link.
	else if (m_hNextClipboardViewer != NULL)
    {
		if(m_hNextClipboardViewer != m_hWnd)
		{
			DWORD_PTR result = 0;
			::SendMessageTimeout(m_hNextClipboardViewer, WM_CHANGECBCHAIN, (WPARAM)hWndRemove, (LPARAM)hWndAfter,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result);
		}
		else
		{
			m_hNextClipboardViewer = NULL;
		}
    }
}

LRESULT CClipboardViewer::OnClipboardChange(WPARAM wParam, LPARAM lPara)
{
	Log(StrF(_T("OnClipboardChange - Start")));
	OnDrawClipboard();
	Log(StrF(_T("OnClipboardChange - End")));

	return TRUE;
}

bool CClipboardViewer::GetIgnoreClipboardChange()
{
	if(::IsClipboardFormatAvailable(theApp.m_cfIgnoreClipboard))
	{
		Log(_T("Clipboard Viewer Ignore clipboard format is on the clipboard, ignoring change"));
		return true;
	}

	if (CGetSetOptions::m_enforceClipboardIgnoreFormats == false)
	{
		return false;
	}

	//https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats
	if (::IsClipboardFormatAvailable(theApp.m_excludeClipboardContentFromMonitorProcessing))
	{
		Log(_T("ExcludeClipboardContentFromMonitorProcessing clipboard format is on the clipboard, ignoring change"));
		return true;
	}

	return false;
}

//Message that the clipboard data has changed
void CClipboardViewer::OnDrawClipboard() 
{
	if(::IsClipboardFormatAvailable(theApp.m_PingFormat))
	{
		m_bPinging = false;
		return;
	}

	// don't process the event when we first attach
	if(m_pHandler && !m_bCalling_SetClipboardViewer)
	{
		if(m_bIsConnected)
		{
			if(GetIgnoreClipboardChange() == false)
			{
				if(ValidActiveWnd())
				{          
					Log(StrF(_T("OnDrawClipboard:: *** SetTimer *** %d"), GetTickCount()));

					KillTimer(TIMER_DRAW_CLIPBOARD);
					SetTimer(TIMER_DRAW_CLIPBOARD, CGetSetOptions::m_lProcessDrawClipboardDelay, NULL);		
				}
			}
		}
		else
		{
			Log(_T("Not connected, ignore clipboard change"));
		}
	}

	// pass the event to the next Clipboard viewer in the chain
	if(m_hNextClipboardViewer != NULL)
	{
		if(m_hNextClipboardViewer != m_hWnd)
		{
			DWORD_PTR result = 0;
			::SendMessageTimeout(m_hNextClipboardViewer, WM_DRAWCLIPBOARD, 0, 0,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result);
		}
		else
		{
			m_hNextClipboardViewer = NULL;
		}
	}
}

bool CClipboardViewer::ValidActiveWnd()
{
	m_activeWindow = _T("");
	m_activeWindowTitle = _T("");

	HWND owner = ::GetClipboardOwner();
	if (owner != NULL)
	{
		DWORD PID = 0;
		::GetWindowThreadProcessId(owner, &PID);

		if (PID != 0)
		{
			m_activeWindow = GetProcessName(NULL, PID);
			m_activeWindowTitle = TopLevelWindowText(PID);			
		}
	}

	//L"RuntimeBroker.exe" is what all modern apps report as
	if (m_activeWindow == _T(""))
	{
		HWND active = ::GetForegroundWindow();
		m_activeWindow = GetProcessName(active, 0);
		m_activeWindowTitle = GetWndText(active);
	}

	m_activeWindow = m_activeWindow.MakeLower();

	CString includeApps = CGetSetOptions::GetCopyAppInclude().MakeLower();

	Log(StrF(_T("INCLUDE app names: %s, Active App: %s"), includeApps, m_activeWindow));

	bool tokenMatch = false;

	CTokenizer token(includeApps, CGetSetOptions::GetCopyAppSeparator());
	CString line;

	while(token.Next(line))
	{
		if(line != "")
		{
			if(CWildCardMatch::WildMatch(line.Trim(), m_activeWindow, ""))
			{
				Log(StrF(_T("Inlclude app names Found Match %s - %s"), line, m_activeWindow));

				tokenMatch = true;
				break;
			}
		}
	}				

	if(tokenMatch)
	{
		CString excludeApps = CGetSetOptions::GetCopyAppExclude().MakeLower();

		if(excludeApps != "")
		{
			Log(StrF(_T("EXCLUDE app names %s, Active App: %s"), excludeApps, m_activeWindow));

			CTokenizer token2(excludeApps, CGetSetOptions::GetCopyAppSeparator());
			CString line2;
			while(token2.Next(line2))
			{
				if(line2 != "")
				{
					if(CWildCardMatch::WildMatch(line2.Trim(), m_activeWindow, ""))
					{
						Log(StrF(_T("Exclude app names Found Match %s - %s - NOT SAVING COPY"), line2, m_activeWindow));

						return false;
					}
				}
			}
		}
	}
	else
	{
		Log(StrF(_T("Didn't find a match to INCLUDE match %s, NOT SAVING COPY"), includeApps));
		return false;
	}

	return true;
}

void CClipboardViewer::OnTimer(UINT_PTR nIDEvent) 
{
	switch(nIDEvent)
	{
	case TIMER_ENSURE_VIEWER_IN_CHAIN:
		if(m_bConnect && !m_bIsConnected)
		{
			KillTimer(TIMER_ENSURE_VIEWER_IN_CHAIN);
			Connect();
		}
		else
		{
			SendPing();
		}
		break;

	case TIMER_DRAW_CLIPBOARD:
		{
			KillTimer(nIDEvent);
		
			DWORD dwNow = GetTickCount();

			if(dwNow - m_dwLastCopy > CGetSetOptions::m_dwSaveClipDelay || m_dwLastCopy > dwNow)
			{
				if (GetIgnoreClipboardChange() == false)				
				{
					Log(StrF(_T("OnDrawClipboard::OnTimer %d"), dwNow));

					m_pHandler->OnClipboardChange(m_activeWindow, m_activeWindowTitle);

					m_dwLastCopy = dwNow;
				}
			}
			else
			{
				Log(StrF(_T("Clip copy to fast difference from last copy = %d"), (dwNow - m_dwLastCopy)));
			}

			m_activeWindow = _T("");
			m_activeWindowTitle = _T("");
		}
		break;

	case TIMER_PING:
		KillTimer(TIMER_PING);

		//If we haven't received the change clipboard message then we are disconnected
		//if so reconnect
		if(m_bPinging)
		{
			if(m_bConnect)
			{
				Log(_T("Ping Failed Reconnecting to clipboard"));
				Disconnect(false);
				Connect();
			}
			else
			{
				Log(_T("Ping Failed but Connected set to FALSE so this is ok"));
			}
		}
		else
		{
			if(m_bConnect)
			{
				m_bIsConnected = true;
			}
		}

		break;
	}
	
	CWnd::OnTimer(nIDEvent);
}

LRESULT CClipboardViewer::OnSetConnect(WPARAM wParam, LPARAM lParam)
{
	bool bConnect = wParam == TRUE;
	SetConnect(bConnect);
	return TRUE;
}
