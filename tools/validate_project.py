from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
errors = []

for name in ("bc250_kmd.vcxproj", "bc250_umd.vcxproj"):
    path = root / name
    try:
        ET.parse(path)
    except Exception as exc:
        errors.append(f"XML inválido em {name}: {exc}")

required = [
    "src/kmd/bc250_kmd.cxx",
    "src/kmd/bc250_full_wddm.cxx",
    "src/gfx/bc250_gfx.c",
    "src/gfx/bc250_gfx.h",
    "src/gfx/bc250_gfx_regs.h",
    "src/umd/bc250_umd.c",
    "inf/bc250_kmd.inf",
    "inf/bc250_umd.inf",
    "firmware/manifest.txt",
]
for rel in required:
    if not (root / rel).is_file():
        errors.append(f"arquivo ausente: {rel}")

checks = {
    "bc250_kmd.vcxproj": ["BC250_ENABLE_FULL_WDDM=1", "BC250_GFX_OFFSETS_VALIDATED=0", "BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=0"],
    "bc250_umd.vcxproj": ["BC250_ENABLE_DX_UMD=1"],
    "src/kmd/bc250_full_wddm.hxx": ["#define BC250_ENABLE_FULL_WDDM 1"],
    "src/umd/bc250_umd.c": ["OpenAdapter10", "OpenAdapter11", "OpenAdapter12", "Bc250UmdOpenAdapter12Impl"],
    "inf/bc250_kmd.inf": ["bc250_umd.dll", "UserModeDriverName"],
}
for rel, needles in checks.items():
    text = (root / rel).read_text(encoding="utf-8", errors="replace")
    for needle in needles:
        if needle not in text:
            errors.append(f"{needle!r} ausente em {rel}")

for rel in ("src/kmd/bc250_full_wddm.cxx", "src/gfx/bc250_gfx.c"):
    text = (root / rel).read_text(encoding="utf-8", errors="replace")
    if "STATUS_NOT_SUPPORTED" in text:
        print(f"aviso: STATUS_NOT_SUPPORTED permanece em {rel}")

manifest = root / "firmware/manifest.txt"
if manifest.exists() and not re.search(r"SHA-256", manifest.read_text(errors="replace"), re.I):
    errors.append("manifest firmware não contém seção SHA-256 reconhecível")

if errors:
    for error in errors:
        print(f"ERRO: {error}")
    sys.exit(1)

print("VALIDATION_OK")
print(f"root={root}")
print("xml=ok")
print("required_files=ok")
print("activation_gates=ok (full-WDDM ativo; offsets de engine/IH aguardam validação)")
print("inf_umd_registration=ok")
