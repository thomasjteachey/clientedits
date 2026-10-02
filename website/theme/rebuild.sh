#!/bin/bash
# Rebuild the staged orange theme (out/) and the public preview (/orange-preview/).
set -e
cd "$(dirname "$0")"
rm -rf out
python orangeify.py . out | tail -1
rm -rf out/application/modules/admin out/application/modules/install
cp keep/custom.css out/application/themes/heaven/assets/css/
cp keep/logo.png out/application/themes/heaven/assets/images/graphics/
cp keep/favicon.png out/application/themes/heaven/assets/images/
# the hero is the owner's night scene (hero-night.jpg), used as-is - not recoloured
cp ../night/hero-night.jpg out/application/themes/heaven/assets/images/graphics/hero-night.jpg
# mobile address-bar colour
mkdir -p out/application/themes/heaven/config
python - <<'EOF'
import orangeify as o
p = 'application/themes/heaven/config/config.php'
t = open(p, encoding='utf-8').read()
new = t.replace("'theme_color' => '#11192c'", "'theme_color' => '%s'" % o.HEX.sub(o.fix_hex, '#11192c'))
new = new.replace("'version'     => '1.0',", "'version'     => '1.1',")   # re-versions all.min.css/js links
assert new.count("'version'     => '1.1'") == 1 and "#11192c" not in new
for tpl in ('views/parts/header.tpl', 'views/parts/footer.tpl'):
    q = 'application/themes/heaven/' + tpl
    tt = open(q, encoding='utf-8').read()
    nt = tt.replace('graphics/logo.png"', 'graphics/logo.png?v=1.1"')
    assert nt.count('logo.png?v=1.1') == 1
    import os
    os.makedirs(os.path.dirname('out/' + q), exist_ok=True)
    open('out/' + q, 'w', encoding='utf-8', newline='').write(nt)
open('out/' + p, 'w', encoding='utf-8', newline='').write(new)
EOF
rm -rf preview && mkdir preview && cp -r application preview/ && cp -r out/application preview/
python build_preview.py
find preview -name '*.php' -delete
tar czf preview.tgz -C preview .
P="/c/Program Files/PuTTY"; H="SHA256:A+/c0SFXvxUzvp9UtQeh/49loAJqkby0eEXNVy0O9Wc"
"$P/pscp.exe" -agent -batch -hostkey "$H" preview.tgz brokilodeluxe@192.168.1.226:/tmp/ >/dev/null
"$P/plink.exe" -agent -batch -ssh -hostkey "$H" brokilodeluxe@192.168.1.226 \
  "rm -rf /var/www/html/orange-preview && mkdir /var/www/html/orange-preview && tar xzf /tmp/preview.tgz -C /var/www/html/orange-preview && echo preview uploaded"
