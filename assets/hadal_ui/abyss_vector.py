import sys
PAL = {
 "olive": dict(ink="#15180A", shadow="#55621F", mid="#93A23B", light="#D2DD72", pale="#F2F5C8", glow="#FFB12E", core="#FFF1B8"),
 "abyss": dict(ink="#0B0D12", shadow="#1F2A3A", mid="#35506B", light="#6E95B0", pale="#E8F1F6", glow="#FF8200", core="#FFE2B8"),
}
def svg(p, size=400, bg=None, flat=False):
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 400 400">']
    if bg: s.append(f'<rect width="400" height="400" fill="{bg}"/>')
    s.append(body(p, flat))
    s.append("</svg>")
    return "\n".join(s)


# The outer shapes, for an edge round the whole fish on a dark background
OUTLINE = [
    "M108 205 L42 160 C52 190 52 222 44 238 C54 252 56 278 46 300 L108 258 Z",
    "M128 150 L118 112 L146 132 L156 102 L176 134 Z",
    "M104 182 C112 128 180 98 248 112 C292 122 322 152 332 192 L254 232 L350 252 C346 300 300 330 226 334 C160 338 112 312 100 262 C94 236 98 206 104 182 Z",
    "M176 262 C150 262 140 292 158 300 C176 300 192 286 190 266 Z",
]


def body(p, flat=False, ids="", edge=None):
    """Abyss's shapes in his 400 x 400 box, for use inside another SVG: ids keeps his gradient and
    clip ids apart from the page's, edge (a colour) puts a band of it round him, for dark pages"""
    c = PAL[p]
    ink = c["ink"]
    s = []
    if edge:
        e = f'stroke="{edge}" stroke-width="21" stroke-linejoin="round" stroke-linecap="round" fill="{edge}"'
        for d in OUTLINE:
            s.append(f'<path {e} d="{d}"/>')
        s.append(f'<path stroke="{edge}" stroke-width="19" fill="none" stroke-linecap="round" d="M214 118 C204 52 292 18 330 82 C334 88 336 92 336 96"/>')
        s.append(f'<circle cx="338" cy="114" r="29" fill="{edge}"/>')
        for d in ("M372 92 L384 84", "M378 118 L392 118", "M370 142 L381 151", "M304 92 L294 84"):
            s.append(f'<path stroke="{edge}" stroke-width="15" stroke-linecap="round" d="{d}"/>')
    sw = 9
    g = f'stroke="{ink}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"'
    # glow behind the lure
    if not flat:
        s.append(f'<defs><radialGradient id="gl{ids}"><stop offset="0" stop-color="{c["glow"]}" stop-opacity=".55"/><stop offset="1" stop-color="{c["glow"]}" stop-opacity="0"/></radialGradient></defs>')
        s.append(f'<circle cx="338" cy="112" r="70" fill="url(#gl{ids})"/>')
    # tail
    s.append(f'<path {g} fill="{c["mid"]}" d="M108 205 L42 160 C52 190 52 222 44 238 C54 252 56 278 46 300 L108 258 Z"/>')
    s.append(f'<path stroke="{ink}" stroke-width="5" stroke-linecap="round" fill="none" d="M100 220 L62 196 M100 244 L62 268"/>')
    # dorsal fin
    s.append(f'<path {g} fill="{c["shadow"]}" d="M128 150 L118 112 L146 132 L156 102 L176 134 Z"/>')
    # body
    body = "M104 182 C112 128 180 98 248 112 C292 122 322 152 332 192 L254 232 L350 252 C346 300 300 330 226 334 C160 338 112 312 100 262 C94 236 98 206 104 182 Z"
    s.append(f'<path {g} fill="{c["mid"]}" d="{body}"/>')
    # cel shadow: the lower back, away from the light
    s.append(f'<clipPath id="bc{ids}"><path d="{body}"/></clipPath>')
    s.append(f'<path clip-path="url(#bc{ids})" fill="{c["shadow"]}" d="M60 250 C110 300 200 318 300 300 L360 360 L60 380 Z"/>')
    s.append(f'<path clip-path="url(#bc{ids})" fill="{c["shadow"]}" d="M60 150 C90 200 96 250 130 300 L40 320 Z"/>')
    # lit by the lure: top right of the head
    s.append(f'<path clip-path="url(#bc{ids})" fill="{c["light"]}" d="M206 118 C252 108 300 128 322 168 C300 152 262 140 222 142 C210 134 204 126 206 118 Z"/>')
    # spots
    for x, y, r in ((150, 170, 7), (170, 196, 5), (136, 214, 5), (190, 160, 4)):
        s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{c["shadow"]}"/>')
    # body outline again on top of the shading
    s.append(f'<path {g} fill="none" d="{body}"/>')
    # mouth
    mouth = "M332 192 L254 232 L350 252 Z"
    s.append(f'<path {g} fill="{ink}" d="{mouth}"/>')
    s.append(f'<path fill="{c["shadow"]}" d="M266 232 L320 214 L330 244 Z" opacity=".9"/>')
    # teeth: upper hang, lower rise; the front lower fang crosses the lip
    teeth = [
        "M320 196 L314 222 L308 200 Z", "M298 207 L292 230 L286 211 Z", "M276 218 L272 234 L266 222 Z",
        "M282 240 L288 222 L292 242 Z", "M306 245 L314 220 L318 248 Z", "M334 250 L346 206 L348 252 Z",
    ]
    for t in teeth:
        s.append(f'<path stroke="{ink}" stroke-width="4" stroke-linejoin="round" fill="{c["pale"]}" d="{t}"/>')
    # pectoral fin
    s.append(f'<path {g} fill="{c["light"]}" d="M176 262 C150 262 140 292 158 300 C176 300 192 286 190 266 Z"/>')
    s.append(f'<path stroke="{ink}" stroke-width="4" fill="none" stroke-linecap="round" d="M178 270 L162 290 M184 274 L172 296"/>')
    # eye, looking up at its light
    s.append(f'<circle {g} cx="258" cy="162" r="31" fill="{c["pale"]}"/>')
    s.append(f'<circle cx="268" cy="154" r="18" fill="{ink}"/>')
    s.append(f'<circle cx="274" cy="146" r="7" fill="{c["pale"]}"/>')
    s.append(f'<circle cx="262" cy="164" r="3.5" fill="{c["pale"]}"/>')
    s.append(f'<path stroke="{ink}" stroke-width="7" fill="none" stroke-linecap="round" d="M224 132 C238 118 262 116 280 124"/>')
    # lure
    s.append(f'<path stroke="{ink}" stroke-width="7" fill="none" stroke-linecap="round" d="M214 118 C204 52 292 18 330 82 C334 88 336 92 336 96"/>')
    s.append(f'<circle {g} cx="338" cy="114" r="19" fill="{c["glow"]}"/>')
    s.append(f'<circle cx="332" cy="108" r="7" fill="{c["core"]}"/>')
    # glow ticks, as the reference draws light
    for d in ("M372 92 L384 84", "M378 118 L392 118", "M370 142 L381 151", "M304 92 L294 84"):
        s.append(f'<path stroke="{ink}" stroke-width="5" stroke-linecap="round" d="{d}"/>')
    return "\n".join(s)
if __name__ == "__main__":
    for p in PAL:
        open(f"abyss_{p}.svg", "w").write(svg(p, 400, bg="#ECEFE0" if p=="olive" else "#E9EEF2"))
        open(f"abyss_{p}_flat.svg", "w").write(svg(p, 400, flat=True))
