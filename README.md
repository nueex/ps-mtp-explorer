<p align="center">
  <img src="assets/icon.png" alt="PS MTP Explorer icon" width="128">
</p>

<h1 align="center">PS MTP Explorer</h1>
<p align="center"><strong>Your phone. Your USB drive. Your PS4. One file explorer.</strong><br>by nueex</p>

> **Project summary:** ps-mtp-explorer is a PS4 homebrew utility by nueex for direct Android to PS4 USB file transfers, with support for direct PKG installation. Browse your phone over MTP, move files between phone, USB drive and internal HDD, and install packages without a PC, FTP server or local network setup.

<p align="center">
  <a href="https://github.com/nueex/ps-mtp-explorer/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/nueex/ps-mtp-explorer?style=for-the-badge&color=8b5cf6"></a>
  <img alt="Platform: PS4 homebrew" src="https://img.shields.io/badge/PS4-Homebrew-006FCD?style=for-the-badge">
  <img alt="Connection: USB MTP" src="https://img.shields.io/badge/USB-MTP-22c55e?style=for-the-badge">
  <img alt="Supports PKG installation" src="https://img.shields.io/badge/Supports-PKG_installation-06b6d4?style=for-the-badge">
  <a href="https://ko-fi.com/S6S01X1LYP"><img alt="Support on Ko-fi" src="https://img.shields.io/badge/Support-Ko--fi-ff5e5b?style=for-the-badge&logo=kofi&logoColor=white"></a>
</p>

<p align="center">
  <a href="https://github.com/nueex/ps-mtp-explorer/releases/latest"><strong>Download PKG</strong></a> ·
  <a href="#getting-started">Getting started</a> ·
  <a href="BUILD.md">Build from source</a> ·
  <a href="#install-pkg-from-phone-to-ps4">Install packages</a> ·
  <a href="#controller-guide">Controls</a> ·
  <a href="https://github.com/nueex/ps-mtp-explorer/issues">Report an issue</a>
</p>

---

## Everything in one place

| Capability | What you can do |
| :--- | :--- |
| **Direct phone-to-PS4 transfer** | Browse Android files and transfer them directly over USB MTP, without a PC or network setup. |
| **Internal & USB storage** | Browse PS4 internal storage and connected USB drives alongside your phone. |
| **Transfers in both directions** | Copy files and complete folder trees from phone to PS4/USB, from PS4/USB to phone, and between writable native storage locations. |
| **Familiar file actions** | Copy, Move, Paste, Rename, Delete and Properties across your phone, USB drive and internal HDD. |
| **New Folder** | Create a directory in the current writable location with the on-screen keyboard. |
| **Direct PKG installation** | Supports installing PKGs directly through the app from your phone, USB drive or internal HDD. |
| **Direct Download ([JetBird App](https://github.com/nueex/jetbird))** | Receive downloads directly on PS4 over USB or certificate-pinned Wireless (QR or 6-digit pairing), download directly over HTTPS, and use Parallel Install while downloading. |
| **Built-in media & file viewers** | View images with zoom and pan, view and edit UTF-8 text files with the PlayStation keyboard, and play videos and music (MP3, FLAC, WAV) directly in the app. |
| **Archive manager** | Extract ZIP, RAR/RAR5, 7z, TAR, Gzip, Bzip2, XZ, Zstandard and CPIO archives, and compress files or folders across 12 formats with optional AES-256 password protection. |
| **Multi-select & batch actions** | Select multiple files and folders with **Square** to copy, move, delete or compress in a single batch. |
| **Three themes** | Switch between **Light**, **Dark** and **Classic** themes in Settings. |
| **Reliable transfers** | Built-in integrity checks help protect your files during transfers. |
| **Live updates** | Connected devices and folder contents update automatically as you manage your files. |
| **Grid, list & details** | Switch views, inspect full paths and file properties, and see supported image previews, package artwork and available media metadata. |
| **Seven sorting choices** | Name A–Z/Z–A, size smallest/largest, modified newest/oldest, or file type. Folders stay grouped first. |
| **Designed for the controller** | Browse and manage your files from the controller, with an on-screen keyboard for folder and file names. |

## Interface & themes

PS MTP Explorer 3.0 includes **Light** (default), **Dark**, and **Classic** themes selectable in **Settings → Theme**.

<p align="center">
  <img src="assets/explorer.png" alt="PS MTP Explorer 3.0 — Light theme" width="960"><br>
  <sub><strong>Light theme</strong> (default) — interface preview rendered from v3.0 using sample files.</sub>
</p>

| Dark theme | Classic theme |
| :---: | :---: |
| ![PS MTP Explorer 3.0 — Dark theme](assets/explorer-dark.png) | ![PS MTP Explorer 3.0 — Classic theme](assets/explorer-classic.png) |

## JetBird companion app

For **Direct Download** and **Parallel PKG Installation**, pair PS MTP Explorer 3.0 with **[JetBird](https://github.com/nueex/jetbird)** — the official open-source Android companion app by nueex. JetBird combines a multi-part download manager and built-in ad-blocking browser with direct **USB Transfer**, **Wireless Transfer** (QR code and 6-digit pairing over certificate-pinned TLS), and console-side **Direct PS4 Download** straight to your PS4.

- **Download & source code:** [github.com/nueex/jetbird](https://github.com/nueex/jetbird)

## Hardware safety

PS MTP Explorer uses standard USB data transfers and is electrically safe under normal operating conditions. Read the [Hardware Safety & Electrical Analysis](HARDWARE_SAFETY.md) for clear answers about fast-charging phones, the Southbridge, large transfers, and safe cables.

## Getting started

### What you need

- A jailbroken **PS4 with GoldHEN active**.
- A controller and enough free storage for your transfers or installations.
- For phone access: an **MTP-capable Android phone** and a **USB data cable**. Unlock the phone and choose **File Transfer / Android Auto (MTP)** from its USB options. No companion phone app is required.
- For USB storage: a drive that your PS4 exposes as a mounted USB storage device.

**Tested on PS4 firmware 9.00, 10.01 and 11.00 with GoldHEN active.**

### Install and open

Build **PS MTP Explorer 3.0** using the [build instructions](BUILD.md), then install the PKG using GoldHEN's package installer. Close PS MTP Explorer before installing. The title ID is `NUEX00001`, and the PS4 `APP_VER` and `VERSION` fields are both `03.00`. [Published releases](https://github.com/nueex/ps-mtp-explorer/releases)

Then:

1. **Connect your phone via USB.** Unlock it and select File Transfer / MTP.
2. **Launch PS MTP Explorer.** Choose your phone, USB drive or internal storage.
3. **Manage files or install PKGs.** Browse folders, copy or move files, or select a package to install.

## Copy or move files

Use PS4 MTP transfer to move files between Android and your console, or manage files across internal storage and USB drives.

1. Highlight a file or folder and open **Options**.
2. Choose **Copy** or **Move**.
3. Browse to the destination on the phone, USB drive or internal storage.
4. Open **Options → Paste**, review the operation, then confirm with **X**.

Transfers include folder contents. Existing destination names are protected rather than silently overwritten. Supported large-file transfers can exceed 4 GB; phone uploads require the phone's large-file MTP support and a destination filesystem that permits the file size.

Use **Options → New Folder** to create a destination folder. For internal storage, use writable locations such as `/data`; other system locations may be browse-only. Phone write, rename and delete actions depend on the operations exposed by that phone.

Use **Properties** to inspect a file's size, full path and available metadata.

## Install PKG from Phone to PS4

Highlight a `.pkg`, press **X** and confirm **Install**. The app supports direct PKG installation from your Android phone, USB drive or internal HDD.

| Package location | Installation workflow | Source after installation |
| :--- | :--- | :--- |
| **Internal storage** | Install directly from the internal HDD. | Original PKG is kept. |
| **USB storage** | Install directly from the USB drive. | Original PKG is kept. |
| **Phone over MTP** | Select a PKG on your phone; the app prepares it automatically. | Original PKG on the phone is kept. |

For phone installations, leave enough internal space for **both the temporary PKG and installed content**. Keep the source connected and the app open until installation finishes.

**Package installation:** Select a PKG from your phone, USB drive or internal HDD and install it through the app. GoldHEN must already be active.

## Controller guide

| Button | Action |
| :--- | :--- |
| **D-pad** | Navigate files, folders, menus and storage devices. |
| **X** | Open a folder, supported image, text, video or audio file; review installation for a PKG; show Properties for other files; confirm a selected action. |
| **Circle** | Clear multi-selection, go back, close a dialog or cancel an operation. |
| **Triangle / Options** | Open the Options menu (including Extract, Compress and batch actions). |
| **Square** | Enter multi-select mode and toggle file or folder selection. |
| **L3** | Toggle Grid / List. |
| **R3** | Reopen a hidden transfer or Direct Download progress dialog. |
| **L1 / R1** | Previous / next page. |
| **L2 / R2** | Previous / next available phone storage area. |
| **Touchpad** | Refresh devices and the current view. |
| **Left from the file area's left edge** | Enter the device sidebar. |

## Build from source

See [BUILD.md](BUILD.md) for the required tools, SDK setup, production PKG
build commands, package validation, and optional CMake host library build.
Application source, bundled artwork, fonts, and the libjbc integration are
included in this repository; install the external compiler and OpenOrbis SDK
as described in the guide.

## Common questions

<details>
<summary><strong>My phone appears, but I cannot open it.</strong></summary>

Unlock the phone, choose File Transfer/MTP, and approve any device-side USB access prompt. Use a data-capable cable. Refresh or relaunch the app after changing the phone's USB mode. MTP implementations vary; phone-specific restrictions can affect browsing and writes.
</details>

<details>
<summary><strong>Can I use an iPhone?</strong></summary>

This release targets phones exposing standard MTP storage. iPhone file access is not supported.
</details>

<details>
<summary><strong>Why does a phone PKG need extra internal space?</strong></summary>

Phone package installation requires free internal storage for both the package and installed content. Your original phone file is kept.
</details>

<details>
<summary><strong>Where are downloads and source code?</strong></summary>

The application source and build scripts are included in this repository; follow [BUILD.md](BUILD.md) to compile your own copy. Previously released binaries retain their accompanying notices. See [LICENSING.md](LICENSING.md) for the current original-work GPL grant and unresolved requirements for distributing the complete application.
</details>

## Feedback

[Open an issue](https://github.com/nueex/ps-mtp-explorer/issues) with your PS4 firmware/GoldHEN version, phone model and Android version if relevant, the storage source/destination, and steps to reproduce the issue. Include the error shown on screen.

## Support / Donate

If PS MTP Explorer makes managing your PS4 easier, you can support development on Ko-fi. Thank you for helping the project continue.

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/S6S01X1LYP)

## Credits & third-party notices

**Original Author: nueex — Copyright (C) 2026 nueex** (original project work). Built with [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), musl, stb, dr_libs, BearSSL, libarchive, QR Code generator, Noto Sans, Urbanist and the Mozilla CA bundle.

**libjbc - PS4 Jailbreak C library by sleirsgoevy, with modifications by bucanero. Provided and used with full author attribution for PS4 homebrew execution.**

Additional libjbc contributor: **LightningMods**, for upstream Sony credential/capability handling.

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and the third-party materials attached to the release for component notices. PS MTP Explorer is an independent homebrew project and is not affiliated with Sony Interactive Entertainment.

## License and scope

**nueex's original work is licensed under GNU GPL v3.0-or-later (`GPL-3.0-or-later`)**, effective 2026-10-26. You may redistribute and/or modify the covered work under GNU GPL version 3 or, at your option, any later version. It is provided **WITHOUT ANY WARRANTY**, including MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the complete [LICENSE](LICENSE), [licensing scope](LICENSING.md), and explicit [file/contribution inventory](LICENSE_SCOPE.json).

The grant covers original application code, project-specific UI/design, authored icons and artwork owned by nueex. Mixed files are covered only for nueex's original contributions. Third-party libraries, OpenOrbis-derived material, fonts, trademarks and externally owned artwork retain their existing terms. No whole-file GPL-3.0-or-later grant is asserted for libjbc or mixed assets.

**The complete application/source bundle is not cleared for redistribution.** libjbc's inherited-code permissions and excluded third-party artwork/branding remain unresolved; see [LICENSING.md](LICENSING.md) and the [asset inventory](LICENSING_ASSETS.json). This status does not limit the GPL grant for nueex's original work. No component, feature or UI appearance was changed by this migration.

Previously MIT-licensed versions and material retain their original permissions. The complete [historical MIT notice](LICENSES/MIT-legacy.txt), including existing copyright notices, is preserved. These GPL notices do not revoke prior MIT grants or grant new rights to third-party content. Historical releases and archives are unchanged.

---

<p align="center"><strong>GoldHEN file manager | PS4 MTP transfer | Android to PS4 USB | Direct PKG installation</strong><br>PS MTP Explorer by nueex</p>

