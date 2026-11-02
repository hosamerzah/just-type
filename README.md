# Just Type (اكتب بلا هم) 🌐⌨️

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%20Windows%2011-0078D6.svg)](https://microsoft.com)
[![Language](https://img.shields.io/badge/Language-C%2B%2B20-green.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

**Just Type (اكتب بلا هم)** is a lightweight native Windows utility for seamless automatic switching between Arabic and English keyboard layouts. It lets you type freely without manually toggling between layouts with `Alt + Shift` or `Win + Space`.

100% local, offline, ultra-fast, and private: no AI, no cloud dependencies, and no typing data collection.

---

## 👥 Authors & Credits

* **Eng. Mohammed Alshamiri (المهندس محمد الشميري)**
  * **Role**: Idea & Concept (صاحب الفكرة)
  * **Email**: [m.shameri214@gmail.com](mailto:m.shameri214@gmail.com)
  * **Phone**: [+967771784214](tel:+967771784214)

* **Eng. Ahmed Merzah Alzubair (المهندس أحمد ميرزاح الزبير)**
  * **Role**: Development & Implementation (التطوير البرمجي)
  * **Email**: [eng.alzubairy27@gmail.com](mailto:eng.alzubairy27@gmail.com)
  * **Phone**: [+967774361616](tel:+967774361616)
  * **GitHub**: [@hosamerzah](https://github.com/hosamerzah)

---

## ✨ Features

- **Automatic Language Switching**: Detects word boundaries and switches between Arabic (101) and English (US) automatically.
- **Hybrid Hook + TSF Architecture**: Combines low-level keyboard hooks (`WH_KEYBOARD_LL`) with Text Services Framework (TSF) integration for full compatibility across Win32 and modern Windows 11 applications (Notepad, Word, Edge, Chrome, WhatsApp).
- **50,000-Word Frequency Dictionaries**: Pre-bundled with high-frequency wordlists and Hunspell dictionaries for accurate layout selection.
- **Safety & Caret Protection**: Password fields, URLs, terminals, and arrow/mouse movements preserve your text safely.
- **Custom Protected Words**: Simple editor to add technical terms, usernames, or commands that should never be converted.
- **PDF Word Bank Importer**: Import custom words from your own PDF documents into a local SQLite database.
- **System Tray Controls**: Quickly switch modes, lock language, or access settings from the Windows taskbar.
- **Ghost Keyboard Cleaner**: Includes `tools/clean_ghost_keyboards.bat` to eliminate phantom keyboard layouts in Windows 11.

---

## 🚀 Download & Installation

1. Download the installer from the [Releases](https://github.com/hosamerzah/just-type/releases) page:
   - `AutoKeyboard-0.2.0-unsigned-preview-setup.exe` (or `JustType-setup.exe`)
2. Run the installer and launch the program from the Start menu.
3. Click **Activate keyboard**, and select **Automatic Arabic / English** via `Win + Space` if needed.

### Shortcuts
| Shortcut | Action |
| :--- | :--- |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Space</kbd> | Convert current word between Arabic and English |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Backspace</kbd> | Undo last conversion |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>L</kbd> | Lock / unlock current language |

---

## 📁 Repository Structure

```text
just-type/
├── assets/                  # Icons and application manifest
├── docs/                    # Architecture and validation documentation
├── packs/                   # Arabic & English dictionaries and keymaps
├── scripts/                 # Build and packaging automation
├── src/                     # Core C++20 engine, hooks, TSF provider, UI
├── tests/                   # Automated unit & integration test suites
├── tools/                   # Helper utilities (ghost keyboard cleaner)
├── AUTHORS.md               # Authors & credits
├── CMakeLists.txt           # Build definition
├── LICENSE                  # MIT License
└── README.md                # Documentation
```

---

## 🔧 Building from Source

```powershell
# Bootstrap dependencies
python scripts/bootstrap.py --arch x64 --toolchain
python scripts/bootstrap.py --arch x86

# Build x64 and x86
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Architecture x64 -UseLocalClang
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Architecture x86 -UseLocalClang

# Package installer
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -UnsignedPreview

# Verify installer package
python scripts/verify_package.py
```

---

## 📜 License

Released under the [MIT License](LICENSE).  
Copyright (c) 2026 Eng. Mohammed Alshamiri & Eng. Ahmed Merzah Alzubair.
