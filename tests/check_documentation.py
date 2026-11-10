#!/usr/bin/env python3
"""Check Markdown relative links, heading fragments and RU/EN page parity."""
from __future__ import annotations

import re
import sys
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP = {".git", ".venv", "venv", "build", "target", ".analysis-tmp", "node_modules", "__pycache__"}
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
HEADING = re.compile(r"^#{1,6}\s+(.+?)\s*#*$")


def heading_levels(path: Path) -> list[int]:
    levels = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^(#{1,6})\s+", line)
        if match:
            levels.append(len(match.group(1)))
    return levels


def heading_slugs(text: str) -> list[str]:
    used: dict[str, int] = {}
    slugs: list[str] = []
    for line in text.splitlines():
        match = HEADING.match(line)
        if not match:
            continue
        heading = match.group(1).strip()
        heading = re.sub(r"!\[([^\]]*)\]\([^)]*\)", r"\1", heading)
        heading = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", heading)
        heading = re.sub(r"`([^`]*)`", r"\1", heading)
        slug = heading.lower().replace(" ", "-")
        slug = re.sub(r"[^\w-]", "", slug, flags=re.UNICODE)
        count = used.get(slug, 0)
        used[slug] = count + 1
        if count:
            slug = f"{slug}-{count}"
        slugs.append(slug)
    return slugs


def main() -> int:
    markdown = [
        path for path in ROOT.rglob("*.md")
        if not any(part in SKIP for part in path.relative_to(ROOT).parts)
    ]
    missing: list[tuple[str, str]] = []
    bad_fragments: list[tuple[str, str, str]] = []
    relative_links = 0

    for source in markdown:
        text = source.read_text(encoding="utf-8-sig")
        for match in LINK.finditer(text):
            raw = match.group(1).strip()
            if raw.startswith("<") and raw.endswith(">"):
                raw = raw[1:-1]
            raw = raw.split(" ", 1)[0].strip("<>")
            if not raw or raw.startswith(("http://", "https://", "mailto:", "tel:", "data:")):
                continue
            relative_links += 1
            url = urllib.parse.urlsplit(raw)
            destination = urllib.parse.unquote(url.path)
            fragment = urllib.parse.unquote(url.fragment)

            if destination:
                target = (source.parent / destination).resolve()
                if target.is_dir():
                    target = target / "README.md"
                if not target.exists():
                    missing.append((source.relative_to(ROOT).as_posix(), raw))
                    continue
            else:
                target = source

            if fragment:
                target_text = target.read_text(encoding="utf-8-sig")
                if fragment not in heading_slugs(target_text) and not re.search(
                    r"<a\s+[^>]*id=[\"']" + re.escape(fragment) + r"[\"']",
                    target_text,
                    re.IGNORECASE,
                ):
                    bad_fragments.append(
                        (source.relative_to(ROOT).as_posix(), raw, target.relative_to(ROOT).as_posix())
                    )

    parity_errors: list[str] = []
    pair_counts: dict[str, tuple[int, int, int]] = {}
    for group in ("reference", "how-to", "explanation"):
        en = {path.name: path for path in (ROOT / "docs" / "eng" / group).glob("*.md")}
        ru = {path.name: path for path in (ROOT / "docs" / "ru" / group).glob("*.md")}
        missing_ru = sorted(set(en) - set(ru))
        missing_en = sorted(set(ru) - set(en))
        mismatches = []
        for name in sorted(set(en) & set(ru)):
            if heading_levels(en[name]) != heading_levels(ru[name]):
                mismatches.append(name)
        pair_counts[group] = (len(en), len(ru), len(mismatches))
        if missing_ru or missing_en or mismatches:
            parity_errors.append(
                f"{group}: missing_ru={missing_ru}, missing_en={missing_en}, "
                f"heading_mismatches={mismatches}"
            )

    print(
        f"MARKDOWN={len(markdown)} RELATIVE_LINKS={relative_links} "
        f"MISSING={len(missing)} BAD_FRAGMENTS={len(bad_fragments)}"
    )
    for group, (en_count, ru_count, mismatches) in pair_counts.items():
        print(f"PAIR {group}: EN={en_count} RU={ru_count} HEADING_LEVEL_MISMATCHES={mismatches}")
    for item in missing[:30]:
        print("MISSING", *item)
    for item in bad_fragments[:30]:
        print("BAD_FRAGMENT", *item)
    for item in parity_errors:
        print("PAIR_ERROR", item)

    return 1 if missing or bad_fragments or parity_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
