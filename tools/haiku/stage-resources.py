#!/usr/bin/env python3
"""Copies BambuStudio's resources directory into a package stage for air/OS,
leaving out what the Haiku build cannot use and costs space or memory:

  - hms/: the printer error database (185 MB of JSON). Only the device pages
    use it, and they need Bambu's network plug-in, which has no Haiku build.
    Shipping it also made the application copy all of it into the user's
    settings directory at the first start (DevHMSQuery's sync_bundled_hms);
    without it that copy does not happen. The pages download the database
    from Bambu's servers when they need it.
  - pictures far larger than any place they are shown (the 5000 x 5000
    accessory pictures of the firmware upgrade panel, 7-11 MB each and about
    100 MB of memory each when decoded): user interface pictures in images/
    are scaled down to 1024 pixels, the build plate textures of the printer
    profiles (drawn on the 3D bed) to 2048. Web pages are left alone.

    stage-resources.py SOURCE_RESOURCES DEST_RESOURCES
"""
import os
import shutil
import sys

from PIL import Image

EXCLUDE = {'hms'}
# directory: (pictures with a side above this..., ...get this longest side)
SCALE = {'images': (2048, 1024), 'profiles': (2048, 2048)}


def scale_down(path, above, side):
    try:
        image = Image.open(path)
    except (OSError, Image.UnidentifiedImageError):
        return False  # not a PNG after all (images/extra_icon.png is empty)
    with image:
        if max(image.size) <= above:
            return False
        factor = side / max(image.size)
        small = image.resize((round(image.width * factor), round(image.height * factor)), Image.LANCZOS)
    small.save(path, optimize=True)
    return True


def main():
    src, dest = sys.argv[1], sys.argv[2]
    if os.path.exists(dest):
        shutil.rmtree(dest)
    shutil.copytree(src, dest, symlinks=False,
                    ignore=lambda d, names: [n for n in names
                                             if os.path.samefile(d, src) and n in EXCLUDE])
    scaled = 0
    for top, (above, side) in SCALE.items():
        for root, _, files in os.walk(os.path.join(dest, top)):
            for name in files:
                if name.lower().endswith('.png') and scale_down(os.path.join(root, name), above, side):
                    scaled += 1
    print(f'stage-resources: left out {", ".join(sorted(EXCLUDE))}; scaled {scaled} pictures')


if __name__ == '__main__':
    main()
