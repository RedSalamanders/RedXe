"""Validate repository skill metadata on developer machines and clean CI runners."""
from pathlib import Path
import re
import sys

import yaml


def validate_skill(folder):
    path = folder / "SKILL.md"
    if not path.is_file():
        return "missing SKILL.md"
    text = path.read_text(encoding="utf-8")
    match = re.match(r"\A---\n(.*?)\n---(?:\n|$)", text, re.DOTALL)
    if not match:
        return "missing YAML front matter"
    try:
        metadata = yaml.safe_load(match[1])
    except yaml.YAMLError as error:
        return f"invalid YAML: {error}"
    if not isinstance(metadata, dict):
        return "front matter must be a mapping"
    if set(metadata) - {"name", "description", "license", "allowed-tools", "metadata"}:
        return "unsupported front matter key"
    name = metadata.get("name")
    if not isinstance(name, str) or name != folder.name or len(name) > 64 or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", name):
        return "name must match the folder and use lowercase hyphen-separated words"
    description = metadata.get("description")
    if not isinstance(description, str) or not description.strip() or len(description) > 1024 or "<" in description or ">" in description:
        return "invalid description"
    if description.lstrip().startswith("[TODO:"):
        return "unfinished description"
    body = text[match.end():]
    if not body.strip():
        return "empty skill instructions"
    fence = None
    for line in body.splitlines():
        marker = re.match(r"^[ \t]*(?:(?:[-+*]|\d+[.)])[ \t]+)?(`{3,}|~{3,})(.*)$", line)
        if marker:
            if fence is None:
                fence = marker[1]
            elif marker[1][0] == fence[0] and len(marker[1]) >= len(fence) and not marker[2].strip():
                fence = None
        elif fence is None and re.fullmatch(r"[ ]{0,3}\[TODO:[^\n]*\][ \t]*", line):
            return "unfinished instructions"
    return None


def main():
    root = Path(__file__).resolve().parents[1] / ".agents" / "skills"
    folders = sorted(path for path in root.iterdir() if path.is_dir())
    failures = [] if folders else ["No repository skills found"]
    for folder in folders:
        error = validate_skill(folder)
        if error:
            failures.append(f"{folder.name}: {error}")
        else:
            print(f"Validated {folder.name}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
