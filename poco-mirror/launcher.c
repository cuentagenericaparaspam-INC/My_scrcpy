#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

#define IDC_DEVICE      1001
#define IDC_REFRESH     1002
#define IDC_START       1003
#define IDC_STOP        1004
#define IDC_SETTINGS    1005
#define IDC_RESOLUTION  1006
#define IDC_FPS         1007
#define IDC_BITRATE     1008
#define IDC_CODEC       1009
#define IDC_AUDIO       1010
#define IDC_STAY        1011
#define IDC_TURNOFF     1012
#define IDC_FULLSCREEN  1013
#define IDC_STATUS      1014
#define IDC_SETUP       1015

#define IDR_RUNTIME_ZIP 3001
#define RUNTIME_VERSION L"v4.1"

static HWND hDevice, hResolution, hFps, hBitrate, hCodec;
static HWND hAudio, hStay, hTurnOff, hFullscreen, hStatus;
static PROCESS_INFORMATION scrcpy_pi = {0};
static wchar_t runtime_dir[MAX_PATH] = {0};

static void set_status(const wchar_t *s) {
    SetWindowTextW(hStatus, s);
}

static BOOL file_exists(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL ensure_dir(const wchar_t *path) {
    if (CreateDirectoryW(path, NULL)) return TRUE;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static void get_app_dir(wchar_t *out, size_t out_count) {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        out[0] = L'\0';
        return;
    }
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcsncpy_s(out, out_count, exe, _TRUNCATE);
}

static BOOL get_runtime_dir(wchar_t *out, size_t out_count) {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return FALSE;

    _snwprintf_s(out, out_count, _TRUNCATE,
                 L"%s\\PocoMirror\\runtime-%s", base, RUNTIME_VERSION);
    return TRUE;
}

static void ps_quote(const wchar_t *in, wchar_t *out, size_t out_count) {
    size_t j = 0;
    if (out_count == 0) return;

    out[j++] = 39;
    for (size_t i = 0; in[i] && j + 2 < out_count; ++i) {
        if (in[i] == 39) out[j++] = 39;
        out[j++] = in[i];
    }
    if (j + 1 < out_count) out[j++] = 39;
    out[j] = 0;
}

static BOOL run_hidden_capture(const wchar_t *cmd, char *out, DWORD out_chars, DWORD *exit_code) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE read_pipe = NULL, write_pipe = NULL;
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    wchar_t *mutable_cmd = _wcsdup(cmd);
    DWORD read = 0, total = 0;
    BOOL ok = FALSE;

    if (out_chars > 0) out[0] = '\\0';
    if (exit_code) *exit_code = (DWORD)-1;
    if (!mutable_cmd) return FALSE;

    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) goto cleanup;
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) goto cleanup;

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessW(NULL, mutable_cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        goto cleanup;
    }

    CloseHandle(write_pipe);
    write_pipe = NULL;

    while (total + 1 < out_chars) {
        if (!ReadFile(read_pipe, out + total, (out_chars - total) - 1, &read, NULL) || read == 0)
            break;
        total += read;
    }
    out[total] = '\\0';

    WaitForSingleObject(pi.hProcess, 5000);
    if (exit_code) GetExitCodeProcess(pi.hProcess, exit_code);
    ok = TRUE;

cleanup:
    if (write_pipe) CloseHandle(write_pipe);
    if (read_pipe) CloseHandle(read_pipe);
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    free(mutable_cmd);
    return ok;
}

static void write_diagnostic_log(const char *adb_output, DWORD exit_code) {
    wchar_t base[MAX_PATH], path[MAX_PATH];
    if (get_runtime_dir(base, MAX_PATH)) {
        if (ensure_dir(base)) {
            _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\PocoMirror-adb.log", base);
            FILE *f = _wfopen(path, L"wb");
            if (f) {
                fprintf(f, "adb exit code: %lu\\r\\n", (unsigned long)exit_code);
                fprintf(f, "%s", adb_output ? adb_output : "");
                fclose(f);
            }
        }
    }
}

static BOOL extract_embedded_runtime(void) {
    wchar_t zip_path[MAX_PATH], ps_zip[MAX_PATH], ps_dest[MAX_PATH], cmd[4096];
    HRSRC resource;
    HGLOBAL loaded;
    DWORD size;
    const void *data;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    DWORD exit_code = (DWORD)-1;

    if (!get_runtime_dir(runtime_dir, MAX_PATH)) {
        set_status(L"No se pudo preparar la carpeta interna de PocoMirror.");
        return FALSE;
    }

    wchar_t parent[MAX_PATH];
    wcsncpy_s(parent, MAX_PATH, runtime_dir, _TRUNCATE);
    wchar_t *slash = wcsrchr(parent, L'\\');
    if (slash) {
        *slash = L'\0';
        if (!ensure_dir(parent) && GetFileAttributesW(parent) == INVALID_FILE_ATTRIBUTES) {
            set_status(L"No se pudo crear la carpeta de datos de PocoMirror.");
            return FALSE;
        }
    }

    if (!ensure_dir(runtime_dir) && GetFileAttributesW(runtime_dir) == INVALID_FILE_ATTRIBUTES) {
        set_status(L"No se pudo crear la carpeta interna del runtime.");
        return FALSE;
    }

    wchar_t adb_path[MAX_PATH], scrcpy_path[MAX_PATH];
    _snwprintf_s(adb_path, MAX_PATH, _TRUNCATE, L"%s\\adb.exe", runtime_dir);
    _snwprintf_s(scrcpy_path, MAX_PATH, _TRUNCATE, L"%s\\scrcpy.exe", runtime_dir);
    if (file_exists(adb_path) && file_exists(scrcpy_path)) return TRUE;

    resource = FindResourceW((HINSTANCE)GetModuleHandleW(NULL),
                             MAKEINTRESOURCEW(IDR_RUNTIME_ZIP), RT_RCDATA);
    if (!resource) {
        set_status(L"No se encontró el paquete interno de scrcpy.");
        return FALSE;
    }

    loaded = LoadResource(NULL, resource);
    if (!loaded) {
        set_status(L"No se pudo cargar el paquete interno de scrcpy.");
        return FALSE;
    }

    size = SizeofResource(NULL, resource);
    data = LockResource(loaded);
    if (!data || size == 0) {
        set_status(L"El paquete interno de scrcpy está vacío.");
        return FALSE;
    }

    _snwprintf_s(zip_path, MAX_PATH, _TRUNCATE, L"%s\\runtime.zip", runtime_dir);
    file = CreateFileW(zip_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_status(L"No se pudo preparar el runtime interno. Revisa permisos de la carpeta de usuario.");
        return FALSE;
    }

    if (!WriteFile(file, data, size, &written, NULL) || written != size) {
        CloseHandle(file);
        DeleteFileW(zip_path);
        set_status(L"No se pudo copiar el runtime interno.");
        return FALSE;
    }
    CloseHandle(file);

    ps_quote(zip_path, ps_zip, MAX_PATH);
    ps_quote(runtime_dir, ps_dest, MAX_PATH);
    _snwprintf_s(
        cmd, 4096, _TRUNCATE,
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
        L"-Command \"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath %s -DestinationPath %s -Force\"",
        ps_zip, ps_dest);

    {
        char ps_output[4096];
        if (!run_hidden_capture(cmd, ps_output, sizeof(ps_output), &exit_code) || exit_code != 0) {
            DeleteFileW(zip_path);
            set_status(L"No se pudo extraer el runtime interno de scrcpy.");
            return FALSE;
        }
    }

    DeleteFileW(zip_path);

    if (!file_exists(adb_path) || !file_exists(scrcpy_path)) {
        set_status(L"El runtime interno se extrajo, pero faltan adb.exe o scrcpy.exe.");
        return FALSE;
    }
    return TRUE;
}

static BOOL ensure_runtime(void) {
    if (runtime_dir[0] == L'\0') {
        if (!get_runtime_dir(runtime_dir, MAX_PATH)) return FALSE;
    }

    wchar_t adb_path[MAX_PATH], scrcpy_path[MAX_PATH];
    _snwprintf_s(adb_path, MAX_PATH, _TRUNCATE, L"%s\\adb.exe", runtime_dir);
    _snwprintf_s(scrcpy_path, MAX_PATH, _TRUNCATE, L"%s\\scrcpy.exe", runtime_dir);

    if (file_exists(adb_path) && file_exists(scrcpy_path)) return TRUE;
    return extract_embedded_runtime();
}

static BOOL refresh_devices(void) {
    char buffer[16384];
    DWORD exit_code = (DWORD)-1;
    char serial[256];
    int found = 0;
    int unauthorized = 0;
    int offline = 0;

    if (!ensure_runtime()) return FALSE;

    wchar_t adb[MAX_PATH], cmd[1024];
    _snwprintf_s(adb, MAX_PATH, _TRUNCATE, L"%s\\adb.exe", runtime_dir);

    _snwprintf_s(cmd, 1024, _TRUNCATE, L"\"%s\" start-server", adb);
    {
        char start_output[4096];
        DWORD start_code = (DWORD)-1;
        run_hidden_capture(cmd, start_output, sizeof(start_output), &start_code);
    }

    _snwprintf_s(cmd, 1024, _TRUNCATE, L"\"%s\" devices -l", adb);
    if (!run_hidden_capture(cmd, buffer, sizeof(buffer), &exit_code)) {
        set_status(L"ADB no pudo iniciarse.");
        return FALSE;
    }

    write_diagnostic_log(buffer, exit_code);
    SendMessageW(hDevice, CB_RESETCONTENT, 0, 0);

    char *ctx = NULL;
    char *line = strtok_s(buffer, "\\r\\n", &ctx);
    while (line) {
        char state[64] = {0};
        if (sscanf_s(line, "%255s %63s", serial, (unsigned)_countof(serial),
                     state, (unsigned)_countof(state)) == 2) {
            if (strcmp(state, "device") == 0) {
                wchar_t wserial[256];
                int converted = MultiByteToWideChar(CP_UTF8, 0, serial, -1,
                                                    wserial, _countof(wserial));
                if (converted <= 0) {
                    MultiByteToWideChar(CP_ACP, 0, serial, -1,
                                        wserial, _countof(wserial));
                }
                SendMessageW(hDevice, CB_ADDSTRING, 0, (LPARAM)wserial);
                found++;
            } else if (strcmp(state, "unauthorized") == 0) {
                unauthorized++;
            } else if (strcmp(state, "offline") == 0) {
                offline++;
            }
        }
        line = strtok_s(NULL, "\\r\\n", &ctx);
    }

    if (found > 0) {
        SendMessageW(hDevice, CB_SETCURSEL, 0, 0);
        wchar_t msg[256];
        _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                     L"%d dispositivo(s) ADB listo(s).", found);
        set_status(msg);
    } else if (unauthorized > 0) {
        set_status(L"POCO F6 detectado, pero no autorizado. Desbloquea el teléfono y acepta la clave RSA.");
    } else if (offline > 0) {
        set_status(L"POCO F6 detectado, pero ADB está fuera de línea. Desconecta y vuelve a conectar el USB.");
    } else if (exit_code != 0) {
        set_status(L"ADB respondió con un error. Revisa PocoMirror-adb.log en AppData.");
    } else {
        set_status(L"No hay dispositivos ADB. Activa Depuración USB y acepta la autorización RSA.");
    }
    return found > 0;
}

static BOOL start_scrcpy(void) {
    if (scrcpy_pi.hProcess) return TRUE;
    if (!ensure_runtime()) return FALSE;

    wchar_t cmd[4096], serial[256], resolution[32], fps[32], bitrate[32], codec[32];
    wchar_t scrcpy_exe[MAX_PATH];

    _snwprintf_s(scrcpy_exe, MAX_PATH, _TRUNCATE, L"%s\\scrcpy.exe", runtime_dir);

    SendMessageW(hDevice, WM_GETTEXT, (WPARAM)_countof(serial), (LPARAM)serial);
    SendMessageW(hResolution, WM_GETTEXT, (WPARAM)_countof(resolution), (LPARAM)resolution);
    SendMessageW(hFps, WM_GETTEXT, (WPARAM)_countof(fps), (LPARAM)fps);
    SendMessageW(hBitrate, WM_GETTEXT, (WPARAM)_countof(bitrate), (LPARAM)bitrate);
    SendMessageW(hCodec, WM_GETTEXT, (WPARAM)_countof(codec), (LPARAM)codec);

    if (!serial[0]) {
        set_status(L"Selecciona un dispositivo antes de iniciar el espejo.");
        return FALSE;
    }

    BOOL audio = SendMessageW(hAudio, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL keep_active = SendMessageW(hStay, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL turnoff = SendMessageW(hTurnOff, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL fullscreen = SendMessageW(hFullscreen, BM_GETCHECK, 0, 0) == BST_CHECKED;

    _snwprintf_s(
        cmd, 4096, _TRUNCATE,
        L"\"%s\" --serial=\"%s\" --max-size=%s --max-fps=%s "
        L"--video-bit-rate=%s --video-codec=%s%s%s%s%s",
        scrcpy_exe, serial, resolution, fps, bitrate, codec,
        audio ? L"" : L" --no-audio",
        keep_active ? L" --keep-active" : L"",
        turnoff ? L" --turn-screen-off" : L"",
        fullscreen ? L" --fullscreen" : L"");

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    wchar_t *mutable_cmd = _wcsdup(cmd);
    if (!mutable_cmd) {
        set_status(L"No se pudo preparar el comando de scrcpy.");
        return FALSE;
    }

    BOOL ok = CreateProcessW(NULL, mutable_cmd, NULL, NULL, FALSE,
                             0, NULL, runtime_dir, &si, &scrcpy_pi);
    free(mutable_cmd);

    if (!ok) {
        set_status(L"No se pudo iniciar scrcpy. Revisa el runtime interno.");
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
    MessageBoxW(
        hwnd,
        L"Configuración inicial para POCO F6\n\n"
        L"1. Activa Opciones de desarrollador.\n"
        L"2. Activa Depuración USB.\n"
        L"3. Conecta el teléfono por USB.\n"
        L"4. Desbloquea el teléfono y acepta la clave RSA.\n\n"
        L"En Xiaomi/POCO puede ser necesario activar "
        L"Depuración USB (ajustes de seguridad) para control "
        L"por teclado, ratón o gamepad.\n\n"
        L"Android no permite que PocoMirror active "
        L"silenciosamente esos permisos protegidos.",
        L"Asistente de configuración",
        MB_OK | MB_ICONINFORMATION);
}

static HWND label(HWND parent, const wchar_t *text, int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, NULL, NULL, NULL);
}

static HWND combo(HWND parent, int id, int x, int y, int w, const wchar_t *initial) {
    HWND c = CreateWindowW(
        L"COMBOBOX", NULL,
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
        hDevice = CreateWindowW(
            L"COMBOBOX", NULL,
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

        hAudio = CreateWindowW(
            L"BUTTON", L"Audio", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            220, 132, 80, 22, hwnd, (HMENU)IDC_AUDIO, NULL, NULL);
        SendMessageW(hAudio, BM_SETCHECK, BST_CHECKED, 0);

        hStay = CreateWindowW(
            L"BUTTON", L"Mantener activo", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            305, 132, 125, 22, hwnd, (HMENU)IDC_STAY, NULL, NULL);

        hTurnOff = CreateWindowW(
            L"BUTTON", L"Apagar pantalla", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            435, 132, 120, 22, hwnd, (HMENU)IDC_TURNOFF, NULL, NULL);

        hFullscreen = CreateWindowW(
            L"BUTTON", L"Pantalla completa", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, 170, 135, 22, hwnd, (HMENU)IDC_FULLSCREEN, NULL, NULL);

        CreateWindowW(L"BUTTON", L"Iniciar espejo", WS_CHILD | WS_VISIBLE,
            20, 210, 135, 34, hwnd, (HMENU)IDC_START, NULL, NULL);

        CreateWindowW(L"BUTTON", L"Detener", WS_CHILD | WS_VISIBLE,
            165, 210, 100, 34, hwnd, (HMENU)IDC_STOP, NULL, NULL);

        CreateWindowW(L"BUTTON", L"Configuración inicial", WS_CHILD | WS_VISIBLE,
            275, 210, 150, 34, hwnd, (HMENU)IDC_SETUP, NULL, NULL);

        hStatus = CreateWindowW(
            L"STATIC", L"Preparando PocoMirror...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 260, 540, 60, hwnd, (HMENU)IDC_STATUS, NULL, NULL);

        SetTimer(hwnd, 1, 2000, NULL);
        refresh_devices();
        return 0;

    case WM_TIMER:
        if (wp == 1 && !scrcpy_pi.hProcess) {
            if (SendMessageW(hDevice, CB_GETCOUNT, 0, 0) == 0)
                refresh_devices();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH:
            refresh_devices();
            break;

        case IDC_START:
            if (SendMessageW(hDevice, CB_GETCOUNT, 0, 0) > 0)
                start_scrcpy();
            else
                set_status(L"Primero conecta y autoriza el POCO F6.");
            break;

        case IDC_STOP:
            stop_scrcpy();
            break;

        case IDC_SETUP:
            setup_message(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        stop_scrcpy();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdline, int show) {
    (void)hPrev;
    (void)cmdline;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PocoMirrorWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowW(
        L"PocoMirrorWindow", L"PocoMirror",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 590, 350,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
