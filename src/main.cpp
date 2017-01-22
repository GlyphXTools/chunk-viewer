#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include "resource.h"
#include <afxres.h>
#include <tchar.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include "ChunkFile.h"
#include "Exceptions.h"

using namespace std;

typedef basic_string<TCHAR> tstring;
typedef basic_stringstream<TCHAR> tstringstream;

// Global info about the applications
struct ApplicationInfo
{
	HINSTANCE hInstance;

	HWND hMainWnd;
	HWND hNodeTree;
	HWND hNodeInfo;

	HWND hWidthLabel;
	HWND hWidthUpDown;
	HWND hWidthEdit;

    tstring filename;

	vector<pair<int, char*> > nodeData;
};

static bool IsProbablyMiniChunk(int size, char* data)
{
	while (size > 2 && data[1] != 0)
	{
		int msize = (unsigned char)data[1] + 2;
		size -= msize;
		data += msize;
	}
	return (size == 0);
}

static void RemoveChildren( HWND hTree, HTREEITEM hItem)
{
    HTREEITEM hChild;
	while ((hChild = TreeView_GetChild(hTree, hItem)) != NULL)
	{
        RemoveChildren(hTree, hChild);
        TreeView_DeleteItem(hTree, hChild);
    }
}

static void SetNodes( vector<pair<int, char*> >& nodeData, HWND hTree, HTREEITEM hParent, File* input)
{
    HTREEITEM hItem = TreeView_GetChild(hTree, hParent);
	while (!input->eof())
	{
		Chunk chunk(input);
		unsigned long start = input->tell();

		tstringstream str;
		str << hex << setw(8) << setfill(TEXT('0')) << chunk.getType() << "h (" << setw(8) << chunk.getSize() << "h)";
		tstring title = str.str();

        TVINSERTSTRUCT item;
	    item.item.mask      = TVIF_CHILDREN | TVIF_TEXT | TVIF_PARAM;
        item.item.hItem     = hItem;
	    item.item.cChildren = (chunk.isGroup() ? 1 : 0);
	    item.item.pszText   = const_cast<tstring::value_type*>(title.c_str());
	    item.item.lParam    = (chunk.isGroup() ? -1 : nodeData.size());
        
        if (hItem == NULL)
        {
            // Add it
		    item.hParent        = hParent;
		    item.hInsertAfter   = TVI_LAST;
		    hItem = TreeView_InsertItem(hTree, &item);
        }
        else
        {
            // Edit it
		    if (!chunk.isGroup())
		    {   
                // Remove all the node's children
                RemoveChildren(hTree, hItem);
            }
            TreeView_SetItem(hTree, &item.item);
        }

		if (chunk.isGroup())
		{
			SetNodes(nodeData, hTree, hItem, chunk.getStream() );
		}
		else
		{
            // Set the node data
			int   size = chunk.getSize();
			char* data = chunk.getData();
			if (size < 0x1000 && IsProbablyMiniChunk(size, data))
			{
				size = -size;
			}
			nodeData.push_back( make_pair(size, data) );
		}

		input->seek( start + chunk.getSize() );

        hItem = TreeView_GetNextSibling(hTree, hItem);
	}

    // Remove any trailing nodes
    while (hItem != NULL)
    {
        RemoveChildren(hTree, hItem);
        HTREEITEM hNext = TreeView_GetNextSibling(hTree, hItem);
        TreeView_DeleteItem(hTree, hItem);
        hItem = hNext;
    }
}

static void FillNodeTree( ApplicationInfo* info, File* file )
{
	// Clear previous
	for (vector<pair<int, char*> >::iterator i = info->nodeData.begin(); i != info->nodeData.end(); i++)
	{
		delete[] i->second;
	}
	info->nodeData.clear();

    HTREEITEM hRoot = TreeView_GetRoot(info->hNodeTree);
    if (hRoot == NULL)
    {
	    TVINSERTSTRUCT item;
	    item.hParent        = NULL;
	    item.hInsertAfter   = TVI_ROOT;
	    item.item.mask      = TVIF_CHILDREN | TVIF_TEXT | TVIF_PARAM;
	    item.item.cChildren = 1;
	    item.item.pszText   = TEXT("Chunk File");
	    item.item.lParam    = -1;        
    	hRoot = TreeView_InsertItem(info->hNodeTree, &item);
    }
	SetNodes(info->nodeData, info->hNodeTree, hRoot, file);
	TreeView_Expand( info->hNodeTree, hRoot, TVE_EXPAND);
}

static tstring FormatNodeInfo( int size, char* data, int width, const char* prefix = "")
{
	tstringstream str;
	str << hex;
	for (int i = 0; i < size; i++)
	{
		if (i % width == 0) str << prefix;
		str << setw(2) << setfill(TEXT('0')) << (int)(unsigned char)data[i] << " ";
		if (i % width == width - 1 || i == size - 1)
		{
			for (int k = i; k % width < width - 1; k++) str << "   ";
			str << " | ";
			for (int j = (i / width) * width; j <= i; j++)
			{
				str << (isprint((unsigned char)data[j]) ? data[j] : '.');
			}
			str << "\r\n";
		}
	}
	return str.str();
}

static void SetNodeInfo( HWND hWnd, int size, char* data, int width = 16 )
{
	width = max(1, width);

	tstring text;
	if (size < 0)
	{
		// Parse as mini-chunks
		size = -size;
		tstringstream str;
		while (size > 0)
		{
			int type  = (unsigned char)data[0];
			int msize = (unsigned char)data[1];
			tstring chunk = FormatNodeInfo(msize, data + 2, width, "       ");
			if (!chunk.empty()) chunk = chunk.substr(7);
			str << hex << setw(2) << setfill(TEXT('0')) << type  << " "
				       << setw(2) << setfill(TEXT('0')) << msize << ": " << chunk;
			size -= msize + 2;
			data += msize + 2;
		}
		text = str.str();
	}
	else
	{
		text = FormatNodeInfo(size, data, width);
	}
	SetWindowText(hWnd, text.c_str());
}

static void OnNodeSelected(ApplicationInfo* info, int index)
{
    if (index >= 0 && (size_t)index < info->nodeData.size())
    {
	    int   size  = info->nodeData[ index ].first;
	    char* data  = info->nodeData[ index ].second;
	    BOOL  error;
	    int   width = (int)SendMessage(info->hWidthUpDown, UDM_GETPOS32, 0, (LPARAM)&error);
	    if (error) width = 16;

	    SetNodeInfo( info->hNodeInfo, size, data, width );
    }
    else
    {
	    SetWindowText(info->hNodeInfo, TEXT(""));
    }
}

static void DlgOpenFile( ApplicationInfo *info )
{
	try
	{
		TCHAR filename[MAX_PATH];
		filename[0] = TEXT('\0');

		OPENFILENAME ofn;
		memset(&ofn, 0, sizeof(OPENFILENAME));
		ofn.lStructSize  = sizeof(OPENFILENAME);
		ofn.hwndOwner    = info->hMainWnd;
		ofn.hInstance    = info->hInstance;
		ofn.lpstrFilter  = TEXT("Alamo Chunk Files (*.alo, *.ala, *.ted, *.tem, *.bui, *.rec)\0*.ALO; *.ALA; *.TED; *.TEM; *.BUI; *.REC\0All Files (*.*)\0*.*\0\0");
		ofn.nFilterIndex = 0;
		ofn.lpstrFile    = filename;
		ofn.nMaxFile     = MAX_PATH;
		ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		if (GetOpenFileName( &ofn ) != 0)
		{
			// Clear previous
            info->filename = filename;
			File* file = new PhysicalFile(info->filename);
    	    SetWindowText(info->hNodeInfo, TEXT(""));
		    FillNodeTree( info, file );
            EnableMenuItem(GetMenu(info->hMainWnd), ID_FILE_REFRESH, MF_BYCOMMAND | MF_ENABLED);
            SetFocus(info->hNodeTree);
			SAFE_RELEASE(file);
		}
	}
	catch (exception&)
	{
		MessageBox(NULL, TEXT("Unable to open the specified file"), NULL, MB_OK | MB_ICONHAND );
	}
}

static void RefreshFile(ApplicationInfo* info)
{
    try
    {
        File* file = new PhysicalFile(info->filename);
        FillNodeTree( info, file );
        SAFE_RELEASE(file);

        // Refresh selection
        TVITEM item;
        if ((item.hItem = TreeView_GetSelection(info->hNodeTree)) != NULL)
        {
            item.mask  = TVIF_PARAM;
            TreeView_GetItem(info->hNodeTree, &item);
            OnNodeSelected(info, (int)item.lParam);
        }
    }
    catch (exception&)
    {
        MessageBox(NULL, TEXT("Unable to refresh the opened file"), NULL, MB_OK | MB_ICONHAND );
    }
}

static void DoSelectAll()
{
    HWND hFocus = GetFocus();
    TCHAR classname[256];
    GetClassName(hFocus, classname, 256);
    if (_tcscmp(classname, TEXT("Edit")) == 0)
    {
        // Select all text
        SendMessage(hFocus, EM_SETSEL, 0, -1);
    }
}

static LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ApplicationInfo* info = (ApplicationInfo*)(LONG_PTR)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (uMsg)
	{
        case WM_CREATE:
        {
            CREATESTRUCT* pcs = (CREATESTRUCT*)lParam;
            info = (ApplicationInfo*)pcs->lpCreateParams;
		    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)info );

		    RECT client;
		    GetClientRect(hWnd, &client);

		    if ((info->hNodeTree = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, TEXT(""),
                WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT,
			    0, 0, 400, client.bottom, hWnd, NULL, pcs->hInstance, NULL)) == NULL) return -1;

		    if ((info->hWidthLabel = CreateWindow(TEXT("STATIC"), TEXT("Width:"),
                WS_CHILD | WS_VISIBLE,
			    410, 6, 40, 12, hWnd, NULL, pcs->hInstance, NULL)) == NULL) return -1;

		    if ((info->hWidthEdit = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
                WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_NUMBER,
			    450, 4, 75, 20, hWnd, NULL, pcs->hInstance, NULL)) == NULL) return -1;

		    if ((info->hWidthUpDown = CreateWindow(UPDOWN_CLASS, TEXT(""),
                WS_CHILD | WS_VISIBLE | UDS_NOTHOUSANDS | UDS_SETBUDDYINT | UDS_AUTOBUDDY | UDS_ARROWKEYS | UDS_ALIGNRIGHT,
			    400, 0, 10, 10, hWnd, NULL, pcs->hInstance, NULL)) == NULL) return -1;

		    if ((info->hNodeInfo = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
			    400, 30, client.right - 600, client.bottom, hWnd, NULL, pcs->hInstance, NULL)) == NULL) return -1;

		    SendMessage(info->hWidthUpDown, UDM_SETRANGE32, (WPARAM)1, (LPARAM)INT_MAX);
		    SendMessage(info->hWidthUpDown, UDM_SETPOS32,   0, (LPARAM)16);

		    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		    SendMessage(info->hNodeTree,   WM_SETFONT, (WPARAM)hFont, FALSE);
		    SendMessage(info->hWidthLabel, WM_SETFONT, (WPARAM)hFont, FALSE);
		    SendMessage(info->hWidthEdit,  WM_SETFONT, (WPARAM)hFont, FALSE);
		    SendMessage(info->hNodeInfo,   WM_SETFONT, (WPARAM)GetStockObject(OEM_FIXED_FONT), FALSE);
            break;
        }

        case WM_SETFOCUS:
            SetFocus(info->hNodeTree);
            break;

		case WM_COMMAND:
			if (lParam == 0)
			{
				// Menu or accelerator
				switch (LOWORD(wParam))
				{
					case ID_FILE_OPEN:
						if (info != NULL)
						{
							DlgOpenFile(info);
						}
						break;

					case ID_FILE_REFRESH:
						if (info != NULL)
						{
							RefreshFile(info);
						}
						break;

					case ID_FILE_EXIT:
						PostQuitMessage(0);
						break;

                    case ID_EDIT_COPY:
                        SendMessage(GetFocus(), WM_COPY, 0, 0);
                        break;
                    
                    case ID_EDIT_SELECTALL:
                        DoSelectAll();
                        break;

                    case ID_HELP_ABOUT:
                        MessageBox(hWnd, TEXT("Alamo Chunk Viewer 1.0\n\nBy Mike Lankamp"), TEXT("About"), MB_OK);
                        break;
				}
			}
			else if (info != NULL)
			{
				// Control
				switch (HIWORD(wParam))
				{
					case EN_CHANGE:
					{
						TCHAR strWidth[32];
						GetWindowText( info->hWidthEdit, strWidth, 32 );

						TVITEM item;
						item.mask  = TVIF_PARAM;
						item.hItem = TreeView_GetSelection(info->hNodeTree);
						TreeView_GetItem(info->hNodeTree, &item);

						if (item.hItem != NULL && item.lParam != -1)
						{
							int   width = _tcstoul(strWidth, 0, NULL);
							int   size  = info->nodeData[ item.lParam ].first;
							char* data  = info->nodeData[ item.lParam ].second;

							SetNodeInfo( info->hNodeInfo, size, data, width );
						}
						break;
					}
				}
			}
			break;

		case WM_NOTIFY:
			if (info != NULL)
			{
				NMHDR* nmhdr = (NMHDR*)lParam;
				switch (nmhdr->code)
				{
					case TVN_SELCHANGED:
					{
                        NMTREEVIEW* pnmtv = (NMTREEVIEW*)lParam;
                        OnNodeSelected(info, (int)pnmtv->itemNew.lParam);
						break;
					}

					case UDN_DELTAPOS:
					{
						NM_UPDOWN* nmud = (NM_UPDOWN*)nmhdr;
						int width = nmud->iPos + nmud->iDelta;

						TVITEM item;
						item.mask  = TVIF_PARAM;
						item.hItem = TreeView_GetSelection(info->hNodeTree);
						TreeView_GetItem(info->hNodeTree, &item);
						if (item.hItem != NULL && item.lParam != -1)
						{
							int   size = info->nodeData[ item.lParam ].first;
							char* data = info->nodeData[ item.lParam ].second;
							SetNodeInfo( info->hNodeInfo, size, data, width );
						}
						break;
					}
				}
			}
			break;

		case WM_SIZE:
			if (info != NULL)
			{
				RECT client;
				GetClientRect(info->hMainWnd, &client);
				MoveWindow(info->hNodeTree,   0,  0, 400, client.bottom, TRUE);
				MoveWindow(info->hNodeInfo, 400, 30, client.right - 400, client.bottom - 30, TRUE);
			}
			break;

		case WM_SIZING:
		{
			const int MIN_WIDTH  = 750;
			const int MIN_HEIGHT = 300;

			RECT* rect = (RECT*)lParam;
			bool left  = (wParam == WMSZ_BOTTOMLEFT) || (wParam == WMSZ_LEFT) || (wParam == WMSZ_TOPLEFT);
			bool top   = (wParam == WMSZ_TOPLEFT)    || (wParam == WMSZ_TOP)  || (wParam == WMSZ_TOPRIGHT);
			if (rect->right - rect->left < MIN_WIDTH)
			{
				if (left) rect->left  = rect->right - MIN_WIDTH;
				else      rect->right = rect->left  + MIN_WIDTH;
			}
			if (rect->bottom - rect->top < MIN_HEIGHT)
			{
				if (top) rect->top    = rect->bottom - MIN_HEIGHT;
				else     rect->bottom = rect->top    + MIN_HEIGHT;
			}
			break;
		}

		case WM_CLOSE:
			PostQuitMessage(0);
			break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void main( ApplicationInfo* info )
{
	ShowWindow(info->hMainWnd, SW_SHOW);

	HACCEL hAccel = LoadAccelerators( info->hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1));
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) != 0)
	{
		if (!TranslateAccelerator(info->hMainWnd, hAccel, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

// Create the main window and its child windows
static void CreateMainWindow( ApplicationInfo* info )
{
	WNDCLASSEX wcx;
	wcx.cbSize        = sizeof wcx;
    wcx.style		  = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc	  = MainWindowProc;
    wcx.cbClsExtra	  = 0;
    wcx.cbWndExtra    = 0;
    wcx.hInstance     = info->hInstance;
    wcx.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcx.lpszMenuName  = MAKEINTRESOURCE(IDR_MENU1);
    wcx.lpszClassName = TEXT("ChunkViewer");
    wcx.hIconSm       = NULL;

	if (RegisterClassEx(&wcx) == 0)
	{
		throw runtime_error("Unable to register window class");
	}

	if ((info->hMainWnd = CreateWindowEx(0, TEXT("ChunkViewer"), TEXT("Chunk File Viewer"), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, info->hInstance, info)) == NULL)
    {
        UnregisterClass(TEXT("ChunkViewer"), info->hInstance);
		throw runtime_error("Unable to create main window");
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	try
	{
		ApplicationInfo info;
		info.hInstance = hInstance;
		CreateMainWindow( &info );
		main( &info );
	}
	catch (exception& e)
	{
		MessageBoxA(NULL, e.what(), NULL, MB_OK );
	}
	return 0;
}