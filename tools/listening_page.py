#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn rendered pieces (WAV + spectrogram PNG + info JSON) into a listening page on Jaxi's artifact shelf.

  listening_page.py RENDER_DIR ROUND

Writes ~/Documents/Artifacts/ascii-odyssey-sound-check.html and its media folder, then refreshes the shelf index.
Reactions and notes on the page sync back through the shelf to .shelf/state.json.
"""
import html
import json
import os
import subprocess
import sys

from PIL import Image

SHELF = os.path.expanduser('~/Documents/Artifacts')
PAGE = 'ascii-odyssey-sound-check'


def figlet(text, font):
    out = subprocess.run(['figlet', '-w', '200', '-f', font, text], capture_output=True, text=True).stdout
    lines = out.rstrip('\n').split('\n')
    while lines and not lines[-1].strip():
        lines.pop()
    return '\n'.join(line.rstrip() for line in lines)


def clock(seconds):
    return f'{int(seconds) // 60}:{int(seconds) % 60:02d}'


def main():
    render_dir, round_name = sys.argv[1], sys.argv[2]
    media = os.path.join(SHELF, PAGE)
    os.makedirs(media, exist_ok=True)
    pieces = []
    for name in sorted(os.listdir(render_dir)):
        if not name.endswith('.json'):
            continue
        slug = name[:-5]
        info = json.load(open(os.path.join(render_dir, name)))
        mp3 = os.path.join(media, slug + '.mp3')
        if not os.path.exists(mp3) or os.path.getmtime(mp3) < os.path.getmtime(os.path.join(render_dir, slug + '.wav')):
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', os.path.join(render_dir, slug + '.wav'),
                            '-c:a', 'libmp3lame', '-q:a', '3', mp3], check=True)
        Image.open(os.path.join(render_dir, slug + '.png')).convert('RGB').resize((1200, 300), Image.LANCZOS).save(
            os.path.join(media, slug + '.jpg'), quality=86)
        info['slug'] = slug
        info['art'] = figlet(info['title'], 'small')
        pieces.append(info)
    order = {'synthwave': 0, 'keygen': 1, 'ambient': 2}
    pieces.sort(key=lambda p: (order.get(p['mood'], 9), p['title']))
    template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'listening_page.html')).read()
    cards = []
    for p in pieces:
        total = p['length'] + 6
        segments = ''.join(
            f'<span class="seg {s["kind"]}" style="left:{s["start"] / total * 100:.3f}%;width:{(s["end"] - s["start"]) / total * 100:.3f}%" '
            f'title="{s["kind"]}"></span>' for s in p['sections'])
        cards.append(f'''
<article class="piece" id="{p['slug']}" data-slug="{p['slug']}">
  <pre class="fig" aria-label="{html.escape(p['title'])}">{html.escape(p['art'])}</pre>
  <p class="meta">{p['mood']} <span class="dot">·</span> {p['key']} <span class="dot">·</span> {p['bpm']} BPM <span class="dot">·</span> {clock(p['length'])}</p>
  <div class="track" role="presentation">
    <img src="{PAGE}/{p['slug']}.jpg" alt="Spectrogram of {html.escape(p['title'])}" width="1200" height="300" loading="lazy">
    <div class="sections">{segments}</div>
    <div class="playhead" hidden></div>
  </div>
  <audio controls preload="none" src="{PAGE}/{p['slug']}.mp3"></audio>
  <div class="react" role="group" aria-label="Your reaction to {html.escape(p['title'])}">
    <button type="button" data-reaction="love">Love it</button>
    <button type="button" data-reaction="okay">It's okay</button>
    <button type="button" data-reaction="no">Not for me</button>
  </div>
  <label class="note-label" for="note-{p['slug']}">Notes</label>
  <textarea id="note-{p['slug']}" rows="2" placeholder="A catchy bit, a harsh sound, a part that drags..."></textarea>
</article>''')
    page = template.replace('{{ROUND}}', html.escape(round_name)).replace('{{TITLE_ART}}', html.escape(figlet('ASCII Odyssey', 'slant')))
    page = page.replace('{{PIECES}}', '\n'.join(cards))
    with open(os.path.join(SHELF, PAGE + '.html'), 'w') as f:
        f.write(page)
    subprocess.run([os.path.join(SHELF, 'refresh-index.py')], check=False, cwd=SHELF)


if __name__ == '__main__':
    main()
