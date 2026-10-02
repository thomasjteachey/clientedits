set -euo pipefail
cd /var/www/html/downloads/patches
OLD=$(cat addons.version)
echo "served addons.version=$OLD"
[ "$OLD" = "1.00066" ] || { echo "unexpected version, stopping"; exit 1; }
SRC=/tmp/claude-iqc/addon_fixed.lua
MEMBER=CENTURION_HeirloomColorFix/CENTURION_HeirloomColorFix.lua
unzip -v addons.zip "$MEMBER" | sed -n 2,4p
METHOD=$(unzip -v addons.zip "$MEMBER" | awk 'NR==4{print $2}')
W=/tmp/addons-heirloomhex-build
rm -rf "$W" && mkdir -p "$W/stage/CENTURION_HeirloomColorFix"
cp addons.zip "$W/addons.zip"
cp "$SRC" "$W/stage/$MEMBER"
cd "$W/stage"
if [ "$METHOD" = "Stored" ]; then zip -0 "$W/addons.zip" "$MEMBER"; else zip -9 "$W/addons.zip" "$MEMBER"; fi
cd "$W"
unzip -tq addons.zip
echo "member md5 in new zip: $(unzip -p addons.zip "$MEMBER" | md5sum | cut -c1-32)"
echo "tested file md5:       $(md5sum "$SRC" | cut -c1-32)"
diff <(unzip -v /var/www/html/downloads/patches/addons.zip | awk 'NF>=8 {print $7, $8}' | grep -v "$MEMBER") \
     <(unzip -v addons.zip | awk 'NF>=8 {print $7, $8}' | grep -v "$MEMBER") && echo "all other members byte-identical (CRC)"
unzip -l addons.zip | tail -1
unzip -v addons.zip "$MEMBER" | sed -n 4p
ls -la "$W/addons.zip"
