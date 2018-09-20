#!/usr/bin/env python3


# =====
def main():
    with open("blank.jpg", "rb") as jpg_file:
        jpg_data = jpg_file.read()

    rows = [[]]
    for ch in jpg_data:
        if len(rows[-1]) > 20:
            rows.append([])
        rows[-1].append(hex(ch))

    text = ",\n\t".join(", ".join(row) for row in rows)
    text = "const unsigned char BLANK_JPG_DATA[] = {\n\t" + text + "\n};"
    text = "const unsigned long BLANK_JPG_SIZE = %d;\n\n" % (len(jpg_data)) + text
    text = "const unsigned BLANK_JPG_HEIGHT = 480;\n\n" + text
    text = "const unsigned BLANK_JPG_WIDTH = 640;\n" + text

    with open("src/blank.h", "w") as h_file:
        h_file.write(text)


# =====
if __name__ == "__main__":
    main()
