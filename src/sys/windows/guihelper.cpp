
#include "include/sys/windows/guihelper.h"

#include <QWidget>

#include <windows.h>
#include <shlobj.h>

void Windows_QWidget_SetForegroundWindow(QWidget *w) {
    HWND hForgroundWnd = GetForegroundWindow();
    DWORD dwForeID = ::GetWindowThreadProcessId(hForgroundWnd, NULL);
    DWORD dwCurID = ::GetCurrentThreadId();
    const bool attach = dwForeID != 0 && dwForeID != dwCurID &&
                        AttachThreadInput(dwCurID, dwForeID, TRUE);
    SetForegroundWindow((HWND) w->winId());
    if (attach) AttachThreadInput(dwCurID, dwForeID, FALSE);
}

int isThisAdmin = -1;

bool Windows_IsInAdmin() {
    if (isThisAdmin >= 0) return isThisAdmin;
    isThisAdmin = IsUserAnAdmin();
    return isThisAdmin;
}
