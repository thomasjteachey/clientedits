set -euo pipefail
cd /var/www/html/downloads/patches
OLD=$(cat addons.version)
echo "served addons.version=$OLD"
[ "$OLD" = "1.00067" ] || { echo "unexpected version, stopping"; exit 1; }
SRC=/tmp/centmode/gtimer
DIR=CENTURION_GurubashiTimer
if unzip -l addons.zip | grep -q " $DIR/"; then echo "$DIR already in the zip, stopping"; exit 1; fi
REF=CENTURION_HeirloomColorFix/CENTURION_HeirloomColorFix.lua
METHOD=$(unzip -v addons.zip "$REF" | awk 'NR==4{print $2}')
W=/tmp/addons-gurubashitimer-build
rm -rf "$W" && mkdir -p "$W/stage/$DIR"
cp addons.zip "$W/addons.zip"
cp "$SRC/$DIR.lua" "$SRC/$DIR.toc" "$W/stage/$DIR/"
cd "$W/stage"
if [ "$METHOD" = "Stored" ]; then zip -0 -r "$W/addons.zip" "$DIR"; else zip -9 -r "$W/addons.zip" "$DIR"; fi
cd "$W"
unzip -tq addons.zip
for f in "$DIR/$DIR.lua" "$DIR/$DIR.toc"; do
  echo "$f zip=$(unzip -p addons.zip "$f" | md5sum | cut -c1-32) tested=$(md5sum "$W/stage/$f" | cut -c1-32)"
done
diff <(unzip -v /var/www/html/downloads/patches/addons.zip | awk 'NF>=8 {print $7, $8}') \
     <(unzip -v addons.zip | awk 'NF>=8 {print $7, $8}' | grep -v " $DIR/") && echo "all existing members byte-identical (CRC)"
echo "members before: $(unzip -l /var/www/html/downloads/patches/addons.zip | tail -1)"
echo "members after:  $(unzip -l addons.zip | tail -1)"
ls -la "$W/addons.zip"
