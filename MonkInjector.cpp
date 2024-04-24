#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <TlHelp32.h>
#include <commdlg.h>

using namespace std;

HWND g_hwnd, g_hButton, g_hEdit, g_hInjectButton, g_hDllLabel, g_hComboBox, g_hRadioProcess, g_hRadioWindow;
char g_szFileName[MAX_PATH] = "";
vector<pair<DWORD, string>> g_processes;
bool g_showProcesses = true;

void ListProcesses()
{
    g_processes.clear();
    PROCESSENTRY32 process_entry;
    process_entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return;

    if (Process32First(hSnapshot, &process_entry))
    {
        do
        {
            if (process_entry.th32ProcessID != 0)
            {
                string process_name = process_entry.szExeFile;
                g_processes.push_back(make_pair(process_entry.th32ProcessID, process_name));
            }
        } while (Process32Next(hSnapshot, &process_entry));
    }

    CloseHandle(hSnapshot);
}

void ListWindows()
{
    g_processes.clear();
    HWND hwnd = GetTopWindow(NULL);
    while (hwnd)
    {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (IsWindowVisible(hwnd) && pid != GetCurrentProcessId())
        {
            char window_title[MAX_PATH];
            GetWindowText(hwnd, window_title, MAX_PATH);
            string title(window_title);
            if (!title.empty())
                g_processes.push_back(make_pair(pid, title));
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }
}

void PopulateComboBox(HWND hwnd)
{
    SendMessage(g_hComboBox, CB_RESETCONTENT, 0, 0);

    for (auto& process : g_processes)
    {
        SendMessage(g_hComboBox, CB_ADDSTRING, 0, (LPARAM)process.second.c_str());
    }
}

void get_proc_id(DWORD selected_index, DWORD& process_id)
{
    if (selected_index >= g_processes.size())
    {
        process_id = 0;
        return;
    }
    process_id = g_processes[selected_index].first;
}

void error(const char* error_title, const char* error_message)
{
    MessageBox(NULL, error_message, error_title, MB_ICONERROR);
}

void OpenFileExplorer(HWND hwnd)
{
    OPENFILENAME ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Dynamic Link Libraries\0*.dll\0All Files\0*.*\0";
    ofn.lpstrFile = g_szFileName;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(g_szFileName);
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "dll";

    if (GetOpenFileName(&ofn))
    {
        SetWindowText(g_hDllLabel, g_szFileName);
    }
}

void InjectDLL(HWND hwnd)
{
    if (strlen(g_szFileName) == 0)
    {
        error("Error 0000x2", "Failed to find dll | No dll selected?");
        return;
    }

    DWORD selected_index = SendMessage(g_hComboBox, CB_GETCURSEL, 0, 0);
    DWORD proc_id = 0;
    get_proc_id(selected_index, proc_id);
    if (proc_id == 0)
    {
        error("Error 0000x3", "Failed to find process | No process selected?");
        return;
    }

    HANDLE h_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, proc_id);
    if (!h_process)
    {
        error("Error 0000x4", "Failed to open process | No permission?");
        return;
    }

    LPVOID allocated_memory = VirtualAllocEx(h_process, nullptr, MAX_PATH, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocated_memory)
    {
        error("Error 0000x5", "Failed to allocate memory in target process");
        CloseHandle(h_process);
        return;
    }

    if (!WriteProcessMemory(h_process, allocated_memory, g_szFileName, MAX_PATH, nullptr))
    {
        error("Error 0000x6", "Failed to write to process memory | No permission?");
        CloseHandle(h_process);
        VirtualFreeEx(h_process, allocated_memory, NULL, MEM_RELEASE);
        return;
    }

    HANDLE h_thread = CreateRemoteThread(h_process, nullptr, NULL, LPTHREAD_START_ROUTINE(LoadLibraryA), allocated_memory, NULL, nullptr);
    if (!h_thread)
    {
        error("Error 0000x7", "Failed to create remote thread");
        CloseHandle(h_process);
        VirtualFreeEx(h_process, allocated_memory, NULL, MEM_RELEASE);
        return;
    }

    CloseHandle(h_thread);
    CloseHandle(h_process);
    VirtualFreeEx(h_process, allocated_memory, NULL, MEM_RELEASE);

    MessageBox(hwnd, "DLL injected successfully!", "Success", MB_OK | MB_ICONINFORMATION);
}


void SwitchProcessWindowMode()
{
    if (g_showProcesses)
    {
        g_showProcesses = false;
        SendMessage(g_hRadioWindow, BM_SETCHECK, BST_CHECKED, 0);
        ListWindows();
        PopulateComboBox(g_hwnd);
    }
    else
    {
        g_showProcesses = true;
        SendMessage(g_hRadioProcess, BM_SETCHECK, BST_CHECKED, 0);
        ListProcesses();
        PopulateComboBox(g_hwnd);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        g_hButton = CreateWindow(TEXT("BUTTON"), TEXT("Select DLL"),
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            10, 10, 100, 30,
            hwnd, (HMENU)1, NULL, NULL);
        SendMessage(g_hButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));

        g_hDllLabel = CreateWindow(TEXT("STATIC"), TEXT("Selected DLL: "),
            WS_VISIBLE | WS_CHILD,
            120, 10, 150, 30,
            hwnd, NULL, NULL, NULL);
        SendMessage(g_hDllLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));

        g_hInjectButton = CreateWindow(TEXT("BUTTON"), TEXT("Inject DLL"),
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            10, 50, 100, 30,
            hwnd, (HMENU)2, NULL, NULL);
        SendMessage(g_hInjectButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));

        g_hRadioProcess = CreateWindow(TEXT("BUTTON"), TEXT("Processes"),
            WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
            20, 90, 80, 15,
            hwnd, (HMENU)3, NULL, NULL);
        SendMessage(g_hRadioProcess, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));
        SendMessage(g_hRadioProcess, BM_SETCHECK, BST_CHECKED, 0);

        g_hRadioWindow = CreateWindow(TEXT("BUTTON"), TEXT("Windows"),
            WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
            20, 105, 80, 15,
            hwnd, (HMENU)4, NULL, NULL);
        SendMessage(g_hRadioWindow, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));

        g_hComboBox = CreateWindow(TEXT("COMBOBOX"), TEXT(""),
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_AUTOHSCROLL | WS_VSCROLL,
            120, 50, 150, 200,
            hwnd, (HMENU)5, NULL, NULL);
        SendMessage(g_hComboBox, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));

        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1:
            OpenFileExplorer(hwnd);
            break;
        case 2:
            InjectDLL(hwnd);
            break;
        case 3:
            SwitchProcessWindowMode();
            break;
        case 4:
            SwitchProcessWindowMode();
            break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int main()
{
    const char* CLASS_NAME = "CalcFrame";

    ListProcesses();

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(150, 111, 51)); 
    wc.hIcon = (HICON)LoadImage(NULL, "icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);

    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "Monk Injector",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 165,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_hwnd == NULL)
    {
        return 0;
    }

    PopulateComboBox(g_hwnd);

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
