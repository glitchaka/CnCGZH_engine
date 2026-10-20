# CnCGZH Engine / Studio

CnCGZH Studio es el editor visual del código fuente de Command & Conquer: Generals / Zero Hour conservado en `upstream/ea-generals-zero-hour`.

No hay una versión 1 ni una versión 2 del editor. Hay una sola aplicación: **CnCGZH Studio**.

## Objetivo

Exponer las herramientas y sistemas del motor mediante una interfaz visual para que puedan usarse sin escribir C++ directamente.

La base actual incluye:

- viewport 3D;
- jerarquía de objetos;
- inspector de transformación;
- gizmos de mover, rotar y escalar;
- cámaras visibles en escena;
- navegador de recursos;
- catálogo de funciones y cabeceras del código original;
- scripting visual anidado a cada objeto.

## Código original

El código fuente original de Generals / Zero Hour se mantiene bajo:

`upstream/ea-generals-zero-hour`

El editor nuevo está bajo:

`editor/`

## Compilar en Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target CnCGZHStudio
.\build\Release\CnCGZHStudio.exe
```
