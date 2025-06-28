# 🎮 League of Legends Skins Collection & WAD Tools

Welcome to the **Shinano LOL Repository** — a custom-skin archive and toolset for League of Legends enthusiasts.

---

## 📦 Skins Download Collection

This repo contains **pre-packed `.zip` files** for various custom skins. These can be imported directly into tools like [**CSLOL Manager**](https://github.com/LoL-Fantome/CSLoL-Manager).

### 🛠 How to Use Skins

1. **Download** any `.zip` file from the `skins/` folder.
2. Open CSLOL Manager (or similar custom skin tool).
3. Import the downloaded skin `.zip`.
4. Launch **League of Legends** and select the **base/default skin** for the champion.
5. Enjoy your modded skin!

> ✅ *You must use the **base/default** skin for the custom skin to show in-game.*

---

## 🧰 Local WAD Extractor (Mac/Web)

Included in this repo is a fully working **WAD WebUI Tool** that lets you:
- Upload `.wad.client` files from League's game client
- View, extract, and inspect embedded `.bin`, `.dds`, `.skl`, and other assets
- Use it locally in a browser with a dark-themed UI

### 🚀 How to Use the Extractor

#### Prerequisites

- Python 3.10+
- `pip install flask xxhash`

#### Run the WebUI

```bash
cd wad-webui
python app.py
```

Then open: http://localhost:5000
Drag in any .wad.client file and the tool will extract its contents.

## 🧰 Local BIN Decoder (Python CLI)

Included in this repo is **`ritobin_mac.py`** – a standalone script that:

- Reads any Riot `.bin` file (character skins, VFX, audio, etc.)
- Resolves every hashed field/type name using Riot’s official hash tables
- Emits a clean `.py` module (`data = {...}`) you can import or inspect

### 🚀 How to Use the Decoder

#### Prerequisites

- Python 3.9 or newer  
- Install one tiny dependency:  
  ```bash
  pip install xxhash
  
# from the repo root
python bin-webui/ritobin_mac.py path/to/skin*.bin
(All credit goes to moonshadow565 (https://github.com/moonshadow565/ritobin) and other contributer) (My only job here is ported their code from Windows to MacOS and from C++ to Python)

## 💻 How to Download This Repository (on Windows)

If you want the whole collection:

1. Install **Git** for Windows: [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Install **7-Zip** (optional, to extract downloaded zip files): [https://www.7-zip.org/](https://www.7-zip.org/)
3. Open **Git Bash** (comes with Git), and run the following:

```bash
git clone https://github.com/ShinanoMusashi/Shinano_LOL.git
```

## **GLHF**
---

DM me if you'd like to add:
- Images/screenshots of skins
- A list of included champions
- A new champion skin

If you need help, want to request a fix, or have an idea for improvement:
- Message me on Discord: monika_base64
- Or open an issue on this GitHub repo

## **IMPORTANT**
This project is for learning purposes only. Commercial use or any illegal activity is prohibited. Any direct or indirect consequences occured from the use of this project will be beared solely by the user, not by the author.
By using this project, you fully understand and accept the above terms
