"""Check a Nitoryu translation file against en.ini (Python 3, no extra packages).

    python lang_check.py ru.ini          (run it in this folder)
    python lang_check.py                 (checks every <code>.ini here)

Reports: keys missing from the translation (English will show there), keys
en.ini does not have (typos: never shown), {placeholders} that differ from
English, texts longer than the "max N" noted in en.ini, and files that are not
UTF-8. Prints the share of texts translated. Exit code 1 if an error was found.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAX = re.compile(r"max\s+(\d+)")
PLACEHOLDER = re.compile(r"\{[^{}]*\}")


def parse(path):
    """(texts {key: value}, limits {key: max}, problems [str])."""
    problems = []
    raw = open(path, "rb").read()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
        problems.append("note: the file starts with a UTF-8 BOM (fine, but not needed)")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        return {}, {}, ["ERROR: not UTF-8 (%s). Save the file as UTF-8." % e]
    texts, limits, pending = {}, {}, None
    for n, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if not s or s.startswith("["):
            continue
        if s.startswith(";") or s.startswith("#"):
            m = MAX.search(s)
            if m:
                pending = int(m.group(1))
            continue
        if "=" not in s:
            problems.append("ERROR: line %d has no '=': %s" % (n, s))
            continue
        key, value = s.split("=", 1)
        key, value = key.strip(), value.strip()
        if key in texts:
            problems.append("ERROR: line %d repeats the key %s" % (n, key))
        texts[key] = value
        if pending is not None:
            limits[key] = pending
            pending = None
    return texts, limits, problems


def check(path, ref, limits):
    texts, _, problems = parse(path)
    errors = [p for p in problems if p.startswith("ERROR")]
    notes = [p for p in problems if not p.startswith("ERROR")]
    for key in sorted(set(ref) - set(texts)):
        notes.append("missing (English is shown): %s" % key)
    for key in sorted(set(texts) - set(ref)):
        errors.append("ERROR: unknown key (never shown; a typo?): %s" % key)
    for key in sorted(set(texts) & set(ref)):
        if not texts[key]:
            notes.append("empty (English is shown): %s" % key)
            continue
        want = sorted(PLACEHOLDER.findall(ref[key]))
        got = sorted(PLACEHOLDER.findall(texts[key]))
        if want != got:
            errors.append("ERROR: %s must keep the placeholders %s (has %s)" % (key, want, got))
        limit = limits.get(key)
        shown = texts[key].replace("\\n", "\n")
        if limit and max(len(part) for part in shown.split("\n")) > limit:
            notes.append("too long for its place (%d > max %d): %s" % (len(shown), limit, key))
    done = len([k for k in ref if texts.get(k)])
    print("%s: %d of %d texts (%d%%)" % (os.path.basename(path), done, len(ref), 100 * done // max(1, len(ref))))
    for line in errors + notes:
        print("  " + line)
    return not errors


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")    # a console that cannot print the language
    ref_path = os.path.join(HERE, "en.ini")
    ref, limits, problems = parse(ref_path)
    if not ref:
        print("en.ini not found or unreadable next to this script: %s" % problems)
        return 1
    files = [os.path.abspath(a) for a in sys.argv[1:]] or sorted(
        os.path.join(HERE, f) for f in os.listdir(HERE) if f.endswith(".ini") and f != "en.ini")
    ok = True
    for f in files:
        ok = check(f, ref, limits) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
