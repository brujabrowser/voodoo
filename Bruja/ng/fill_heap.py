# Add the heap-execution column to address-db.db and rewrite the markdown.
import sqlite3

DB = r"C:\Users\grego\WASMVoodooCompile\Bruja\out\address-db.db"
MD = r"C:\Users\grego\WASMVoodooCompile\Bruja\out\address-db.md"
TSV = r"C:\Users\grego\WASMVoodooCompile\Bruja\out\heap-results.tsv"


def main():
    rows = []
    with open(TSV, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            name, heap = line.split("\t", 1)
            rows.append((heap, name))
    con = sqlite3.connect(DB)
    cols = [r[1] for r in con.execute("PRAGMA table_info(methods)")]
    if "heap" not in cols:
        con.execute("ALTER TABLE methods ADD COLUMN heap TEXT")
    con.executemany("UPDATE methods SET heap=? WHERE name=?", rows)
    missing = con.execute("SELECT COUNT(*) FROM methods WHERE heap IS NULL").fetchone()[0]
    ran = con.execute(
        "SELECT COUNT(*) FROM methods WHERE heap LIKE 'ran %'"
    ).fetchone()[0]
    n = con.execute("SELECT COUNT(*) FROM methods").fetchone()[0]
    if missing or ran != n:
        raise SystemExit("heap rows ran=%s methods=%s missing=%s" % (ran, n, missing))
    con.execute(
        "INSERT INTO meta(key,value) VALUES('heap','chpt+ept+tpt') "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"
    )
    con.commit()

    lines = [
        "# address db",
        "",
        "The database file is `C:\\Users\\grego\\WASMVoodooCompile\\Bruja\\out\\address-db.db`.",
        "",
        "dagger, voodoo, and mojo are the three addresses. `heap` is the result of calling the function those addresses name.",
        "The object is loaded from CHPT, the callee from EPT, and the type record from TPT. `ran` means that call returned.",
        "",
        "%d methods. %d heap calls returned." % (n, ran),
        "",
        "| name | dagger | voodoo | mojo | dagger log | voodoo log | mojo log | heap |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    query = """
      SELECT m.name, m.dagger, m.voodoo, m.mojo,
             d.log_size, v.log_size, j.log_size, m.heap
      FROM methods m
      LEFT JOIN replies d ON d.name = m.name AND d.space = 'dagger'
      LEFT JOIN replies v ON v.name = m.name AND v.space = 'voodoo'
      LEFT JOIN replies j ON j.name = m.name AND j.space = 'mojo'
      ORDER BY m.rowid
    """
    for name, dagger, voodoo, mojo, dl, vl, jl, heap in con.execute(query):
        lines.append(
            "| %s | %s | %s | %s | %s | %s | %s | %s |"
            % (
                name.replace("|", "/"),
                dagger,
                voodoo,
                mojo,
                "" if dl is None else dl,
                "" if vl is None else vl,
                "" if jl is None else jl,
                (heap or "").replace("|", "/"),
            )
        )
    with open(MD, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("methods", n, "ran", ran)
    con.close()


if __name__ == "__main__":
    main()
