#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn rendered voyages (MP4 + poster JPG + info JSON from `piece --info`) into a video page on Jaxi's artifact shelf.

  voyage_page.py RENDER_DIR ROUND PAGE TITLE [ZIP]

Writes ~/Documents/Artifacts/PAGE.html and its media folder PAGE/, then refreshes the shelf index. Reactions and notes
sync back through the shelf to .shelf/state.json under PAGE, so each round keeps its own. Without ZIP, the page says
the Wii build is still to come.
"""
import html
import json
import os
import re
import shutil
import subprocess
import sys

SHELF = os.path.expanduser('~/Documents/Artifacts')
MOOD_ORDER = ['synthwave', 'keygen', 'ambient', 'lo-fi', 'trance', 'dubstep', 'music box', 'dub']
FILLED = {'ROUND', 'TITLE_ART', 'TITLE', 'PAGE', 'INSTALL', 'PIECES', 'FACES'}


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


def section_at(p, t):
    for s in p['sections']:
        if s['start'] <= t < s['end']:
            return s['kind']
    return p['sections'][-1]['kind']


def card(p, page):
    total = p['duration']
    blocks = []
    for s in p['shots']:
        start, end = s['start'], min(s['end'], total)
        if start >= total:
            continue
        what = 'answers the music' if s['reactive'] else 'moves on its own'
        # A shot's entrance starts a little before its bar line, so its middle says which section it belongs to.
        blocks.append(f'<span class="seg {section_at(p, (start + end) / 2)}{"" if s["reactive"] else " still"}" '
                      f'style="left:{start / total * 100:.3f}%;width:{(end - start) / total * 100:.3f}%" '
                      f'title="{html.escape(s["scene"])}, {what}"></span>')
    title = html.escape(p['title'])
    palettes = ' / '.join(html.escape(name) for name in dict.fromkeys(p['palettes']))
    scenes = len(set(s['scene'] for s in p['shots']))
    return f'''
<article class="piece" id="{p['slug']}" data-slug="{p['slug']}">
  <pre class="fig" aria-label="{title}">{html.escape(p['art'])}</pre>
  <p class="meta">{p['mood']} <span class="dot">·</span> {p['key']} <span class="dot">·</span> {p['bpm']} BPM <span class="dot">·</span> {clock(p['length'])} <span class="dot">·</span> {palettes} <span class="dot">·</span> {scenes} scenes</p>
  <video controls preload="none" playsinline poster="{page}/{p['slug']}.jpg" src="{page}/{p['slug']}.mp4"></video>
  <div class="arrangement" data-total="{total:.2f}" tabindex="0" role="slider" aria-label="Shots in {title}, tall where the scene answers the music">{''.join(blocks)}<div class="playhead" hidden></div></div>
  <div class="react" role="group" aria-label="Your reaction to {title}">
    <button type="button" data-reaction="love">Love it</button>
    <button type="button" data-reaction="okay">It's okay</button>
    <button type="button" data-reaction="no">Not for me</button>
  </div>
  <label class="note-label" for="note-{p['slug']}">Notes</label>
  <textarea id="note-{p['slug']}" rows="2" placeholder="A moment that clicked, a sound that grates, a picture that should move more..."></textarea>
</article>'''


def faces(render_dir, media, page):
    # Title faces rendered on their own, each writing the same seed, so she can keep or drop them one by one.
    folder = os.path.join(render_dir, 'faces')
    if not os.path.isdir(folder):
        return ''
    os.makedirs(os.path.join(media, 'faces'), exist_ok=True)
    cards = []
    for name in sorted(n[:-4] for n in os.listdir(folder) if n.endswith('.jpg')):
        shutil.copyfile(os.path.join(folder, f'{name}.jpg'), os.path.join(media, 'faces', f'{name}.jpg'))
        label = html.escape(name)
        cards.append(f'''
    <figure class="face" data-face="{label}">
      <img src="{page}/faces/{label}.jpg" alt="A title written in {label}" loading="lazy" width="640" height="480">
      <figcaption>
        <span>{label}</span>
        <span class="react" role="group" aria-label="Keep {label}?">
          <button type="button" data-reaction="love">Love it</button>
          <button type="button" data-reaction="drop">Drop it</button>
        </span>
      </figcaption>
    </figure>''')
    return ''.join(cards)


INSTALL = ('Every seed here is real. To sail one yourself, unzip <a href="downloads/{zip}">{zip}</a> onto your SD card so it '
           'lands in <code>apps/asciiodyssey</code>, start it from the Homebrew Channel, and type the seed in Settings with '
           "Mood on any and Length on medium, which are the defaults. You'll get this exact voyage.")
WAITING = ("Every seed here is real. The Wii build that plays them comes after a timing check on your Wii itself, since the "
           "new scenes haven't run on the console yet. Once it's on your SD card, type a seed in Settings with Mood on any "
           "and Length on medium, which are the defaults, and you'll get that exact voyage.")


def main():
    render_dir, round_name, page_name, title = sys.argv[1:5]
    package = sys.argv[5] if len(sys.argv) > 5 else None
    template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'voyage_page.html')).read()
    # Checked before anything reaches the shelf, so a placeholder left for hand-written copy never shows on the page.
    unfilled = sorted(set(re.findall(r'\{\{(\w+)\}\}', template)) - FILLED)
    if unfilled:
        sys.exit(f'voyage_page.html has placeholders nothing fills: {", ".join(unfilled)}')
    media = os.path.join(SHELF, page_name)
    os.makedirs(media, exist_ok=True)
    if package:
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
    page =template.replace('{{ROUND}}', html.escape(round_name)).replace('{{TITLE_ART}}', html.escape(figlet('ASCII Odyssey', 'slant')))
    page = page.replace('{{TITLE}}', html.escape(title)).replace('{{PAGE}}', page_name)
    page = page.replace('{{INSTALL}}', INSTALL.format(zip=html.escape(os.path.basename(package))) if package else WAITING)
    page = page.replace('{{PIECES}}', '\n'.join(card(p, page_name) for p in pieces))
    page = page.replace('{{FACES}}', faces(render_dir, media, page_name))
    with open(os.path.join(SHELF, page_name + '.html'), 'w') as f:
        f.write(page)
    subprocess.run([os.path.join(SHELF, 'refresh-index.py')], check=False, cwd=SHELF)


if __name__ == '__main__':
    main()
