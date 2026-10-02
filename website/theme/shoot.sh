#!/bin/bash
# shoot.sh <path> <name> [height] : before/after screenshots of one site page, side by side
cd "$(dirname "$0")"
PATH_=$1; NAME=$2; HGT=${3:-1400}
"/c/Program Files/PuTTY/plink.exe" -agent -batch -ssh -hostkey "SHA256:A+/c0SFXvxUzvp9UtQeh/49loAJqkby0eEXNVy0O9Wc" brokilodeluxe@192.168.1.226 \
  "curl -sk -H 'Host: centurionpvp.com' 'https://127.0.0.1$PATH_'" > "pg_$NAME.html"
python - "$NAME" <<'EOF'
import re, sys
n = sys.argv[1]
h = open('pg_%s.html' % n, encoding='utf-8').read()
B = 'https://centurionpvp.com/'
def sw(m):
    u = m.group(2)
    if re.match(re.escape(B) + r'(writable/cache|application/(themes|modules|css))/', u) and re.search(r'\.(css|png|jpe?g|gif|webp)(\?|$)', u):
        u = B + 'orange-preview/' + u[len(B):].split('?')[0]
    return m.group(1) + u + m.group(3)
hide = '<style>.swal2-container{display:none!important}body.swal2-shown{overflow:auto!important}</style>'
o = re.sub(r'((?:href|src)=")([^"]+)(")', sw, h)
for name, t in (('before', h), ('after', o)):
    t = t.replace('<head>', '<head><base href="https://centurionpvp.com/">', 1).replace('</head>', hide + '</head>', 1)
    open('pg_%s_%s.html' % (n, name), 'w', encoding='utf-8').write(t)
EOF
W=$(cygpath -w "$PWD")
for v in before after; do
  rm -rf chromeprof
  "/c/Program Files/Google/Chrome/Application/chrome.exe" --headless=new --disable-gpu --hide-scrollbars --user-data-dir="$W\\chromeprof" \
    --window-size=1440,$HGT --virtual-time-budget=8000 --screenshot="$W\\pg_${NAME}_$v.png" "file:///$W\\pg_${NAME}_$v.html" 2>/dev/null
done
python -c "
from PIL import Image
a=Image.open('pg_${NAME}_before.png'); b=Image.open('pg_${NAME}_after.png')
w,h=a.size; s=720/w
c=Image.new('RGB',(1450,int(h*s)),'white'); c.paste(a.resize((720,int(h*s))),(0,0)); c.paste(b.resize((720,int(h*s))),(730,0)); c.save('cmp_${NAME}.png')"
