#!/usr/bin/env python3
import sys
from html import escape
from pathlib import Path

W, H = 1200, 752
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
MONO = "ui-monospace, SFMono-Regular, 'SF Mono', Menlo, Consolas, 'Liberation Mono', monospace"

THEMES = {
    "light": dict(
        text="#1f2328", muted="#59636e", box="#ffffff", box_line="#d1d9e0",
        guest_fill="#eef4fd", guest_line="#a8c3ec", guest_accent="#0969da",
        host_fill="#eef8f1", host_line="#a3d4b0", host_accent="#1a7f37",
        wire="#8250df", wire_fill="#f6f1ff", arrow="#59636e", gpu_fill="#1f2328", gpu_text="#ffffff",
        chip="#ddeaff", chip_text="#0a3069", band_fill="#fff8e6", band_line="#e8c66a", band_accent="#9a6700",
    ),
    "dark": dict(
        text="#e6edf3", muted="#9198a1", box="#0d1117", box_line="#3d444d",
        guest_fill="#0f1d33", guest_line="#264b80", guest_accent="#4493f8",
        host_fill="#0e2418", host_line="#24583a", host_accent="#3fb950",
        wire="#ab7df8", wire_fill="#1d1430", arrow="#9198a1", gpu_fill="#e6edf3", gpu_text="#0d1117",
        chip="#132f57", chip_text="#cfe2ff", band_fill="#2a2210", band_line="#7a5d17", band_accent="#e3b341",
    ),
}


def svg(theme):
    c = THEMES[theme]
    out = []
    add = out.append

    def text(x, y, s, size=14, weight=400, fill=None, anchor="start", mono=False, italic=False):
        add(f'<text x="{x}" y="{y}" font-family="{MONO if mono else FONT}" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill or c["text"]}" text-anchor="{anchor}"'
            f'{" font-style=\"italic\"" if italic else ""}>{escape(s)}</text>')

    def rect(x, y, w, h, fill, line, r=10, width=1.2, dash=None):
        add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{line}" '
            f'stroke-width="{width}"{f" stroke-dasharray=\"{dash}\"" if dash else ""}/>')

    def box(x, y, w, h, title, sub=None, mono_title=True, accent=None, sub2=None):
        rect(x, y, w, h, c["box"], c["box_line"], r=8)
        if accent:
            add(f'<rect x="{x}" y="{y + 8}" width="3" height="{h - 16}" rx="1.5" fill="{accent}"/>')
        lines = 1 + (1 if sub else 0) + (1 if sub2 else 0)
        top = y + h / 2 - (lines - 1) * 9 + 5
        text(x + w / 2, top, title, size=14, weight=600, anchor="middle", mono=mono_title)
        if sub:
            text(x + w / 2, top + 18, sub, size=12, fill=c["muted"], anchor="middle")
        if sub2:
            text(x + w / 2, top + 34, sub2, size=12, fill=c["muted"], anchor="middle")

    def arrow(d, color=None, width=1.6, dash=None, both=False):
        color = color or c["arrow"]
        marker = "a-" + color.strip("#")
        add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" '
            f'{f"stroke-dasharray=\"{dash}\" " if dash else ""}'
            f'marker-end="url(#{marker})"{f" marker-start=\"url(#{marker}s)\"" if both else ""}/>')

    colors = {c["arrow"], c["wire"], c["guest_accent"], c["host_accent"], c["band_accent"]}
    add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
        f'role="img" aria-label="accel-virt: Windows programs in the guest run on the Linux host GPU">')
    add("<defs>")
    for col in colors:
        m = "a-" + col.strip("#")
        add(f'<marker id="{m}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{col}"/></marker>')
        add(f'<marker id="{m}s" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto"><path d="M10,0 L0,5 L10,10 z" fill="{col}"/></marker>')
    add("</defs>")

    gx, gw = 16, 470
    hx, hw = 714, 470
    top, bottom = 16, 600

    rect(gx, top, gw, bottom - top, c["guest_fill"], c["guest_line"], r=14)
    rect(hx, top, hw, bottom - top, c["host_fill"], c["host_line"], r=14)
    text(gx + 20, top + 32, "Windows 11 guest", size=18, weight=700, fill=c["guest_accent"])
    text(gx + gw - 20, top + 32, "programs run unmodified", size=12, fill=c["muted"], anchor="end", italic=True)
    text(hx + 20, top + 32, "Linux host", size=18, weight=700, fill=c["host_accent"])
    text(hx + hw - 20, top + 32, "QEMU / KVM", size=12, fill=c["muted"], anchor="end", italic=True)

    ax, ay, aw, ah = gx + 20, 66, gw - 40, 64
    rect(ax, ay, aw, ah, c["box"], c["box_line"], r=8)
    text(ax + 14, ay + 24, "Applications and games", size=14, weight=600)
    chips = ["Direct3D 9 · 11 · 12", "Vulkan", "OpenGL", "OpenCL"]
    cx = ax + 14
    for chip in chips:
        cw = 14 + len(chip) * 7.1
        rect(cx, ay + 34, cw, 22, c["chip"], c["chip"], r=11)
        text(cx + cw / 2, ay + 50, chip, size=12, weight=600, fill=c["chip_text"], anchor="middle")
        cx += cw + 8

    dy, dh = 168, 62
    dw = (gw - 40 - 3 * 10) / 4
    drivers = [("avumd.dll", "Direct3D, DXGI"), ("avvk.dll", "Vulkan ICD"),
               ("avgl.dll", "OpenGL ICD"), ("avcl.dll", "OpenCL ICD")]
    for i, (name, sub) in enumerate(drivers):
        x = ax + i * (dw + 10)
        box(x, dy, dw, dh, name, sub, accent=c["guest_accent"])
        arrow(f"M{x + dw / 2},{ay + ah} L{x + dw / 2},{dy - 2}")
    text(ax + aw, dy + dh + 18, "user-mode drivers, for x64 and x86 processes", size=12, fill=c["muted"],
         anchor="end")

    ky = 330
    box(ax, ky, 120, 58, "DWM, GDI", "the desktop", mono_title=False)
    box(ax + 150, ky, 100, 58, "dxgkrnl", "Windows", mono_title=True)
    box(ax + 280, ky, aw - 280, 58, "avgpu.sys", "WDDM 3.2 miniport", accent=c["guest_accent"])
    arrow(f"M{ax + 120},{ky + 29} L{ax + 148},{ky + 29}")
    arrow(f"M{ax + 250},{ky + 29} L{ax + 278},{ky + 29}")
    text(ax + 280 + (aw - 280) / 2, ky + 80, "modes · memory · scheduling", size=12, fill=c["muted"], anchor="middle")
    text(ax + 280 + (aw - 280) / 2, ky + 96, "scanout · cursor · fences", size=12, fill=c["muted"], anchor="middle")
    arrow(f"M{ax + 40},{ky - 2} L{ax + 40},{dy + dh + 2}", dash="4 4", width=1.4)
    text(ax + 48, ky - 30, "composes with", size=11, fill=c["muted"], italic=True)
    text(ax + 48, ky - 16, "Direct3D 11", size=11, fill=c["muted"], italic=True)

    ny = 470
    rect(ax, ny, aw, 106, c["box"], c["box_line"], r=8, dash="5 4")
    text(ax + 14, ny + 26, "One test-signed driver package", size=14, weight=600)
    text(ax + 14, ny + 48, "avgpu.sys, and avumd, avvk, avgl and avcl for x64 and x86.", size=12,
         fill=c["muted"])
    text(ax + 14, ny + 66, "The adapter is named after the host GPU: Device Manager,", size=12, fill=c["muted"])
    text(ax + 14, ny + 84, "dxdiag and every program see its name, IDs and VRAM.", size=12, fill=c["muted"])

    rx = hx + 20
    rw = hw - 40
    devw = 170
    rect(rx, 160, devw, 228, c["box"], c["box_line"], r=8)
    add(f'<rect x="{rx}" y="168" width="3" height="212" rx="1.5" fill="{c["host_accent"]}"/>')
    text(rx + devw / 2, 248, "virtio-gpu-av-pci", size=14, weight=600, anchor="middle", mono=True)
    for i, line in enumerate(["QEMU device", "one per host GPU", "a worker per context"]):
        text(rx + devw / 2, 272 + i * 18, line, size=12, fill=c["muted"], anchor="middle")
    ex = rx + devw + 20
    ew = rw - devw - 20
    box(ex, 160, ew, 100, "libav_executor.so", "judges every command,", sub2="then runs it on the host",
        accent=c["host_accent"])
    rect(ex, 288, ew, 100, c["box"], c["box_line"], r=8, dash="5 4")
    text(ex + ew / 2, 318, "av-renderer", size=14, weight=600, anchor="middle", mono=True)
    text(ex + ew / 2, 338, "optional: the executor in its own", size=12, fill=c["muted"], anchor="middle")
    text(ex + ew / 2, 354, "process, so a host driver crash", size=12, fill=c["muted"], anchor="middle")
    text(ex + ew / 2, 370, "resets the device, not the VM", size=12, fill=c["muted"], anchor="middle")
    arrow(f"M{rx + devw},{210} L{ex - 2},{210}")
    arrow(f"M{ex + ew / 2},262 L{ex + ew / 2},286", dash="3 3", width=1.2)

    by, bh = 420, 66
    bw = (rw - 3 * 10) / 4
    backends = [("DXVK", "vkd3d-proton"), ("Vulkan", "host driver"), ("EGL", "+ OpenGL"), ("OpenCL", "e.g. rusticl")]
    for i, (name, sub) in enumerate(backends):
        x = rx + i * (bw + 10)
        box(x, by, bw, bh, name, sub, mono_title=False)
    for i in range(4):
        x = rx + i * (bw + 10) + bw / 2
        add(f'<path d="M{x},398 L{x},{by - 2}" fill="none" stroke="{c["arrow"]}" stroke-width="1.6" '
            f'marker-end="url(#a-{c["arrow"].strip("#")})"/>')
    add(f'<path d="M{rx},398 L{rx + rw},398" fill="none" stroke="{c["arrow"]}" stroke-width="1.6"/>')
    add(f'<path d="M{ex + ew / 2},388 L{ex + ew / 2},398" fill="none" stroke="{c["arrow"]}" stroke-width="1.6"/>')
    arrow(f"M{rx + bw},{by + bh / 2} L{rx + bw + 8},{by + bh / 2}")
    text(rx + bw / 2, by + bh + 16, "Direct3D", size=11, fill=c["muted"], anchor="middle", italic=True)

    gy = 520
    rect(rx, gy, rw, 56, c["gpu_fill"], c["gpu_fill"], r=8)
    text(rx + rw / 2, gy + 24, "Host GPU", size=15, weight=700, fill=c["gpu_text"], anchor="middle")
    text(rx + rw / 2, gy + 43, "AMD · Intel · NVIDIA, through its own Linux driver", size=12,
         fill=c["gpu_text"], anchor="middle")
    for i in range(1, 4):
        x = rx + i * (bw + 10) + bw / 2
        arrow(f"M{x},{by + bh} L{x},{gy - 2}")

    mx0, mx1 = gx + gw, hx
    mid = (mx0 + mx1) / 2
    rect(mid - 96, 160, 192, 92, c["wire_fill"], c["wire"], r=10, width=1.2)
    text(mid, 182, "av context", size=14, weight=700, fill=c["wire"], anchor="middle")
    text(mid, 201, "ring in guest RAM", size=12, anchor="middle")
    text(mid, 218, "doorbell · ioeventfd", size=12, anchor="middle")
    text(mid, 235, "fence page · MSI-X", size=12, anchor="middle")
    arrow(f"M{ax + aw + 2},{dy + dh / 2} L{mid - 98},{dy + dh / 2}", color=c["wire"], width=2)
    arrow(f"M{mid + 96},{dy + dh / 2} L{rx - 2},{dy + dh / 2}", color=c["wire"], width=2)
    text(mid, 146, "user-mode submission", size=12, weight=600, fill=c["wire"], anchor="middle")
    text(mid, 270, "bypasses the kernel", size=11, fill=c["muted"], anchor="middle", italic=True)

    rect(mid - 96, 318, 192, 70, c["box"], c["box_line"], r=10)
    text(mid, 342, "virtio-gpu", size=14, weight=700, anchor="middle")
    text(mid, 360, "control queue", size=12, anchor="middle")
    text(mid, 376, "contexts · modes · scanout", size=12, fill=c["muted"], anchor="middle")
    arrow(f"M{ax + aw + 2},{ky + 29} L{mid - 98},{ky + 29}", width=1.8)
    arrow(f"M{mid + 96},{ky + 29} L{rx - 2},{ky + 29}", width=1.8)

    band_y = 624
    rect(gx, band_y, W - 2 * gx, 112, c["band_fill"], c["band_line"], r=14)
    text(gx + 20, band_y + 30, "Shared memory: the host window", size=16, weight=700, fill=c["band_accent"])
    text(gx + 20, band_y + 54,
         "Every byte the guest CPU and the host GPU share is host GPU memory, allocated on demand and exported as a dma-buf.",
         size=13)
    text(gx + 20, band_y + 74,
         "QEMU maps it into the hostmem= BAR, which the guest maps with the host's own caching: no copies in between.",
         size=13)
    text(gx + 20, band_y + 94, "Guest RAM is never handed to a host GPU, and the frames the desktop composes are scanned out zero-copy.",
         size=13, fill=c["muted"])

    add("</svg>")
    return "\n".join(out)


if __name__ == "__main__":
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "docs" / "images"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in THEMES:
        (out_dir / f"architecture-{name}.svg").write_text(svg(name) + "\n")
