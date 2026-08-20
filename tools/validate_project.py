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
    "src/firmware/bc250_psp.c",
    "src/firmware/bc250_psp.h",
    "src/umd/bc250_umd.c",
    "inf/bc250_kmd.inf",
    "inf/bc250_umd.inf",
    "firmware/manifest.txt",
    "tools/validate_psp_protocol.py",
]
for rel in required:
    if not (root / rel).is_file():
        errors.append(f"arquivo ausente: {rel}")

checks = {
    "bc250_kmd.vcxproj": ["BC250_ENABLE_FULL_WDDM=1", "BC250_GFX_OFFSETS_VALIDATED=0", "BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=0", "BC250_PSP_RING_VALIDATED=0", "BC250_PSP_HDP_OFFSETS_VALIDATED=0"],
    "bc250_umd.vcxproj": ["BC250_ENABLE_DX_UMD=1"],
    "src/kmd/bc250_full_wddm.hxx": ["#define BC250_ENABLE_FULL_WDDM 1"],
    "src/firmware/bc250_psp.h": ["BC250_PSP_C2PMSG_64", "BC250_PSP_C2PMSG_67", "BC250_PSP_C2PMSG_TOS_READY", "BC250_PSP_COMMAND_SETUP_TMR", "Bc250PspAttest", "Bc250PspLoadFirmware"],
    "src/kmd/bc250_kmd.hxx": ["BC250_PSP_STATE Psp"],
    "src/kmd/bc250_kmd.cxx": ["Bc250PspInitializeState", "Bc250PspCreateRing", "Bc250PspReleaseState"],
    "src/umd/bc250_umd.c": ["OpenAdapter10", "OpenAdapter11", "OpenAdapter12", "Bc250UmdOpenAdapter12Impl"],
    "inf/bc250_kmd.inf": ["bc250_umd.dll", "UserModeDriverName"],
}
for rel, needles in checks.items():
    text = (root / rel).read_text(encoding="utf-8", errors="replace")
    for needle in needles:
        if needle not in text:
            errors.append(f"{needle!r} ausente em {rel}")

protocol_check = root / "tools/validate_psp_protocol.py"
if protocol_check.exists():
    import subprocess
    completed = subprocess.run([sys.executable, str(protocol_check)], capture_output=True, text=True)
    if completed.returncode != 0:
        errors.append("validação offline do protocolo PSP falhou")

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
print("activation_gates=ok (full-WDDM ativo; offsets de engine/IH/PSP/HDP aguardam validação)")
print("inf_umd_registration=ok")
print("psp_protocol_layout=ok")
