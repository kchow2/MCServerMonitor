#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"
#pragma comment(lib, "Winmm.lib")

NOTIFYICONDATA iconData;
HMENU popupMenu;
char readBuffer[32768];

#define MSG_TRAY_ICON WM_USER+1
#define ID_TRAY_ICON 1001
#define IDM_EXIT 1002

struct MCLOGFILE{	
	DWORD fileOffset;
	TCHAR fileName[MAX_PATH];
};

void createTrayIcon(HWND hwnd){
	ZeroMemory(&iconData, sizeof(iconData));
	iconData.hWnd = hwnd;
	iconData.uVersion = NOTIFYICON_VERSION_4;
	iconData.cbSize = NOTIFYICONDATA_V3_SIZE;
	iconData.uFlags = NIF_ICON | NIF_MESSAGE;
	iconData.uID = ID_TRAY_ICON;
	iconData.uCallbackMessage = MSG_TRAY_ICON;
	
	iconData.hIcon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON1));
	Shell_NotifyIcon(NIM_ADD, &iconData);
	Shell_NotifyIcon(NIM_SETVERSION, &iconData);
}

void sendTrayNotification(char* message){
	PlaySound(TEXT("SystemWelcome"), NULL, SND_SYSTEM|SND_ASYNC);

	iconData.uFlags |= NIF_INFO;
	MultiByteToWideChar(CP_OEMCP, 0, message, -1, iconData.szInfo, 256);
	wcscpy(iconData.szInfoTitle, TEXT("MC Server Monitor"));
	iconData.dwInfoFlags = NIIF_INFO;
	Shell_NotifyIcon(NIM_MODIFY, &iconData);
}

//checks MC log file for new entries since the last time it was read
//if logFile.hFile is NULL, it will open the file. Otherwise it will use the existing handle.
bool readMCLogFile(MCLOGFILE &logFile, char *readBuf, int readBufSize, int *bytesRead){
	char tmp[256];

	//open the log file
	HANDLE hFile = CreateFile(logFile.fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE){
		sprintf(tmp, "ERROR: Could not read file '%s'. (Error %d)\n", logFile.fileName, GetLastError());
		MessageBoxA(0, tmp, "ERROR", MB_ICONERROR);
		return false;
	}

	//determine whether the file has been modified since the last time we looked at it
	DWORD fileSize = GetFileSize(hFile, 0);
	if (fileSize < logFile.fileOffset){	//if the filesize is less than our current position in the file, it usually means the file has been deleted and recreated due to server starting up.
		//reset the fileOffset to 0 since the log was deleted and recreated.
		logFile.fileOffset = 0;
	}
	if (fileSize > logFile.fileOffset){
		DWORD bytesAvailable = fileSize - logFile.fileOffset;
		DWORD _bytesRead = 0;
		SetFilePointer(hFile, logFile.fileOffset, 0, FILE_BEGIN);
		BOOL readSucceeded = ReadFile(hFile, readBuffer, readBufSize - 1, &_bytesRead, 0);
		if (!readSucceeded){
			sprintf(tmp, "ReadFile() failed with error %d.", GetLastError());
			MessageBoxA(0, tmp, 0, 0);
			return false;
		}
		logFile.fileOffset = fileSize;	//update the fileOffset so we don't keep reading old data
		readBuffer[_bytesRead] = 0; //NULL terminate the string
		*bytesRead = _bytesRead;
	}
	else{
		readBuffer[0] = 0;
		*bytesRead = 0;
	}

	CloseHandle(hFile);

	return true;
}

bool handleInput(char *buf){
	char* ptr = strstr(buf, "]: ");
	//MessageBoxA(0, buf, "DATA", 0);
	if (ptr){
		char* msg = ptr + 3;

		if (strstr(msg, "joined the game") != 0){
			sendTrayNotification(msg);
		}
		if (strstr(msg, "left the game") != 0){
			sendTrayNotification(msg);
		}
		if (strstr(msg, "Starting minecraft server version") != 0){
			sendTrayNotification(msg);
		}
		if (strstr(msg, "Stopping server") != 0){
			sendTrayNotification(msg);
			return false;
		}

	}
	else{
		//printf("Read log entry with bad format!");
	}
	return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam){
	switch (message){
	case WM_CREATE:
		createTrayIcon(hwnd);
		popupMenu = CreatePopupMenu();
		AppendMenu(popupMenu, MF_DISABLED, 0, TEXT("MC Server Monitor"));
		AppendMenu(popupMenu, MF_SEPARATOR, 0, TEXT(""));
		AppendMenu(popupMenu, MF_STRING, IDM_EXIT, TEXT("Exit"));

		break;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		PostQuitMessage(0); 
		break;
	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &iconData);
		break;
	case MSG_TRAY_ICON:
		if (LOWORD(lparam) == WM_RBUTTONDOWN){
			
			POINT cursor;
			GetCursorPos(&cursor);
			SetForegroundWindow(hwnd);
			UINT clicked = TrackPopupMenu(popupMenu, TPM_RIGHTALIGN | TPM_RETURNCMD, cursor.x, cursor.y, NULL, hwnd, NULL);
			PostMessage(hwnd, WM_NULL, 0, 0);

			if (clicked == IDM_EXIT){
				DestroyWindow(hwnd);
				PostQuitMessage(0);
			}
		}
		break;
	default:
		return DefWindowProc(hwnd, message, wparam, lparam);
	}
	return 0;
}

DWORD WINAPI threadProc(LPVOID parameter){
	TCHAR fileName[] = TEXT("logs/latest.log");
	char currentDir[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, currentDir);
	char welcomeMessage[512];
	sprintf(welcomeMessage, "Starting up in directory '%s'.", currentDir);
	sendTrayNotification(welcomeMessage);

	bool shouldQuit = false;

	HANDLE hFile = CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE){
		char tmp[256];
		sprintf(tmp, "ERROR: Could not read file '%s'. (Error %d)\n", fileName, GetLastError());
		MessageBoxA(0, tmp, "ERROR", MB_ICONERROR);
		return 0;
	}
	DWORD fileSize = GetFileSize(hFile, 0);
	CloseHandle(hFile);

	MCLOGFILE logFile;
	wcscpy(logFile.fileName, fileName);
	logFile.fileOffset = fileSize;

	while (!shouldQuit)
	{
		int bytesRead = 0;
		readMCLogFile(logFile, readBuffer, 32768, &bytesRead);
		if (bytesRead > 0){
			char* linePtr = strtok(readBuffer, "\n");	//break input up into lines
			while (linePtr){
				if (!handleInput(linePtr)){
					//shouldQuit = true;
					break;
				}
				linePtr = strtok(NULL, "\n");
			}
		}
		Sleep(5000);	//file is read once per 5 seconds
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	WNDCLASSEX wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hIcon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON1));
	wc.hIconSm = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON1));
	wc.hInstance = GetModuleHandle(0);
	wc.lpszClassName = TEXT("MCSERVERMON");
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpfnWndProc = WndProc;
	RegisterClassEx(&wc);
	HWND hwnd = CreateWindowEx(0, TEXT("MCSERVERMON"), TEXT("Dummy"), 0, 100,100,100,100,HWND_DESKTOP,0,GetModuleHandle(0), 0);//WS_VISIBLE|WS_SYSMENU

	DWORD threadId;
	HANDLE hThread = CreateThread(0, 0, threadProc, 0, 0, &threadId);
	if (hThread == INVALID_HANDLE_VALUE){
		printf("Couldn't create the thread. Error %d", GetLastError());
	}

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0) > 0){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	TerminateThread(hThread, 0);
	
	Shell_NotifyIcon(NIM_DELETE, &iconData);	//remove the icon when our program terminates

	return 0;
}

