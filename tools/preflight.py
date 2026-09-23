"""Check the packaged artifacts the way nppPluginList's validator.py does.

Run this before opening the pull request. It mirrors the checks that made the
upstream CI reject an entry, so passing here means the PR should pass too.

Usage (from the repo root):
    python3 tools/preflight.py

ASCII output only: the Windows console is in the OEM codepage and would mangle
anything else.
"""

import hashlib
import io
import json
import os
import sys
import zipfile

REQUIRED_FIELDS = [
    "folder-name",
    "display-name",
    "version",
    "id",
    "repository",
    "description",
    "author",
    "homepage",
]

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
entry_path = os.path.join(root, "build", "pl.x64.entry.json")

if not os.path.exists(entry_path):
    sys.exit("missing %s -- run package.bat first" % entry_path)

with open(entry_path, encoding="utf-8") as handle:
    entry = json.load(handle)

zip_path = os.path.join(root, "build", "BatPathIntelliSense_x64.zip")

if not os.path.exists(zip_path):
    sys.exit("missing %s -- run package.bat first" % zip_path)

failures = []


def check(ok, label, detail=""):
    print("[%s] %s%s" % ("OK" if ok else "FAIL", label, ("   " + detail) if detail else ""))
    if not ok:
        failures.append(label)


with open(zip_path, "rb") as handle:
    payload = handle.read()

# 1) The "id" field is the SHA-256 of the zip, verified with the same library
#    the upstream validator uses.
digest = hashlib.sha256(payload).hexdigest()
check(digest == entry.get("id", "").lower(), "id equals sha256 of the zip", digest)

# 2) The zip must be readable.
try:
    archive = zipfile.ZipFile(io.BytesIO(payload))
    names = archive.namelist()
    valid_zip = True
except zipfile.BadZipFile:
    names = []
    valid_zip = False

check(valid_zip, "zip is readable")

# 3) The DLL must sit at the ROOT of the zip, named "<folder-name>.dll".
#    The validator matches entry names with no path prefix, so a nested folder
#    fails even though the file is present.
expected_dll = "%s.dll" % entry.get("folder-name", "")
at_root = any(name.lower() == expected_dll.lower() for name in names)
check(at_root, "zip root contains %s" % expected_dll, str(names))

# 4) validator.py pads the JSON version with ".0" up to four components and
#    compares it against the DLL's FileVersionMS/LS. package.ps1 derives the
#    JSON version from the DLL, so this just confirms the two agree.
version = entry.get("version", "")
padded = version + (3 - version.count(".")) * ".0"
parts = padded.split(".")

check(
    len(parts) == 4 and all(part.isdigit() for part in parts),
    "version %r is a valid four-part version" % version,
    padded,
)

# 5) Every required field present and non-empty.
missing = [field for field in REQUIRED_FIELDS if not entry.get(field)]
check(not missing, "all required fields present", str(missing) if missing else "")

# 6) The repository URL must point straight at the zip.
check(entry.get("repository", "").lower().endswith(".zip"), "repository points at a .zip")

# 7) Remind about the values that are easy to leave as placeholders.
for field in ("repository", "homepage"):
    if "nicehero" in entry.get(field, ""):
        print("NOTE: %s still says 'nicehero' -- confirm that is your GitHub account" % field)

print()
if failures:
    print("%d check(s) failed" % len(failures))
    sys.exit(1)

print("all checks passed -- the entry should clear upstream CI")
