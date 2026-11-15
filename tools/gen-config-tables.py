#!/usr/bin/env python3 -I
"""Regenerates the tables in docs/configuration.md from EFI/OC/config.plist.
Usage (from the repo root): python3 -I tools/gen-config-tables.py
Replaces everything between the BEGIN/END markers in docs/configuration.md."""
import os, plistlib, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
oc = os.path.join(root, "EFI", "OC")
c = plistlib.load(open(os.path.join(oc, "config.plist"), "rb"))

def ver(bundle):
    try:
        d = plistlib.load(open(os.path.join(oc, "Kexts", bundle, "Contents", "Info.plist"), "rb"))
        return d.get("CFBundleShortVersionString") or d.get("CFBundleVersion", "?")
    except Exception:
        return "?"

def hx(v):
    return v.hex() if isinstance(v, (bytes, bytearray)) else v

out = ["### Kexts (load order)", "", "| # | Kext | Version | Enabled |", "|---|---|---|---|"]
for i, k in enumerate(c["Kernel"]["Add"]):
    out.append(f"| {i} | `{k['BundlePath']}` | {ver(k['BundlePath'])} | {'yes' if k.get('Enabled') else 'no'} |")
out += ["", "### ACPI tables (EFI/OC/ACPI)", "", "| SSDT | Enabled | Comment |", "|---|---|---|"]
for a in c["ACPI"]["Add"]:
    out.append(f"| `{a['Path']}` | {'yes' if a.get('Enabled') else 'no'} | {a.get('Comment', '')} |")
out += ["", "### ACPI patches", "", "| Comment | Enabled |", "|---|---|"]
for p in c["ACPI"]["Patch"]:
    out.append(f"| {p.get('Comment', '(no comment)')} | {'yes' if p.get('Enabled') else 'no'} |")
out += ["", "### UEFI drivers", "", "| Driver | Enabled |", "|---|---|"]
for d in c["UEFI"]["Drivers"]:
    path = d.get("Path") if isinstance(d, dict) else d
    out.append(f"| `{path}` | {'yes' if (not isinstance(d, dict) or d.get('Enabled')) else 'no'} |")
out += ["", "### DeviceProperties", ""]
for path, props in c["DeviceProperties"]["Add"].items():
    out.append(f"- `{path}`: " + ", ".join(f"`{k}`={hx(v)}" for k, v in props.items()))
nv = c["NVRAM"]["Add"]["7C436110-AB2A-4BBB-A880-FE41995C9F82"]
sec, boot = c["Misc"]["Security"], c["Misc"]["Boot"]
quirks = [k for k, v in c["Kernel"]["Quirks"].items() if v is True]
out += ["", "### NVRAM / security", "",
        f"- boot-args: `{nv.get('boot-args')}`",
        f"- csr-active-config: `{hx(nv.get('csr-active-config'))}`",
        f"- SecureBootModel: `{sec.get('SecureBootModel')}`, Vault: `{sec.get('Vault')}`, ScanPolicy: `{sec.get('ScanPolicy')}`",
        f"- LauncherOption: `{boot.get('LauncherOption')}`, PickerMode: `{boot.get('PickerMode')}`, ShowPicker: `{boot.get('ShowPicker')}`, Timeout: `{boot.get('Timeout')}`",
        f"- BlessOverride: `{c['Misc'].get('BlessOverride')}`",
        f"- SMBIOS model: `{c['PlatformInfo']['Generic'].get('SystemProductName')}`. The serial, MLB, UUID and ROM in this repo"
        " are placeholders: generate your own (see [installation.md](installation.md#2-generate-your-own-smbios)).",
        "- Kernel quirks enabled: " + ", ".join(f"`{q}`" for q in quirks)]

doc = os.path.join(root, "docs", "configuration.md")
text = open(doc).read()
begin, end = "<!-- BEGIN GENERATED TABLES -->", "<!-- END GENERATED TABLES -->"
if begin not in text or end not in text:
    sys.exit("docs/configuration.md is missing the generated-table markers")
head, rest = text.split(begin, 1)
_, tail = rest.split(end, 1)
open(doc, "w").write(head + begin + "\n" + "\n".join(out) + "\n" + end + tail)
print(f"docs/configuration.md tables regenerated ({len(out)} lines)")
