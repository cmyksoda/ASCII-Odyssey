#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Zip the Homebrew Channel folder for an SD card: apps/APP/ with boot.dol, meta.xml and icon.png.

  package.py APP VERSION
"""
import os
import sys
import zipfile


def main():
    app, version = sys.argv[1], sys.argv[2]
    root = os.path.join('dist', 'apps', app)
    out = os.path.join('dist', f'{app}-{version}.zip')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for name in ('boot.dol', 'meta.xml', 'icon.png'):
            z.write(os.path.join(root, name), f'apps/{app}/{name}')
    print(out)


if __name__ == '__main__':
    main()
