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
| **Reliable transfers** | Built-in integrity checks help protect your files during transfers. |
| **Live updates** | Connected devices and folder contents update automatically as you manage your files. |
| **Grid, list & details** | Switch views, inspect full paths and file properties, and see supported image previews, package artwork and available media metadata. |
| **Seven sorting choices** | Name A–Z/Z–A, size smallest/largest, modified newest/oldest, or file type. Folders stay grouped first. |
| **Designed for the controller** | Browse and manage your files from the controller, with an on-screen keyboard for folder and file names. |

<p align="center">
  <img src="assets/explorer.png" alt="PS MTP Explorer grid view with connected storage, folders and file details" width="960"><br>
  <sub>Interface preview rendered from the app using sample files.</sub>
</p>

## Getting started

### What you need

- A jailbroken **PS4 with GoldHEN active**.
- A controller and enough free storage for your transfers or installations.
- For phone access: an **MTP-capable Android phone** and a **USB data cable**. Unlock the phone and choose **File Transfer / Android Auto (MTP)** from its USB options. No companion phone app is required.
- For USB storage: a drive that your PS4 exposes as a mounted USB storage device.

**Tested on firmware 11.00.**

### Install and open

Download **`PS-MTP-Explorer-1.00.pkg`** from [Releases](https://github.com/nueex/ps-mtp-explorer/releases/latest) and install it using GoldHEN's package installer. Then:

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
| **X** | Open a folder; review installation for a PKG; show Properties for another file; confirm a selected action. |
| **Circle** | Go back, close a dialog or cancel an operation. |
| **Triangle / Options** | Open the Options menu. |
| **L3** | Toggle Grid / List. |
| **L1 / R1** | Previous / next page. |
| **L2 / R2** | Previous / next available phone storage area. |
| **Touchpad** | Refresh devices and the current view. |
| **Left from the file area's left edge** | Enter the device sidebar. |

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

The compiled application is distributed through GitHub Releases. This repository contains release documentation and assets; the application source is not published here. Required third-party notices and corresponding runtime materials accompany the release separately.
</details>

<details>
<summary><strong>Where is the project's source code?</strong></summary>

The source code will be published in the future.
</details>

## Feedback

[Open an issue](https://github.com/nueex/ps-mtp-explorer/issues) with your PS4 firmware/GoldHEN version, phone model and Android version if relevant, the storage source/destination, and steps to reproduce the issue. Include the error shown on screen.

## Support / Donate

If PS MTP Explorer makes managing your PS4 easier, you can support development on Ko-fi. Thank you for helping the project continue.

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/S6S01X1LYP)

## Credits & third-party notices

Created by **nueex**. Built with [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), musl, stb and Noto Sans.

**libjbc - PS4 Jailbreak C library by sleirsgoevy, with modifications by bucanero. Provided and used with full author attribution for PS4 homebrew execution.**

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and the third-party materials attached to the release for component notices. PS MTP Explorer is an independent homebrew project and is not affiliated with Sony Interactive Entertainment.

---

<p align="center"><strong>GoldHEN file manager | PS4 MTP transfer | Android to PS4 USB | Direct PKG installation</strong><br>PS MTP Explorer by nueex</p>
