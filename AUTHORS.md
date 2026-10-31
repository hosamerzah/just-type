# Authors and Credits

## Original Concept & Idea
- **Mohammed Alshamiri**
  - Email: [m.shameri214@gmail.com](mailto:m.shameri214@gmail.com)
  - Phone: [+967771784214](tel:+967771784214)
  - Contribution: Conceived the foundational idea, concept, and initial vision for an automatic, seamless Arabic-English bilingual typing assistant that detects and switches keyboard language automatically without user friction.

## Lead Architect & Developer
- **Ahmed Merzah Alzubair**
  - Email: [eng.alzubairy27@gmail.com](mailto:eng.alzubairy27@gmail.com)
  - Phone: [+967774361616](tel:+967774361616)
  - GitHub: [@hosamerzah](https://github.com/hosamerzah)
  - Contribution: Complete system architecture and implementation, including:
    - Low-level Windows hook (`WH_KEYBOARD_LL`) & Windows Text Services Framework (TSF) hybrid engine.
    - Low-latency real-time layout switching (`WM_INPUTLANGCHANGEREQUEST`) across Win32, UWP, and modern Windows 11 apps.
    - Integration of 50,000-word Arabic & English frequency dictionaries with Hunspell spell-checking.
    - Intelligent boundary detection, protected field recognition (passwords, URLs), and cursor safety.
    - PDF word bank extraction worker, settings UI, system tray integration, and installer packaging.

---
*AutoKeyboard is dedicated to making bilingual Arabic-English typing completely effortless.*
