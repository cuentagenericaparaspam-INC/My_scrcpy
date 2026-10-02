#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

#define IDC_DEVICE 1001
#define IDC_REFRESH 1002
#define IDC_START 1003
#define IDC_STOP 1004
#define IDC_SETTINGS 1005
#define IDC_RESOLUTION 1006
#define IDC_FPS 1007
#define IDC_BITRATE 1008
#define IDC_CODEC 1009
#define IDC_AUDIO 1010
#define IDC_STAY 1011
#define IDC_TURNOFF 1012
#define IDC_FULLSCREEN 1013
#define IDC_STATUS 1014
#define IDC_SETUP 1015

static HWND hDevice, hResolution, hFps, hBitrate, hCodec;
static HWND hAudio, hStay, hTurnOff, hFullscreen, hStatus;
static PROCESS_INFORMATION scrcpy_pi = {0};

static void set_status(const wchar_t *s) {
    SetWindowTextW(hStatus, s);
}

static BOOL run_hidden_capture(const wchar_t *cmd, wchar_t *out, DWORD out_chars) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE read_pipe = NULL, write_pipe = NULL;
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    wchar_t *mutable_cmd = _wcsdup(cmd);
    DWORD read = 0, total = 0;
    BOOL ok = FALSE;

    if (!mutable_cmd) return FALSE;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) goto cleanup;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessW(NULL, mutable_cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) goto cleanup;

    CloseHandle(write_pipe);
    write_pipe = NULL;

    while (total + 1 < out_chars) {
        if (!ReadFile(read_pipe, out + total, (out_chars - total) - 1, &read, NULL) || read == 0)
            break;
        total += read;
    }
    out[total] = L'\0';
    WaitForSingleObject(pi.hProcess, 3000);
    ok = TRUE;

cleanup:
    if (write_pipe) CloseHandle(write_pipe);
    if (read_pipe) CloseHandle(read_pipe);
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    free(mutable_cmd);
    return ok;
}

static void refresh_devices(void) {
    wchar_t exe[MAX_PATH], cmd[1024], buffer[8192];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *(slash + 1) = L'\0';
    swprintf(cmd, 1024, L"\"%sadb.exe\" devices", exe);
    SendMessageW(hDevice, CB_RESETCONTENT, 0, 0);

    if (!run_hidden_capture(cmd, buffer, 8192)) {
        set_status(L"No se pudo ejecutar ADB. Coloca adb.exe junto a PocoMirror.exe.");
        return;
    }

    wchar_t *ctx = NULL;
    wchar_t *line = wcstok_s(buffer, L"\r\n", &ctx);
    int found = 0;
    while (line) {
        if (wcsstr(line, L"\tdevice")) {
            wchar_t serial[256] = {0};
            swscanf(line, L"%255s", serial);
            if (serial[0]) {
                SendMessageW(hDevice, CB_ADDSTRING, 0, (LPARAM)serial);
                found++;
            }
        }
        line = wcstok_s(NULL, L"\r\n", &ctx);
    }

    if (found) {
        SendMessageW(hDevice, CB_SETCURSEL, 0, 0);
        set_status(L"Dispositivo ADB listo.");
    } else {
        set_status(L"No hay un dispositivo autorizado. Activa Depuración USB y acepta la autorización RSA.");
    }
}

static BOOL start_scrcpy(void) {
    if (scrcpy_pi.hProcess) return TRUE;

    wchar_t dir[MAX_PATH], cmd[4096], serial[256], resolution[32], fps[32], bitrate[32], codec[32];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) *(slash + 1) = L'\0';

    SendMessageW(hDevice, WM_GETTEXT, 255, (LPARAM)serial);
    SendMessageW(hResolution, WM_GETTEXT, 31, (LPARAM)resolution);
    SendMessageW(hFps, WM_GETTEXT, 31, (LPARAM)fps);
    SendMessageW(hBitrate, WM_GETTEXT, 31, (LPARAM)bitrate);
    SendMessageW(hCodec, WM_GETTEXT, 31, (LPARAM)codec);

    BOOL audio = SendMessageW(hAudio, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL stay = SendMessageW(hStay, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL turnoff = SendMessageW(hTurnOff, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL fullscreen = SendMessageW(hFullscreen, BM_GETCHECK, 0, 0) == BST_CHECKED;

    swprintf(cmd, 4096,
        L"\"%sscrcpy.exe\" --serial=\"%s\" --max-size=%s --max-fps=%s --video-bit-rate=%s --video-codec=%s%s%s%s%s",
        dir, serial, resolution, fps, bitrate, codec,
        audio ? L"" : L" --no-audio",
        stay ? L" --stay-awake" : L"",
        turnoff ? L" --turn-screen-off" : L"",
        fullscreen ? L" --fullscreen" : L"");

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t *mutable_cmd = _wcsdup(cmd);
    if (!mutable_cmd) return FALSE;

    BOOL ok = CreateProcessW(NULL, mutable_cmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, dir, &si, &scrcpy_pi);
    free(mutable_cmd);

    if (!ok) {
        set_status(L"No se pudo iniciar scrcpy. Verifica que scrcpy.exe y sus DLL estén junto al launcher.");
        ZeroMemory(&scrcpy_pi, sizeof(scrcpy_pi));
        return FALSE;
    }

    set_status(L"Espejo iniciado.");
    return TRUE;
}

static void stop_scrcpy(void) {
    if (scrcpy_pi.hProcess) {
        TerminateProcess(scrcpy_pi.hProcess, 0);
        CloseHandle(scrcpy_pi.hThread);
        CloseHandle(scrcpy_pi.hProcess);
        ZeroMemory(&scrcpy_pi, sizeof(scrcpy_pi));
        set_status(L"Espejo detenido.");
    }
}

static void setup_message(HWND hwnd) {
    MessageBoxW(hwnd,
        L"Configuración inicial para Poco F6\n\n"
        L"1. Activa Opciones de desarrollador.\n"
        L"2. Activa Depuración USB.\n"
        L"3. Conecta el teléfono por USB.\n"
        L"4. Acepta la clave RSA en el teléfono.\n\n"
        L"En Xiaomi/POCO puede ser necesario activar "
        L"Depuración USB (ajustes de seguridad)\n"
        L"para control por teclado, ratón o gamepad. "
        L"Android no permite que este programa active "
        L"silenciosamente esos permisos protegidos.",
        L"Asistente de configuración", MB_OK | MB_ICONINFORMATION);
}

static HWND label(HWND parent, const wchar_t *text, int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, NULL, NULL, NULL);
}

static HWND combo(HWND parent, int id, int x, int y, int w, const wchar_t *initial) {
    HWND c = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        x, y, w, 120, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)initial);
    SendMessageW(c, CB_SETCURSEL, 0, 0);
    return c;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        label(hwnd, L"PocoMirror — prueba para POCO F6", 20, 15, 420, 30);
        label(hwnd, L"Dispositivo", 20, 55, 90, 22);
        hDevice = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST,
            110, 52, 250, 140, hwnd, (HMENU)IDC_DEVICE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Actualizar", WS_CHILD | WS_VISIBLE,
            370, 51, 100, 26, hwnd, (HMENU)IDC_REFRESH, NULL, NULL);

        label(hwnd, L"Resolución máxima", 20, 95, 130, 22);
        hResolution = combo(hwnd, IDC_RESOLUTION, 155, 92, 100, L"1920");
        label(hwnd, L"FPS", 275, 95, 35, 22);
        hFps = combo(hwnd, IDC_FPS, 310, 92, 70, L"60");
        label(hwnd, L"Bitrate", 400, 95, 55, 22);
        hBitrate = combo(hwnd, IDC_BITRATE, 455, 92, 100, L"16M");

        label(hwnd, L"Codec", 20, 135, 60, 22);
        hCodec = combo(hwnd, IDC_CODEC, 85, 132, 110, L"h264");
        SendMessageW(hCodec, CB_ADDSTRING, 0, (LPARAM)L"h265");
        SendMessageW(hCodec, CB_ADDSTRING, 0, (LPARAM)L"av1");

        hAudio = CreateWindowW(L"BUTTON", L"Audio", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            220, 132, 80, 22, hwnd, (HMENU)IDC_AUDIO, NULL, NULL);
        SendMessageW(hAudio, BM_SETCHECK, BST_CHECKED, 0);
        hStay = CreateWindowW(L"BUTTON", L"Mantener activo", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            305, 132, 125, 22, hwnd, (HMENU)IDC_STAY, NULL, NULL);
        hTurnOff = CreateWindowW(L"BUTTON", L"Apagar pantalla", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            435, 132, 120, 22, hwnd, (HMENU)IDC_TURNOFF, NULL, NULL);
        hFullscreen = CreateWindowW(L"BUTTON", L"Pantalla completa", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, 170, 135, 22, hwnd, (HMENU)IDC_FULLSCREEN, NULL, NULL);

        CreateWindowW(L"BUTTON", L"Iniciar espejo", WS_CHILD | WS_VISIBLE,
            20, 210, 135, 34, hwnd, (HMENU)IDC_START, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Detener", WS_CHILD | WS_VISIBLE,
            165, 210, 100, 34, hwnd, (HMENU)IDC_STOP, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Configuración inicial", WS_CHILD | WS_VISIBLE,
            275, 210, 150, 34, hwnd, (HMENU)IDC_SETUP, NULL, NULL);

        hStatus = CreateWindowW(L"STATIC", L"Comprobando ADB...",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 260, 520, 40,
            hwnd, (HMENU)IDC_STATUS, NULL, NULL);
        refresh_devices();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH: refresh_devices(); break;
        case IDC_START:
            if (SendMessageW(hDevice, CB_GETCOUNT, 0, 0) > 0) start_scrcpy();
            else set_status(L"Primero conecta y autoriza el POCO F6.");
            break;
        case IDC_STOP: stop_scrcpy(); break;
        case IDC_SETUP: setup_message(hwnd); break;
        }
        return 0;

    case WM_DESTROY:
        stop_scrcpy();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmd, int show) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PocoMirrorWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"PocoMirrorWindow", L"PocoMirror",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 590, 350,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
