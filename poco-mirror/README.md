# PocoMirror — versión de prueba

Launcher gráfico de Windows para probar una configuración simplificada de scrcpy enfocada al POCO F6.

## Objetivo de esta primera prueba

- No usar la consola para iniciar el espejo.
- Detectar dispositivos ADB autorizados.
- Mostrar un asistente de configuración inicial.
- Configurar desde una ventana:
  - resolución máxima
  - FPS
  - bitrate
  - codec
  - audio
  - mantener dispositivo activo
  - apagar pantalla
  - pantalla completa
- Iniciar y detener scrcpy desde la GUI.
- Mantener scrcpy como motor de mirroring, sin reimplementar su protocolo.

## Estructura esperada para ejecutar

Coloca estos archivos en la misma carpeta:

- PocoMirror.exe
- scrcpy.exe
- adb.exe
- las DLL de scrcpy/FFmpeg/SDL necesarias

La siguiente fase empaquetará estos componentes en una distribución de prueba más cómoda.

## Requisitos del teléfono

Para ADB/scrcpy se requiere Depuración USB y autorización del ordenador en el teléfono. En dispositivos Xiaomi/POCO, algunas formas de control pueden requerir además Depuración USB (ajustes de seguridad).

El launcher no intenta saltarse ni activar silenciosamente permisos protegidos de Android.

## Compilación rápida con MSVC

Desde un Developer Command Prompt:

    cl /O2 /DUNICODE /D_UNICODE poco-mirror\\launcher.c /link /SUBSYSTEM:WINDOWS comctl32.lib /OUT:PocoMirror.exe

La base de scrcpy conserva su licencia Apache-2.0.
