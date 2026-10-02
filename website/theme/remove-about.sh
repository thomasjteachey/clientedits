#!/bin/bash
# Finish removing the About page: its cached copy, its sitemap entry and its SEO entry.
# (The menu link and the page row are already gone from the `website` DB; backups are the
#  zz_about_menu_before / zz_about_page_before tables.)
#   sudo bash remove-about.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=/var/www/html
[ "$(id -u)" -eq 0 ] || { echo "run with sudo"; exit 1; }
mkdir -p "$HERE/backup"
cp -a "$ROOT/sitemap.xml" "$ROOT/application/config/seo.php" "$HERE/backup/"

python3 - "$ROOT" <<'EOF'
import re, sys
root = sys.argv[1]
p = root + '/sitemap.xml'
t = open(p).read()
n = re.sub(r'\s*<url>\s*<loc>https://centurionpvp\.com/page/about</loc>.*?</url>', '', t, count=1, flags=re.S)
assert n != t and 'page/about' not in n, 'sitemap entry not found'
open(p, 'w').write(n)
p = root + '/application/config/seo.php'
t = open(p).read()
n = re.sub(r"\n[ \t]*'page/about' => \[.*?\n[ \t]*\],", '', t, count=1, flags=re.S)
assert n != t and 'page/about' not in n, 'seo entry not found'
open(p, 'w').write(n)
EOF

if ! php -l "$ROOT/application/config/seo.php" >/dev/null; then
	cp -a "$HERE/backup/seo.php" "$ROOT/application/config/seo.php"
	echo "ABORT: seo.php did not parse after the edit - restored it"
	exit 1
fi
rm -f "$ROOT"/writable/cache/data/page_about_*.cache
echo "about page removed: cache cleared, sitemap + seo entries dropped (backups in $HERE/backup)"
