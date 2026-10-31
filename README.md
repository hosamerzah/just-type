# AutoKeyboard 🌐⌨️

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%20Windows%2011-0078D6.svg)](https://microsoft.com)
[![Language](https://img.shields.io/badge/Language-C%2B%2B20-green.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

**AutoKeyboard** is an intelligent, native Windows keyboard layout switcher and typing assistant for **Arabic** and **English**. It eliminates the constant friction of manually pressing `Alt + Shift` or `Win + Space` by automatically detecting the intended language and switching the Windows keyboard layout seamlessly as you type.

100% offline, local, ultra-fast, and privacy-respecting: **no AI, no cloud, no OCR, no runtime downloads, and zero typing history or telemetry collection.**

---

## 👥 Authors & Credits

### 💡 Original Concept & Idea
- **Mohammed Alshamiri (محمد الشميري)**
  - 📧 Email: [m.shameri214@gmail.com](mailto:m.shameri214@gmail.com)
  - 📱 Phone: [+967771784214](tel:+967771784214)
  - *Conceived the original idea, practical vision, and initial concept for an effortless, automatic bilingual typing experience.*

### 🛠️ Lead Architect & Developer
- **Ahmed Merzah Alzubair (أحمد ميرزاح الزبير)**
  - 📧 Email: [eng.alzubairy27@gmail.com](mailto:eng.alzubairy27@gmail.com)
  - 📱 Phone: [+967774361616](tel:+967774361616)
  - 🐙 GitHub: [@hosamerzah](https://github.com/hosamerzah)
  - *Engineered the full system architecture, low-level Windows hooks, Text Services Framework (TSF) integration, 50,000-word frequency dictionaries, UI, PDF vocabulary worker, and Windows 11 compatibility enhancements.*

---

## ✨ Key Features

- **Automatic Real-Time Language Switching**: Switches seamlessly between Arabic (101) and English (US) layouts at word boundaries based on frequency dictionaries and deterministic rules.
- **Dual Architecture (Hook + TSF)**:
  - Low-level Windows hooks (`WH_KEYBOARD_LL` & `WH_MOUSE_LL`) for sub-millisecond layout switching across all applications.
  - Windows Text Services Framework (TSF) COM provider for deep integration with modern Windows 11 applications (Notepad, Word, Edge, Chrome).
- **50,000+ Word Frequency Dictionaries**: Pre-bundled with high-accuracy Hunspell dictionaries and word frequency ranking for both Arabic and English to resolve ambiguous keystrokes intelligently.
- **Smart Safety & Boundary Protection**:
  - Automatically preserves passwords, URL bars, code/terminal sessions, and email fields without interference.
  - Cursor tracking and mouse protection ensure existing text is never accidentally overwritten or corrupted.
- **Custom Protected Words**: Built-in editor to define custom words, names, commands, or jargon that should always stay in a specific layout.
- **PDF Word Bank Importer**: Import your own specialized vocabulary and documents directly from PDF files into a local SQLite database (`words.db`).
- **System Tray Management**: Clean tray menu with live mode indicators, manual layout toggling, lock state, and settings access.
- **Windows 11 Ghost Keyboard Cleaner**: Includes a dedicated script (`tools/clean_ghost_keyboards.bat`) to clean stubborn phantom layouts (e.g. UK English or Afrikaans) from the Windows 11 language flyout.

---

## 🚀 Quick Start for Users

### Installation
1. Download the latest installer from the [Releases](https://github.com/hosamerzah/lang_switcher/releases) page:
   - `AutoKeyboard-0.2.0-unsigned-preview-setup.exe`
2. Run the installer. It registers the keyboard profile and adds a Start menu shortcut.
3. Open **AutoKeyboard** from the Start menu.
4. Click **Activate keyboard**, then select **Automatic Arabic / English** with `Win + Space` if prompted.

### Essential Shortcuts
| Shortcut | Action |
| :--- | :--- |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Space</kbd> | Convert current or most recently typed word between Arabic & English |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Backspace</kbd> | Undo the most recent word conversion |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>L</kbd> | Lock / unlock the current typing language |

---

## 🛠️ How It Works

Words remain provisional while typing. Pressing <kbd>Space</kbd> or <kbd>Enter</kbd> finalizes the word and evaluates whether a layout switch is needed:
- **Cautious Mode** (default): Switches layout only when the alternative language forms a valid dictionary word and the current interpretation does not.
- **Automatic Mode**: Ranks candidates based on frequency rankings, imported document counts, and language preference.
- Punctuation keys finalize words safely without interfering with shared keys (e.g., the US period key `.` which corresponds to Arabic `ز`).
- Arrow keys, Tab, Escape, and mouse clicks commit provisional text without switching.

---

## 📁 Repository Structure

```text
lang_switcher/
├── assets/                  # Icons, application manifests, and resources
├── docs/                    # Architecture, testing, and compatibility specs
├── packs/                   # Arabic & English Hunspell dictionaries & keymaps
│   ├── ar/                  # Arabic 101 layout, dictionary, frequency list
│   └── en/                  # US English layout, dictionary, frequency list
├── scripts/                 # Automated build, bootstrap, and packaging scripts
├── src/                     # Core C++20 engine, TSF provider, hooks, settings UI
├── tests/                   # Automated unit & integration test suites
├── tools/                   # Helper utilities (ghost keyboard cleaner)
├── AUTHORS.md               # Full credits and contact information
├── CMakeLists.txt           # CMake build definition
├── LICENSE                  # MIT Open Source License
└── README.md                # Project documentation
```

---

## 🔧 Building from Source

### Prerequisites
- Windows 10 or 11 (64-bit)
- Python 3.11+
- CMake 3.24+
- Either Visual Studio with Desktop C++ / Windows SDK, or the portable LLVM-MinGW toolchain.

### Build Steps
```powershell
# 1. Download & prepare dependencies (offline-capable after bootstrap)
python scripts/bootstrap.py --arch x64 --toolchain
python scripts/bootstrap.py --arch x86

# 2. Build both x64 and x86 architectures
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Architecture x64 -UseLocalClang
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Architecture x86 -UseLocalClang

# 3. Create the standalone installer
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -UnsignedPreview

# 4. Run automated verification suite
python scripts/verify_package.py
```

The compiled installer will be output to:
`dist/AutoKeyboard-0.2.0-unsigned-preview-setup.exe`

---

## 📜 Dependencies & Licenses

- **AutoKeyboard**: Released under the [MIT License](LICENSE).
- **Hunspell 1.7.2**: LGPL / GPL / MPL options in `licenses/`.
- **SQLite 3.50.4**: Public domain.
- **PDFium (Chromium build 8125)**: BSD and third-party notices in `licenses/`.
- **Dictionaries**: LibreOffice dictionaries commit `4f1df64b2e8e14751aa7c2205d170ca016071315` (GPL / LGPL / MPL).

---

## 💬 Contact & Support

For questions, suggestions, or contributions:
- **Ahmed Merzah Alzubair**: [eng.alzubairy27@gmail.com](mailto:eng.alzubairy27@gmail.com) | [+967774361616](tel:+967774361616)
- **Mohammed Alshamiri**: [m.shameri214@gmail.com](mailto:m.shameri214@gmail.com) | [+967771784214](tel:+967771784214)
