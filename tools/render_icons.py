"""Renders the app icon's PNG sizes from its SVG drawings.

Usage: render_icons.py   (from the repository root; needs Playwright and Chromium)

data/icons/hicolor/scalable/apps/freya-combat-tracker.svg is the full drawing
(a large d20 with swept wings behind it), used at 48 pixels and up.
data/icons/freya-combat-tracker-small.svg is used at 16-32 pixels; it is the
same drawing for now, kept separate so the small sizes can be simplified.
"""

import os
import sys

from playwright.sync_api import sync_playwright

SIZES = [16, 22, 24, 32, 48, 64, 128, 256]
ROOT = os.path.join("data", "icons")
FULL = os.path.join(ROOT, "hicolor", "scalable", "apps", "freya-combat-tracker.svg")
SMALL = os.path.join(ROOT, "freya-combat-tracker-small.svg")


def main():
    executable = os.environ.get("CHROMIUM")
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=executable) if executable else playwright.chromium.launch()
        for size in SIZES:
            with open(SMALL if size <= 32 else FULL, encoding="utf-8") as handle:
                svg = handle.read().replace("<svg ", '<svg width="%d" height="%d" ' % (size, size), 1)
            page = browser.new_page(viewport={"width": size, "height": size}, device_scale_factor=1)
            page.set_content('<html><body style="margin:0;background:transparent">' + svg + "</body></html>")
            out = os.path.join(ROOT, "hicolor", "%dx%d" % (size, size), "apps", "freya-combat-tracker.png")
            os.makedirs(os.path.dirname(out), exist_ok=True)
            page.screenshot(path=out, omit_background=True, clip={"x": 0, "y": 0, "width": size, "height": size})
            page.close()
            print(out)
        browser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
