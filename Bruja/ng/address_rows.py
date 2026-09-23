# dagger is FNV-1 of the method name. voodoo and mojo come from the portfolio.
import sys

PORT = r"C:\Users\grego\Bruja\out\mojovm-portfolio.txt"


def fnv1(s):
    h = 2166136261
    for b in s.encode("utf-8"):
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= b
    h &= 0x7FFFFFFF
    return h or 1


def main():
    out = sys.stdout
    if len(sys.argv) > 1:
        out = open(sys.argv[1], "w", encoding="utf-8", newline="\n")
    n = 0
    with open(PORT, "r", encoding="utf-8") as f:
        for line in f:
            if not line or line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4:
                continue
            name, voodoo, mojo = parts[0], parts[1], parts[2]
            method = name.rsplit(".", 1)[-1]
            out.write(
                "%s\t%s\t%s\t%s\t%s\n" % (name, method, fnv1(method), voodoo, mojo)
            )
            n += 1
    if out is not sys.stdout:
        out.close()
    sys.stderr.write("rows %d\n" % n)


if __name__ == "__main__":
    main()
