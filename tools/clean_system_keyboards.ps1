# Remove Afrikaans from .DEFAULT
Remove-Item "Registry::HKEY_USERS\.DEFAULT\Control Panel\International\User Profile\af" -Recurse -Force -ErrorAction SilentlyContinue

# Remove UK keyboard from en-US under .DEFAULT
Remove-ItemProperty "Registry::HKEY_USERS\.DEFAULT\Control Panel\International\User Profile\en-US" -Name "0409:00000809" -ErrorAction SilentlyContinue

# Remove AutoKeyboard TSF profile from en-US under .DEFAULT
Remove-ItemProperty "Registry::HKEY_USERS\.DEFAULT\Control Panel\International\User Profile\en-US" -Name "0409:{806B192A-0D35-43E5-A47C-7A940E38B565}{7CA8776C-8B94-4CFF-B75E-C7A7D897895A}" -ErrorAction SilentlyContinue

# Fix Preload under .DEFAULT
Set-ItemProperty "Registry::HKEY_USERS\.DEFAULT\Keyboard Layout\Preload" -Name "1" -Value "00000409" -ErrorAction SilentlyContinue
Set-ItemProperty "Registry::HKEY_USERS\.DEFAULT\Keyboard Layout\Preload" -Name "2" -Value "00000401" -ErrorAction SilentlyContinue
Remove-ItemProperty "Registry::HKEY_USERS\.DEFAULT\Keyboard Layout\Preload" -Name "3" -ErrorAction SilentlyContinue
Remove-ItemProperty "Registry::HKEY_USERS\.DEFAULT\Keyboard Layout\Preload" -Name "4" -ErrorAction SilentlyContinue

# Copy clean user settings to system Welcome screen and new user profile
Copy-UserInternationalSettingsToSystem -WelcomeScreen $true -NewUser $true -ErrorAction SilentlyContinue

# Restart TextInputHost and Explorer to refresh taskbar flyout
Stop-Process -Name "TextInputHost" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "explorer" -Force -ErrorAction SilentlyContinue
