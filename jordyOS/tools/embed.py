from pathlib import Path
programs = sorted(Path("programs").glob("*.ape"))
with Path("ape/programs.h").open("w") as out:
    out.write("/* Generated from Apelang programs; do not edit. */\n")
    for index, path in enumerate(programs):
        data = path.read_bytes()
        if len(data) > 16384 or len(path.name.encode()) > 31:
            raise ValueError(f"Program exceeds guest filesystem limits: {path}")
        out.write(f"static const char program_{index}[] = {{")
        out.write(",".join(str(b) for b in data))
        out.write(",0};\n")
    out.write("static const struct { const char* name; const char* source; unsigned size; } embedded_programs[] = {\n")
    for index, path in enumerate(programs):
        out.write(f'  {{"{path.name}",program_{index},sizeof(program_{index})-1}},\n')
    out.write("};\n")
