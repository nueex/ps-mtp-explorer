<p align="center">
  <img src="assets/icon.png" alt="PS MTP Explorer icon" width="128">
</p>

<h1 align="center">PS MTP Explorer</h1>
<p align="center"><strong>Your phone. Your USB drive. Your PS4. One file explorer.</strong><br>by nueex</p>

> **Project summary:** PS MTP Explorer is a PS4 homebrew file manager and offline PKG installer. Connect an Android phone over USB using MTP to browse files, transfer files and folders between your phone and PS4 storage, and install PKG packages. Browse PS4 internal storage and mounted USB drives from the same controller-friendly interface. No PC, Wi-Fi, local network or internet connection is needed for these workflows.

<p align="center">
  <a href="https://github.com/nueex/ps-mtp-explorer/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/nueex/ps-mtp-explorer?style=for-the-badge&color=8b5cf6"></a>
  <img alt="Platform: PS4 homebrew" src="https://img.shields.io/badge/PS4-Homebrew-006FCD?style=for-the-badge">
  <img alt="Connection: USB MTP" src="https://img.shields.io/badge/USB-MTP-22c55e?style=for-the-badge">
  <img alt="Works offline" src="https://img.shields.io/badge/Works-Offline-06b6d4?style=for-the-badge">
  <a href="https://ko-fi.com/S6S01X1LYP"><img alt="Support on Ko-fi" src="https://img.shields.io/badge/Support-Ko--fi-ff5e5b?style=for-the-badge&logo=kofi&logoColor=white"></a>
</p>

<p align="center">
  <a href="https://github.com/nueex/ps-mtp-explorer/releases/latest"><strong>⬇️ Download PKG</strong></a> ·
  <a href="#-getting-started">Getting started</a> ·
  <a href="#-install-pkg-packages">Install packages</a> ·
  <a href="#-controller-guide">Controls</a> ·
  <a href="https://github.com/nueex/ps-mtp-explorer/issues">Report an issue</a>
</p>

---

## ✨ Everything in one place

| Capability | What you can do |
| :--- | :--- |
| 📱 **Phone browsing over USB** | Open an MTP phone, browse its exposed storage and switch between available phone storage areas. |
| 💾 **Internal & USB storage** | Browse PS4 internal storage and connected USB drives alongside your phone. |
| ↔️ **Transfers in both directions** | Copy files and complete folder trees from phone to PS4/USB, from PS4/USB to phone, and between writable native storage locations. |
| 📋 **Familiar file actions** | Copy, Move, Paste, Rename, Delete and Properties in a desktop-style Options menu. Paste stays in the same slot and becomes available when there is a compatible clipboard item. |
| 📁 **New Folder** | Create a directory in the current writable location with the on-screen keyboard. |
| 📦 **Offline PKG installation** | Select a `.pkg`, review the source and size, then confirm installation. Phone packages are staged automatically; native packages install in place. |
| 📊 **Useful progress** | Follow transferred bytes, recent transfer speed and estimated time remaining. Installation shows system-reported progress, with an activity indicator while progress is unavailable. |
| ✅ **Practical verification** | Normal copies use a quick size/content check. Moves perform stronger verification before removing source items. Free-space checks run before supported transfers. |
| 🔄 **Live updates** | Device detection and idle folder refresh keep the explorer current as contents change. Refresh manually at any time. |
| 🖼️ **Grid, list & details** | Switch views, inspect full paths and file properties, and see supported image previews, package artwork and available media metadata. |
| 🔎 **Seven sorting choices** | Name A–Z/Z–A, size smallest/largest, modified newest/oldest, or file type. Folders stay grouped first. |
| 🎮 **Designed for the controller** | Navigate large folders, use button hints, enter names on a QWERTY keyboard, and hear device connection/disconnection cues. |
| 🔌 **Clean phone sessions** | Exit and relaunch with the phone still connected; the app closes and initializes its MTP session again. |

<p align="center">
  <img src="assets/explorer.png" alt="PS MTP Explorer grid view with connected storage, folders and file details" width="960"><br>
  <sub>Interface preview rendered from the app using sample files.</sub>
</p>

## 🚀 Getting started

### What you need

- A homebrew-enabled **PS4 on firmware 11.00 with GoldHEN active**. This is the supported access profile for this release; other firmware versions are not claimed compatible.
- A controller and enough free storage for your transfers or installations.
- For phone access: an **MTP-capable Android phone** and a **USB data cable**. Unlock the phone and choose **File Transfer / Android Auto (MTP)** from its USB options. No companion phone app is required.
- For USB storage: a drive that your PS4 exposes as a mounted USB storage device.

### Install and open

1. Download **`PS-MTP-Explorer-1.00.pkg`** from [Releases](https://github.com/nueex/ps-mtp-explorer/releases/latest).
2. Install it using your homebrew environment's package installer.
3. Launch **PS MTP Explorer** from the PS4 home screen.
4. Connect your phone or USB drive. Select it in the device sidebar and press **X**.
5. Browse folders, manage files or select a PKG to install.

**Release identity:** `NUEEX0001` · App version **1.00** · PS4 package metadata **01.00**.

Upgrading from a development preview? The new NUEEX title ID installs as a separate app from the older PSMTP preview. Existing user files remain in their storage locations.

## ↔️ Copy or move files

1. Highlight a file or folder and open **Options**.
2. Choose **Copy** or **Move**.
3. Browse to the destination on the phone, USB drive or internal storage.
4. Open **Options → Paste**, review the operation, then confirm with **X**.

Transfers include folder contents. Existing destination names are protected rather than silently overwritten. Supported large-file transfers can exceed 4 GB; phone uploads require the phone's large-file MTP support and a destination filesystem that permits the file size.

Use **Options → New Folder** to create a destination folder. For internal storage, use writable locations such as `/data`; other system locations may be browse-only. Phone write, rename and delete actions depend on the operations exposed by that phone.

Pressing **X on a non-PKG file opens Properties**. Copy and Move are explicit Options actions. Image/media previews provide file information; the app is not a media player.

## 📦 Install PKG packages

Highlight a `.pkg`, press **X**, review the confirmation and select **Install**. When the system installer completes, the app shows **“PKG installed successfully.”**

| Package location | Installation workflow | Source after installation |
| :--- | :--- | :--- |
| **Internal storage** | Install directly from its existing path. | Original PKG is kept. No staging copy. |
| **USB storage** | Install directly from the USB drive. | Original PKG is kept. No staging copy. |
| **Phone over MTP** | Copy to temporary internal storage, perform a quick check, then install locally. | Phone original is kept. The app removes its temporary staging file and directory when the attempt succeeds, fails or is cancelled. |

For phone installations, leave enough internal space for **both the temporary PKG and installed content**. Keep the source connected and the app open until the attempt finishes. If storage becomes inaccessible, cleanup may require attention before retrying.

**Fully offline:** the phone connects directly to the PS4 by USB. Installation uses the console's local installer. It does not require HTTP serving, a Wi-Fi network, a router, custom DNS or an internet connection. Your homebrew environment must already be enabled.

## 🎮 Controller guide

| Button | Action |
| :--- | :--- |
| **D-pad** | Navigate files, folders, menus and sidebar devices. From the first row, Up reaches the view/sort toolbar. |
| **X** | Open a folder; review installation for a PKG; show Properties for another file; confirm a selected action. |
| **Circle** | Go back, close a dialog or cancel the current operation at a safe boundary. |
| **Triangle / Options** | Open the Options menu. |
| **L3** | Toggle Grid / List. |
| **L1 / R1** | Previous / next page. |
| **L2 / R2** | Previous / next available phone storage area. |
| **Touchpad** | Refresh devices and the current view. |
| **Left from the file area's left edge** | Enter the device sidebar. |

The on-screen keyboard provides its own Add, Erase, Shift, Clear, Create/Save and Cancel hints. Settings is intentionally grayed out in this release; file actions remain available from Options.

## 💡 Common questions

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

The package must be copied from MTP to a local path before the PS4 installer starts. The temporary copy is removed when the attempt ends. Packages already on USB or internal storage do not need this staging step.
</details>

<details>
<summary><strong>Where are downloads and source code?</strong></summary>

The compiled application is distributed through GitHub Releases. This repository contains release documentation and assets; the application source is not published here. Required third-party notices and corresponding runtime materials accompany the release separately.
</details>

## 🐛 Feedback

[Open an issue](https://github.com/nueex/ps-mtp-explorer/issues) with your PS4 firmware/GoldHEN version, phone model and Android version if relevant, the storage source/destination, and steps to reproduce the issue. Include the error shown on screen. Production builds do not save debug logs.

## ☕ Support / Donate

If PS MTP Explorer makes managing your PS4 easier, you can support development on Ko-fi. Thank you for helping the project continue.

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/S6S01X1LYP)

## 🙏 Credits & third-party notices

Created by **nueex**. Built with [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), musl, stb and Noto Sans.

**libjbc - PS4 Jailbreak C library by sleirsgoevy, with modifications by bucanero. Provided and used with full author attribution for PS4 homebrew execution.**

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and the third-party materials attached to the release for component notices. PS MTP Explorer is an independent homebrew project and is not affiliated with Sony Interactive Entertainment.

---

<p align="center"><strong>PS4 file management • Android MTP transfers • USB storage • Offline PKG installation</strong><br>PS MTP Explorer by nueex</p>
