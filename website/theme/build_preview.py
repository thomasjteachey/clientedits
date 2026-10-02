"""Preview bundle: the live all.min.css recoloured exactly as the rebuilt one will be.

The CMS minifier inlines small images as base64 data URIs, so those are decoded,
recoloured with the same function and re-encoded; the logo block from custom.css is
appended (the live bundle predates it).
"""
import base64
import io
import os
import re
import tempfile

import orangeify as o

t = open('writable/cache/data/minify/all.min.css', encoding='utf-8', errors='surrogateescape').read()


def fix_data(m):
    raw = base64.b64decode(m.group(2))
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, 'a.png'), os.path.join(d, 'b.png')
        open(src, 'wb').write(raw)
        o.image(src, dst)
        return m.group(1) + base64.b64encode(open(dst, 'rb').read()).decode()


t = re.sub(r'(data:image/png;base64,)([A-Za-z0-9+/=]+)', fix_data, t)
t = o.css(t)
custom = open('out/application/themes/heaven/assets/css/custom.css', encoding='utf-8').read()
extra = '\n/* Centurion logo in the header and footer' + custom.split('/* Centurion logo in the header and footer')[1]
# custom.css urls are relative to assets/css/; in the bundle the minifier rewrites them to be
# relative to writable/cache/data/minify/ - do the same here
extra = extra.replace("url('../images/", "url('../../../../application/themes/heaven/assets/images/")
t += extra
os.makedirs('preview/writable/cache/data/minify', exist_ok=True)
open('preview/writable/cache/data/minify/all.min.css', 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
print('bundle', len(t))
