import winreg
import sys

def wipe_root(root_sid):
    print(f"=== Cleaning {root_sid} ===")
    root = winreg.HKEY_USERS
    
    # 1. Update User Profile Languages
    up_path = f"{root_sid}\\Control Panel\\International\\User Profile"
    try:
        with winreg.OpenKey(root, up_path, 0, winreg.KEY_READ | winreg.KEY_WRITE) as k:
            winreg.SetValueEx(k, "Languages", 0, winreg.REG_MULTI_SZ, ["en-US", "ar-SA"])
            print(f"[{up_path}] Set Languages to ['en-US', 'ar-SA']")
    except Exception as e:
        print(f"[{up_path}] Failed setting Languages: {e}")
        
    # Delete 'af' subkey under User Profile
    try:
        winreg.DeleteKey(root, f"{up_path}\\af")
        print(f"[{up_path}\\af] Deleted successfully")
    except Exception as e:
        print(f"[{up_path}\\af] Delete: {e}")

    # Delete 0409:00000809 under en-US
    try:
        with winreg.OpenKey(root, f"{up_path}\\en-US", 0, winreg.KEY_SET_VALUE) as k:
            try:
                winreg.DeleteValue(k, "0409:00000809")
                print(f"[{up_path}\\en-US] Deleted value 0409:00000809")
            except FileNotFoundError:
                pass
    except Exception as e:
        print(f"[{up_path}\\en-US] Value delete error: {e}")

    # 2. Delete User Profile System Backup
    upsb_path = f"{root_sid}\\Control Panel\\International\\User Profile System Backup"
    for sub in ["af", "en-US", "ar-SA"]:
        try:
            winreg.DeleteKey(root, f"{upsb_path}\\{sub}")
            print(f"Deleted subkey {upsb_path}\\{sub}")
        except Exception:
            pass
    try:
        winreg.DeleteKey(root, upsb_path)
        print(f"Deleted {upsb_path}")
    except Exception as e:
        print(f"[{upsb_path}] Delete: {e}")

    # 3. Clean Keyboard Layout\Substitutes
    subst_path = f"{root_sid}\\Keyboard Layout\\Substitutes"
    try:
        with winreg.OpenKey(root, subst_path, 0, winreg.KEY_READ | winreg.KEY_WRITE) as k:
            for val_name in ["d0010409", "00000436"]:
                try:
                    winreg.DeleteValue(k, val_name)
                    print(f"[{subst_path}] Deleted substitute: {val_name}")
                except FileNotFoundError:
                    pass
    except Exception as e:
        print(f"[{subst_path}] Substitutes: {e}")

    # 4. Clean Keyboard Layout\Preload
    preload_path = f"{root_sid}\\Keyboard Layout\\Preload"
    try:
        with winreg.OpenKey(root, preload_path, 0, winreg.KEY_READ | winreg.KEY_WRITE) as k:
            winreg.SetValueEx(k, "1", 0, winreg.REG_SZ, "00000409")
            winreg.SetValueEx(k, "2", 0, winreg.REG_SZ, "00000401")
            for i in range(3, 10):
                try:
                    winreg.DeleteValue(k, str(i))
                    print(f"[{preload_path}] Deleted preload {i}")
                except FileNotFoundError:
                    pass
            print(f"[{preload_path}] Set Preload to 1=00000409, 2=00000401")
    except Exception as e:
        print(f"[{preload_path}] Preload: {e}")

for sid in [".DEFAULT", "S-1-5-18"]:
    wipe_root(sid)

print("Registry cleanup done.")
