"""Embed a small CC0 Kenney input-prompt subset into the single-file web UI."""
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "third_party/kenney-input-prompts/kenney_input-prompts_1.5.zip"
ASSETS = ROOT / "assets/kenney-input-prompts"
PAGE = ROOT / "web/index.html"

XBOX = {
    "cross": "xbox_button_a", "circle": "xbox_button_b",
    "square": "xbox_button_x", "triangle": "xbox_button_y",
    "l1": "xbox_lb", "r1": "xbox_rb", "l2": "xbox_lt", "r2": "xbox_rt",
    "l3": "xbox_stick_l_press", "r3": "xbox_stick_r_press",
    "up": "xbox_dpad_up", "down": "xbox_dpad_down",
    "left": "xbox_dpad_left", "right": "xbox_dpad_right",
    "options": "xbox_button_menu", "create": "xbox_button_view",
    "touchpad": "xbox_button_share",
}
PS = {
    "cross": "playstation_button_cross", "circle": "playstation_button_circle",
    "square": "playstation_button_square", "triangle": "playstation_button_triangle",
    "l1": "playstation_trigger_l1", "r1": "playstation_trigger_r1",
    "l2": "playstation_trigger_l2", "r2": "playstation_trigger_r2",
    "l3": "playstation_stick_l_press", "r3": "playstation_stick_r_press",
    "up": "playstation_dpad_up", "down": "playstation_dpad_down",
    "left": "playstation_dpad_left", "right": "playstation_dpad_right",
    "options": "playstation5_button_options", "create": "playstation5_button_create",
    "touchpad": "playstation5_touchpad",
}


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    symbols = []
    with zipfile.ZipFile(ARCHIVE) as archive:
        (ASSETS / "LICENSE.txt").write_bytes(archive.read("License.txt"))
        for prefix, folder, mapping in (("xb", "Xbox Series", XBOX),
                                        ("ps", "PlayStation Series", PS)):
            for action, stem in mapping.items():
                source = f"{folder}/Vector/{stem}.svg"
                data = archive.read(source).decode("utf-8")
                (ASSETS / f"{prefix}-{action}.svg").write_text(data, encoding="utf-8")
                match = re.search(r"<svg\b[^>]*>(.*?)</svg>", data, re.S)
                if not match:
                    raise ValueError(source)
                inner = re.sub(r"<defs\s*/>", "", match.group(1)).strip()
                symbols.append(f'<symbol id="ki-{prefix}-{action}" viewBox="0 0 64 64">{inner}</symbol>')

    sprite = ('<!-- KENNEY_ICONS_START -->\n'
              '<svg class="icon-sprite" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">\n'
              + "\n".join(symbols) + '\n</svg>\n'
              '<!-- KENNEY_ICONS_END -->')
    page = PAGE.read_text(encoding="utf-8")
    pattern = r"<!-- KENNEY_ICONS_START -->.*?<!-- KENNEY_ICONS_END -->"
    if not re.search(pattern, page, re.S):
        raise ValueError("icon markers missing from web/index.html")
    PAGE.write_text(re.sub(pattern, lambda _: sprite, page, flags=re.S), encoding="utf-8")
    print(f"Embedded {len(symbols)} CC0 icons from Kenney Input Prompts 1.5A")


if __name__ == "__main__":
    main()
