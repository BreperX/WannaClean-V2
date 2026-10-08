# WannaClean v2

[![Descargar](https://img.shields.io/badge/Descargar-WannaClean_v2-b00000?style=for-the-badge)](https://github.com/BreperX/WannaClean-V2/releases)
![Plataforma](https://img.shields.io/badge/Plataforma-Windows-1a1a1a?style=for-the-badge)

**Prepara tu computadora para lo que importa.**

WannaClean es una utilidad para Windows que cierra las aplicaciones y detiene los servicios que elijas antes de una tarea pesada, como jugar. También puede liberar memoria de trabajo y la memoria en caché (standby).

Cada preset trae una lista por defecto de aplicaciones y servicios, y tú la ajustas a tu gusto en **Ajustes**. Lo que no está en la lista, no se toca.

La estética es una parodia de ransomware, pero no cifra ni elimina archivos.

<p align="center">
  <img src="docs/img/inicio.png" width="48%" alt="Pantalla de inicio de WannaClean con los presets Jugar, Trabajar y Agresivo">
  <img src="docs/img/listo.png" width="48%" alt="Resultado de una operación con estimaciones de memoria, aplicaciones cerradas y servicios detenidos">
</p>

Las estimaciones de memoria son independientes y pueden superponerse; no deben sumarse. No garantizan más FPS ni una mejora específica del rendimiento.

## Qué puedes hacer

- Ejecutar los presets **Jugar**, **Trabajar** y **Agresivo**.
- Revisar los objetivos detectados antes de ejecutar Agresivo.
- Consultar el resultado y los detalles de la operación.
- Revertir los servicios que WannaClean dejó detenidos.

## Uso

1. Descarga `WannaClean.exe` desde la pestaña **Releases** y ejecútalo. La aplicación requiere permisos de administrador para cerrar aplicaciones y detener servicios. El ejecutable no está firmado; SmartScreen o tu antivirus pueden mostrar una advertencia antes de abrirlo.
2. Elige un preset. Agresivo pide confirmar los objetivos antes de continuar.
3. Para personalizar las aplicaciones y los servicios de cada preset, abre Ajustes.

## Compilar y probar

Requiere Visual Studio 2022 con las herramientas de C++ para escritorio, el Windows SDK y vcpkg integrado con MSBuild (`vcpkg integrate install`). El manifiesto `vcpkg.json` declara las dependencias. Abre `WannaClean V2.sln` y compila `Release x64`.

Las pruebas de Core validan la lógica de presets, ajustes y protecciones; no cierran procesos ni detienen servicios.

En Visual Studio, selecciona `CoreTests` como proyecto de inicio, elige `Debug x64`, inicia la aplicación y pulsa `t` en el menú de consola. También puedes ejecutar las pruebas directamente con `--test`; la ruta depende de la configuración compilada:

```powershell
# Debug x64
.\x64\Debug\CoreTests.exe --test

# Release x64
.\x64\Release\CoreTests.exe --test
```