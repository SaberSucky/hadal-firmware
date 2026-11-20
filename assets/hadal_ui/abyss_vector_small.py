from abyss_vec import PAL
def svg(p):
    c=PAL[p]; ink=c["ink"]; sw=14
    g=f'stroke="{ink}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"'
    body="M104 186 C112 128 180 100 248 112 C292 122 322 152 334 190 L262 228 L352 250 C348 300 300 332 226 336 C160 340 110 314 100 262 C94 236 98 210 104 186 Z"
    s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1600" viewBox="24 6 376 376">']
    s.append(f'<path {g} fill="{c["mid"]}" d="M110 205 L44 158 C54 190 54 222 46 238 C56 252 58 280 48 304 L110 262 Z"/>')
    s.append(f'<path {g} fill="{c["mid"]}" d="{body}"/>')
    s.append(f'<clipPath id="bc"><path d="{body}"/></clipPath>')
    s.append(f'<path clip-path="url(#bc)" fill="{c["shadow"]}" d="M60 262 C120 306 210 318 320 296 L370 370 L60 380 Z"/>')
    s.append(f'<path clip-path="url(#bc)" fill="{c["light"]}" d="M200 120 C252 106 304 130 326 172 C300 154 262 144 222 146 C206 138 198 128 200 120 Z"/>')
    s.append(f'<path {g} fill="none" d="{body}"/>')
    s.append(f'<path {g} fill="{ink}" d="M334 190 L262 228 L352 250 Z"/>')
    for t in ("M318 196 L308 228 L298 204 Z","M290 211 L284 232 L276 216 Z","M300 244 L310 222 L318 248 Z","M332 250 L346 204 L352 252 Z"):
        s.append(f'<path stroke="{ink}" stroke-width="6" stroke-linejoin="round" fill="{c["pale"]}" d="{t}"/>')
    s.append(f'<circle {g} cx="250" cy="160" r="40" fill="{c["pale"]}"/>')
    s.append(f'<circle cx="262" cy="150" r="22" fill="{ink}"/>')
    s.append(f'<circle cx="268" cy="142" r="9" fill="{c["pale"]}"/>')
    s.append(f'<path stroke="{ink}" stroke-width="12" fill="none" stroke-linecap="round" d="M196 118 C188 50 292 14 334 80 C338 88 340 94 340 98"/>')
    s.append(f'<circle {g} cx="342" cy="118" r="24" fill="{c["glow"]}"/>')
    s.append(f'<circle cx="335" cy="110" r="9" fill="{c["core"]}"/>')
    s.append('</svg>')
    return "\n".join(s)
for p in PAL: open(f"small_{p}.svg","w").write(svg(p))
