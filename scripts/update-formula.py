#!/usr/bin/env python3
"""Set the release fields of a Homebrew formula."""

import re
import sys

# The order brew style requires.
ORDER = ("url", "version", "sha256", "license", "head")
PATTERNS = {f: re.compile(r'^  %s "(.*)"$' % f) for f in ORDER}


def update(text, url, version, sha256):
    values = {"url": url, "version": version, "sha256": sha256}
    found = {}
    lines = []
    for line in text.split("\n"):
        for field, pattern in PATTERNS.items():
            match = pattern.match(line)
            if match:
                found.setdefault(field, match.group(1))
                line = None
                break
        if line is not None:
            lines.append(line)
    for field in ("license", "head"):
        if field not in found:
            sys.exit("the formula has no %s" % field)
    block = ['  %s "%s"' % (f, values[f] if f in values else found[f]) for f in ORDER]
    out = []
    for line in lines:
        out.append(line)
        if line.startswith("  homepage "):
            out.extend(block)
    text = "\n".join(out)
    return re.sub(r"\n{3,}", "\n\n", text)


def main():
    if len(sys.argv) != 5:
        sys.exit("usage: update-formula.py FORMULA URL VERSION SHA256")
    path, url, version, sha256 = sys.argv[1:]
    with open(path) as handle:
        text = handle.read()
    text = update(text, url, version, sha256)
    with open(path, "w") as handle:
        handle.write(text)


if __name__ == "__main__":
    main()
