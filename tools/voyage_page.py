#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn rendered voyages (MP4 + poster JPG + info JSON from `piece --info`) into a video page on Jaxi's artifact shelf.

  voyage_page.py RENDER_DIR ROUND ZIP

Writes ~/Documents/Artifacts/ascii-odyssey-voyages.html and its media folder, then refreshes the shelf index.
Reactions and notes on the page sync back through the shelf to .shelf/state.json.
"""
import html
import json
import os
import shutil
import subprocess
import sys

SHELF = os.path.expanduser('~/Documents/Artifacts')
PAGE = 'ascii-odyssey-voyages'
MOOD_ORDER = ['synthwave', 'keygen', 'ambient', 'lo-fi', 'trance', 'jungle', 'music box', 'dub']


def figlet(text, font):
    out = subprocess.run(['figlet', '-w', '200', '-f', font, text], capture_output=True, text=True).stdout
    lines = out.rstrip('\n').split('\n')
    while lines and not lines[-1].strip():
        lines.pop()
    return '\n'.join(line.rstrip() for line in lines)


def clock(seconds):
    return f'{int(seconds) // 60}:{int(seconds) % 60:02d}'


def duration(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path],
                         capture_output=True, text=True).stdout
    return float(out.strip())


def card(p):
    total = p['duration']
    segments = ''.join(
        f'<span class="seg {s["kind"]}" style="left:{s["start"] / total * 100:.3f}%;width:{(s["end"] - s["start"]) / total * 100:.3f}%;'
        f'height:{18 + 82 * s["react"]:.1f}%" title="{s["kind"]}"></span>' for s in p['sections'])
    title = html.escape(p['title'])
    return f'''
<article class="piece" id="{p['slug']}" data-slug="{p['slug']}">
  <pre class="fig" aria-label="{title}">{html.escape(p['art'])}</pre>
  <p class="meta">{p['mood']} <span class="dot">·</span> {p['key']} <span class="dot">·</span> {p['bpm']} BPM <span class="dot">·</span> {clock(p['length'])} <span class="dot">·</span> {html.escape(p['palette'])} <span class="dot">·</span> {html.escape(p['reactivity'])}</p>
  <video controls preload="none" playsinline poster="{PAGE}/{p['slug']}.jpg" src="{PAGE}/{p['slug']}.mp4"></video>
  <div class="arrangement" data-total="{total:.2f}" tabindex="0" role="slider" aria-label="Arrangement of {title}, taller where the picture reacts more">{segments}<div class="playhead" hidden></div></div>
  <div class="react" role="group" aria-label="Your reaction to {title}">
    <button type="button" data-reaction="love">Love it</button>
    <button type="button" data-reaction="okay">It's okay</button>
    <button type="button" data-reaction="no">Not for me</button>
  </div>
  <label class="note-label" for="note-{p['slug']}">Notes</label>
  <textarea id="note-{p['slug']}" rows="2" placeholder="A moment that clicked, a sound that grates, a picture that should move more..."></textarea>
</article>'''


def main():
    render_dir, round_name, package = sys.argv[1], sys.argv[2], sys.argv[3]
    media = os.path.join(SHELF, PAGE)
    os.makedirs(media, exist_ok=True)
    # The build the videos came from, for sailing the same seeds on the Wii.
    os.makedirs(os.path.join(SHELF, 'downloads'), exist_ok=True)
    shutil.copyfile(package, os.path.join(SHELF, 'downloads', os.path.basename(package)))
    pieces = []
    for name in sorted(os.listdir(render_dir)):
        if not name.endswith('.json'):
            continue
        slug = name[:-5]
        info = json.load(open(os.path.join(render_dir, name)))
        for ext in ('mp4', 'jpg'):
            shutil.copyfile(os.path.join(render_dir, f'{slug}.{ext}'), os.path.join(media, f'{slug}.{ext}'))
        info['slug'] = slug
        info['art'] = figlet(info['title'], 'small')
        info['duration'] = duration(os.path.join(render_dir, f'{slug}.mp4'))
        pieces.append(info)
    pieces.sort(key=lambda p: MOOD_ORDER.index(p['mood']) if p['mood'] in MOOD_ORDER else 99)
    template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'voyage_page.html')).read()
    page = template.replace('{{ROUND}}', html.escape(round_name)).replace('{{TITLE_ART}}', html.escape(figlet('ASCII Odyssey', 'slant')))
    page = page.replace('{{ZIP}}', html.escape(os.path.basename(package)))
    page = page.replace('{{PIECES}}', '\n'.join(card(p) for p in pieces))
    with open(os.path.join(SHELF, PAGE + '.html'), 'w') as f:
        f.write(page)
    subprocess.run([os.path.join(SHELF, 'refresh-index.py')], check=False, cwd=SHELF)


if __name__ == '__main__':
    main()
