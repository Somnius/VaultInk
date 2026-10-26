"""VaultInk home/menu mock-ups, 480x800 portrait, 4 grey levels (e-ink)."""
from xml.sax.saxutils import escape

W, H = 480, 800
BK, DG, LG, WH = "#000", "#555", "#aaa", "#fff"


def t(x, y, s, size=20, w="normal", fill=BK, anchor="start"):
    return (f'<text x="{x}" y="{y}" font-family="Ubuntu" font-size="{size}" font-weight="{w}" '
            f'fill="{fill}" text-anchor="{anchor}">{escape(s)}</text>')


def rect(x, y, w, h, fill=WH, stroke=None, sw=2, r=0):
    st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"{st}/>'


def logo(x, y, s, fill=BK, bg=WH):
    k = s / 96.0
    def p(pts): return " ".join(f"{x + (px - 12) * k:.1f},{y + (py - 28) * k:.1f}" for px, py in pts)
    return (f'<polygon fill="{fill}" points="{p([(12,28),(60,44),(108,28),(108,90),(60,100),(12,90)])}"/>'
            f'<polygon fill="{bg}" points="{p([(18,37),(55.26,92.88),(18,85.12)])}"/>'
            f'<polygon fill="{bg}" points="{p([(102,37),(102,85.12),(64.74,92.88)])}"/>'
            f'<polygon fill="{fill}" points="{p([(12,28),(60,44),(108,28),(60,100)])}"/>')


# Simple line icons in a 32x32 box at (x, y).
def icon(kind, x, y, c=BK, s=32):
    k = s / 32
    def P(d): return f'<path d="{d}" transform="translate({x},{y}) scale({k})" fill="none" stroke="{c}" stroke-width="2.5" stroke-linejoin="round" stroke-linecap="round"/>'
    if kind == "books":
        return P("M4 6h8v22H4zM14 6h6v22h-6zM22 8l6-1.5 4 20-6 1.5z")
    if kind == "vault":
        return f'<g transform="translate({x},{y + 4}) scale({k})">{logo(0, 0, 32)}</g>'
    if kind == "folder":
        return P("M3 9h10l3 3h13v15H3z")
    if kind == "search":
        return P("M13 4a9 9 0 1 1 0 18a9 9 0 1 1 0-18zM20 20l8 8")
    if kind == "transfer":
        return P("M16 4v16M9 13l7 7 7-7M5 26h22")
    if kind == "plugin":
        return P("M6 10h6a4 4 0 1 1 8 0h6v16H6z")
    if kind == "gear":
        g = f'<g transform="translate({x},{y}) scale({k})" fill="none" stroke="{c}">'
        g += '<circle cx="16" cy="16" r="10" stroke-width="5" stroke-dasharray="4 3.85"/>'
        g += '<circle cx="16" cy="16" r="7.5" stroke-width="2.5"/><circle cx="16" cy="16" r="3" stroke-width="2.5"/></g>'
        return g
    if kind == "stats":
        return P("M5 27h22M8 24v-8M14 24V9M20 24v-11M26 24V5")
    if kind == "note":
        return P("M7 4h13l6 6v18H7zM20 4v6h6M11 15h10M11 20h10M11 25h6")
    if kind == "home":
        return P("M4 15L16 5l12 10M8 13v14h16V13")
    if kind == "light":
        return P("M16 4v3M16 25v3M4 16h3M25 16h3M8 8l2 2M22 22l2 2M24 8l-2 2M8 24l2-2M16 10a6 6 0 1 1 0 12a6 6 0 1 1 0-12z")
    if kind == "wifi":
        return P("M4 13a17 17 0 0 1 24 0M8 18a11 11 0 0 1 16 0M12 23a5 5 0 0 1 8 0M16 27h0.1")
    if kind == "sync":
        return P("M26 12a10 10 0 0 0-19-2M6 20a10 10 0 0 0 19 2M7 4v6h6M25 28v-6h-6")
    if kind == "rotate":
        return P("M8 6h12v20H8zM24 10a8 8 0 0 1 0 12")
    if kind == "sleep":
        return P("M24 20A10 10 0 1 1 13 6a8 8 0 0 0 11 14z")
    if kind == "font":
        return P("M5 26L13 6l8 20M8 19h10M21 26l4-10 4 10M22.5 23h5")
    if kind == "layout":
        return P("M6 8h20M6 14h20M6 20h20M6 26h12")
    if kind == "toc":
        return P("M6 8h2M12 8h14M6 16h2M12 16h14M6 24h2M12 24h14")
    if kind == "bookmark":
        return P("M9 4h14v24l-7-6-7 6z")
    return ""


def status(dark=False):
    c = WH if dark else BK
    out = rect(0, 0, W, 40, BK if dark else WH)
    out += t(16, 27, "14:05", 18, "bold", c) + t(W - 16, 27, "86%", 18, "bold", c, "end")
    out += rect(W - 92, 14, 26, 14, "none", c, 2, 2) + rect(W - 89, 17, 17, 8, c)
    return out


def svg(body, name):
    s = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
         f'<rect width="{W}" height="{H}" fill="{WH}"/>{body}</svg>')
    open(f"{name}.svg", "w").write(s)


def cover(x, y, w, h, title):
    out = rect(x, y, w, h, DG, BK, 2, 4) + rect(x + 8, y + 10, w - 16, 3, LG) + rect(x + 8, y + h - 24, w - 16, 3, LG)
    out += t(x + w / 2, y + h / 2 + 6, title, 16, "bold", WH, "middle")
    return out


def continue_card(y, kind="book"):
    out = rect(16, y, W - 32, 168, WH, BK, 3, 10)
    if kind == "book":
        out += cover(32, y + 16, 92, 136, "Dune")
        out += t(140, y + 34, "CONTINUE READING", 14, "bold", DG)
        out += t(140, y + 66, "Dune", 26, "bold") + t(140, y + 92, "Frank Herbert", 18, fill=DG)
        out += rect(140, y + 122, 280, 10, WH, BK, 2, 5) + rect(140, y + 122, 120, 10, BK, None, 0, 5)
        out += t(140, y + 152, "43%  ·  2 h 10 min left", 15, fill=DG)
    return out


# --- Direction A: "Two big doors" ---------------------------------------
def dir_a():
    b = status()
    b += logo(16, 52, 40) + t(66, 80, "VaultInk", 24, "bold")
    b += continue_card(104)
    y = 292
    for i, (k, title, sub) in enumerate([("books", "Books", "128 books"), ("vault", "Vaults", "2 vaults · 3,412 notes")]):
        x = 16 + i * 228
        b += rect(x, y, 220, 180, BK if i == 1 else WH, BK, 3, 12)
        c = WH if i == 1 else BK
        if i == 1:
            b += f'<g>{logo(x + 20, y + 26, 64, WH, BK)}</g>'
        else:
            b += icon("books", x + 20, y + 20, c, 64)
        b += t(x + 20, y + 130, title, 28, "bold", c) + t(x + 20, y + 158, sub, 15, fill=LG if i == 1 else DG)
    b += t(16, 508, "RECENT NOTES", 14, "bold", DG)
    for i, n in enumerate(["Trip plan", "Reading list", "Ideas"]):
        yy = 520 + i * 52
        b += rect(16, yy, W - 32, 46, WH, LG, 2, 8) + icon("note", 28, yy + 9, BK, 28) + t(68, yy + 30, n, 19)
        b += t(W - 30, yy + 30, ["My Vault", "My Vault", "Work"][i], 14, fill=DG, anchor="end")
    y = 690
    for i, (k, l) in enumerate([("folder", "Files"), ("search", "Search"), ("transfer", "Transfer"), ("plugin", "Plugins"), ("gear", "Settings")]):
        x = 16 + i * 90
        b += rect(x, y, 84, 84, WH, BK, 2, 10) + icon(k, x + 26, y + 12) + t(x + 42, y + 70, l, 14, "bold", anchor="middle")
    svg(b, "A-home-doors")


# --- Direction B: "Calm list" --------------------------------------------
def dir_b():
    b = status()
    b += continue_card(52)
    rows = [("books", "Books", "128 books · 3 in progress", True), ("vault", "Vaults", "My Vault, Work · 3,412 notes", True),
            ("search", "Search", "Books, notes and tags", False), ("stats", "Reading stats", "45 min today · 7-day streak", False),
            ("folder", "Files", "SD card", False), ("transfer", "Transfer", "Wi-Fi, USB, Calibre", False),
            ("plugin", "Plugins", "3 installed", False), ("gear", "Settings", "Display, reading, controls", False)]
    y = 236
    for k, title, sub, big in rows:
        h = 76 if big else 56
        b += rect(16, y, W - 32, h, WH, BK if big else LG, 3 if big else 2, 10)
        isz = 40 if big else 30
        b += icon(k, 32, y + (h - isz) / 2, BK, isz)
        b += t(88, y + (34 if big else 28), title, 24 if big else 20, "bold")
        b += t(88, y + (58 if big else 46), sub, 15, fill=DG)
        b += t(W - 34, y + h / 2 + 8, "›", 28, "bold", DG, "end")
        y += h + 6
    svg(b, "B-home-list")


# --- Direction C: "Tabs" ---------------------------------------------------
def dir_c():
    b = status()
    b += t(16, 82, "Good afternoon", 28, "bold") + t(16, 108, "Monday 26 October", 16, fill=DG)
    b += t(16, 148, "CONTINUE", 14, "bold", DG)
    for i, (title, sub) in enumerate([("Dune", "43%"), ("Trip plan", "note"), ("Project", "canvas")]):
        x = 16 + i * 152
        if i == 0:
            b += cover(x, 160, 140, 196, title)
        else:
            b += rect(x, 160, 140, 196, WH, BK, 2, 6)
            b += icon("note" if i == 1 else "layout", x + 54, 196, BK, 32)
            for j in range(5):
                b += rect(x + 16, 250 + j * 14, 108 - (j % 2) * 30, 4, LG)
            b += t(x + 70, 340, title, 15, "bold", anchor="middle")
        b += t(x + 70, 378, sub, 14, fill=DG, anchor="middle")
    b += t(16, 422, "VAULTS", 14, "bold", DG)
    for i, (n, c) in enumerate([("My Vault", "2,980 notes · synced 5 min ago"), ("Work", "432 notes · not synced")]):
        yy = 434 + i * 74
        b += rect(16, yy, W - 32, 66, WH, BK, 2, 10) + logo(30, yy + 18, 40) + t(84, yy + 30, n, 20, "bold") + t(84, yy + 52, c, 14, fill=DG)
    b += t(16, 600, "TODAY", 14, "bold", DG)
    b += rect(16, 612, W - 32, 70, WH, LG, 2, 10) + icon("stats", 30, 630, BK, 32)
    b += t(80, 642, "45 min reading", 20, "bold") + t(80, 664, "7-day streak · 18 pages", 14, fill=DG)
    b += rect(0, 700, W, 100, WH) + rect(0, 700, W, 2, BK)
    for i, (k, l) in enumerate([("home", "Home"), ("books", "Books"), ("vault", "Vaults"), ("search", "Search"), ("gear", "More")]):
        x = i * 96
        if i == 0:
            b += rect(x + 8, 708, 80, 84, BK, None, 0, 10)
        c = WH if i == 0 else BK
        b += icon(k, x + 32, 720, c) + t(x + 48, 778, l, 14, "bold", c, "middle")
    svg(b, "C-home-tabs")


# --- Shared: quick settings (swipe down) ----------------------------------
def quick():
    b = status()
    b += rect(0, 40, W, 760, LG) + rect(16, 60, W - 32, 18, DG, None, 0, 4)
    for j in range(20):
        b += rect(24, 100 + j * 32, W - 48, 6, WH)
    b += rect(0, 40, W, 430, WH) + rect(0, 468, W, 3, BK)
    b += t(16, 80, "Quick settings", 22, "bold")
    tiles = [("light", "Light", True), ("wifi", "Wi-Fi", False), ("sync", "Sync now", False), ("rotate", "Rotate", False),
             ("sleep", "Sleep", False), ("transfer", "Transfer", False)]
    for i, (k, l, on) in enumerate(tiles):
        x, y = 16 + (i % 3) * 152, 100 + (i // 3) * 110
        b += rect(x, y, 144, 100, BK if on else WH, BK, 2, 12)
        c = WH if on else BK
        b += icon(k, x + 16, y + 16, c) + t(x + 16, y + 82, l, 18, "bold", c)
    b += icon("light", 16, 330, BK, 28) + rect(56, 338, 360, 12, WH, BK, 2, 6) + rect(56, 338, 220, 12, BK, None, 0, 6) + t(W - 16, 350, "60%", 16, "bold", anchor="end")
    b += t(16, 404, "Warm", 16, "bold") + rect(76, 392, 340, 12, WH, BK, 2, 6) + rect(76, 392, 120, 12, BK, None, 0, 6) + t(W - 16, 404, "35%", 16, "bold", anchor="end")
    b += rect(W / 2 - 30, 446, 60, 6, DG, None, 0, 3)
    svg(b, "D-quick-settings")


# --- Shared: reading menu (swipe up) --------------------------------------
def reader_menu():
    b = rect(0, 0, W, H, WH)
    for j in range(14):
        b += rect(24, 40 + j * 30, W - 48 - (j % 4) * 20, 8, LG)
    b += rect(0, 440, W, 360, WH) + rect(0, 440, W, 3, BK) + rect(W / 2 - 30, 452, 60, 6, DG, None, 0, 3)
    b += t(16, 494, "Dune", 20, "bold") + t(W - 16, 494, "p. 212 / 498", 16, fill=DG, anchor="end")
    b += rect(16, 506, W - 32, 8, WH, BK, 2, 4) + rect(16, 506, 190, 8, BK, None, 0, 4)
    for i, (k, l) in enumerate([("toc", "Contents"), ("bookmark", "Bookmark"), ("search", "Search"), ("note", "Highlights")]):
        x = 16 + i * 114
        b += rect(x, 532, 106, 92, WH, BK, 2, 10) + icon(k, x + 37, 546) + t(x + 53, 608, l, 15, "bold", anchor="middle")
    b += icon("font", 16, 648, BK, 28) + t(56, 670, "Aa", 18) + rect(90, 656, 290, 12, WH, BK, 2, 6) + rect(90, 656, 130, 12, BK, None, 0, 6) + t(W - 16, 670, "Aa", 28, anchor="end")
    for i, l in enumerate(["Font", "Spacing", "Margins", "More"]):
        x = 16 + i * 114
        b += rect(x, 700, 106, 48, BK if i == 0 else WH, BK, 2, 24) + t(x + 53, 731, l, 16, "bold", WH if i == 0 else BK, "middle")
    b += t(W / 2, 784, "Swipe down or press Back to close", 14, fill=DG, anchor="middle")
    svg(b, "E-reading-menu")


# --- Shared: grouped settings ---------------------------------------------
def settings():
    b = status()
    b += t(16, 84, "‹", 34, "bold") + t(44, 82, "Settings", 26, "bold")
    groups = [("Reading", [("font", "Fonts and layout", "Literata 16 · justified"), ("layout", "Page turns and gestures", "Tap zones, swipes")]),
              ("Vaults", [("vault", "Vaults and sync", "My Vault · LiveSync on"), ("note", "Notes display", "Callouts, embeds, links")]),
              ("Device", [("light", "Screen and light", "Refresh every 10 pages"), ("sleep", "Sleep and battery", "Sleep after 10 min"),
                          ("wifi", "Wi-Fi and accounts", "Home Wi-Fi"), ("gear", "System", "Language, updates, about")])]
    y = 104
    for g, rows in groups:
        b += t(16, y + 22, g.upper(), 14, "bold", DG)
        y += 32
        b += rect(16, y, W - 32, 64 * len(rows), WH, BK, 2, 10)
        for i, (k, title, sub) in enumerate(rows):
            yy = y + i * 64
            if i:
                b += rect(64, yy, W - 96, 1.5, LG)
            b += icon(k, 30, yy + 16, BK, 30) + t(76, yy + 28, title, 19, "bold") + t(76, yy + 50, sub, 14, fill=DG)
            b += t(W - 32, yy + 40, "›", 26, "bold", DG, "end")
        y += 64 * len(rows) + 12
    svg(b, "F-settings")


for f in (dir_a, dir_b, dir_c, quick, reader_menu, settings):
    f()


# --- v2: list home with review fixes ---------------------------------------
def status2():
    out = rect(0, 0, W, 44, WH) + rect(0, 43, W, 1.5, LG)
    out += t(16, 29, "14:05", 18, "bold")
    # tappable status cluster: opens quick settings (also a long press of Back)
    out += rect(W - 196, 4, 180, 36, WH, BK, 2, 18)
    out += icon("light", W - 186, 10, BK, 24) + icon("wifi", W - 154, 10, BK, 24) + icon("sync", W - 122, 10, BK, 24)
    out += t(W - 30, 29, "86%", 16, "bold", anchor="end")
    return out


def stepper(x, y, w, label, value):
    out = t(x, y + 30, label, 18, "bold")
    bx = x + w - 196
    out += rect(bx, y, 56, 44, WH, BK, 2, 10) + t(bx + 28, y + 31, "−", 26, "bold", anchor="middle")
    out += t(bx + 98, y + 30, value, 18, "bold", anchor="middle")
    out += rect(bx + 140, y, 56, 44, WH, BK, 2, 10) + t(bx + 168, y + 31, "+", 26, "bold", anchor="middle")
    return out


def dir_b2():
    b = status2()
    y = 56
    b += rect(16, y, W - 32, 160, WH, BK, 2, 10)
    b += cover(30, y + 14, 88, 132, "Dune")
    b += t(134, y + 32, "CONTINUE", 14, "bold", DG) + t(134, y + 62, "Dune", 26, "bold") + t(134, y + 86, "Frank Herbert", 17, fill=DG)
    b += rect(134, y + 104, 290, 10, WH, BK, 2, 5) + rect(134, y + 104, 125, 10, BK, None, 0, 5)
    b += t(134, y + 138, "43%  ·  45 min today  ·  7-day streak", 15, fill=DG)
    rows = [("books", "Books", "128 books · 3 in progress", True), ("vault", "Vaults", "My Vault, Work · synced 5 min ago", True),
            ("search", "Search", "Books, notes and tags", False), ("folder", "Files", "SD card", False),
            ("transfer", "Transfer", "Wi-Fi, USB, Calibre", False), ("plugin", "Plugins", "3 installed", False),
            ("gear", "Settings", "Display, reading, controls", False)]
    y = 228
    for i, (k, title, sub, big) in enumerate(rows):
        h = 80 if big else 64
        focused = i == 1
        b += rect(16, y, W - 32, h, WH, BK if big or focused else LG, 2, 10)
        if focused:  # button focus: thick bar + outline, a small partial refresh
            b += rect(16, y, W - 32, h, "none", BK, 4, 10) + rect(16, y + 10, 8, h - 20, BK, None, 0, 3)
        isz = 40 if big else 30
        b += icon(k, 36, y + (h - isz) / 2, BK, isz)
        b += t(92, y + (36 if big else 30), title, 24 if big else 20, "bold") + t(92, y + (62 if big else 51), sub, 15, fill=DG)
        b += t(W - 34, y + h / 2 + 8, "›", 28, "bold", DG, "end")
        y += h + 6
    b += t(W / 2, 790, "▲▼ move  ·  OK open  ·  hold Back: quick settings", 13, fill=DG, anchor="middle")
    svg(b, "B2-home-list")


def quick2():
    b = status2() + rect(0, 44, W, 756, LG)
    for j in range(16):
        b += rect(24, 560 + j * 14, W - 48, 5, WH)
    b += rect(0, 44, W, 500, WH) + rect(0, 542, W, 3, BK)
    b += t(16, 84, "Quick settings", 22, "bold")
    tiles = [("light", "Light", True), ("wifi", "Wi-Fi", False), ("sync", "Sync now", False), ("rotate", "Rotate", False),
             ("sleep", "Sleep", False), ("transfer", "Transfer", False)]
    for i, (k, l, on) in enumerate(tiles):
        x, y = 16 + (i % 3) * 152, 100 + (i // 3) * 106
        b += rect(x, y, 144, 96, WH, BK, 4 if on else 2, 12)
        if on:
            b += rect(x + 104, y + 14, 26, 14, BK, None, 0, 7) + t(x + 117, y + 25, "ON", 10, "bold", WH, "middle")
        b += icon(k, x + 16, y + 16) + t(x + 16, y + 80, l, 18, "bold")
    b += stepper(16, 324, W - 32, "Brightness", "6 / 10")
    b += stepper(16, 382, W - 32, "Warmth", "3 / 10")
    b += t(16, 468, "Tap outside, swipe up or press Back to close", 14, fill=DG)
    b += rect(W / 2 - 30, 520, 60, 6, DG, None, 0, 3)
    svg(b, "D2-quick-settings")


def reader_menu2():
    b = rect(0, 0, W, H, WH)
    for j in range(13):
        b += rect(24, 40 + j * 30, W - 48 - (j % 4) * 20, 8, LG)
    b += rect(0, 420, W, 380, WH) + rect(0, 420, W, 3, BK) + rect(W / 2 - 30, 432, 60, 6, DG, None, 0, 3)
    b += t(16, 474, "Dune", 20, "bold") + t(W - 16, 474, "p. 212 / 498 · chapter 9", 15, fill=DG, anchor="end")
    b += rect(16, 486, W - 32, 8, WH, BK, 2, 4) + rect(16, 486, 190, 8, BK, None, 0, 4)
    for i, (k, l) in enumerate([("toc", "Contents"), ("bookmark", "Bookmark"), ("search", "Search"), ("note", "Highlights")]):
        x = 16 + i * 114
        b += rect(x, 510, 106, 92, WH, BK, 4 if i == 0 else 2, 10) + icon(k, x + 37, 524) + t(x + 53, 586, l, 15, "bold", anchor="middle")
    b += stepper(16, 620, W - 32, "Text size", "16 pt")
    b += rect(16, 680, W - 32, 1.5, LG)
    for i, (l, v) in enumerate([("Font", "Literata"), ("Spacing", "Normal"), ("Margins", "20")]):
        x = 16 + i * 152
        b += rect(x, 692, 144, 60, WH, BK, 2, 10) + t(x + 14, 717, l, 14, "bold", DG) + t(x + 14, 740, v, 17, "bold")
    b += t(W / 2, 784, "OK opens this menu · Back closes", 14, fill=DG, anchor="middle")
    svg(b, "E2-reading-menu")


for f in (dir_b2, quick2, reader_menu2):
    f()
