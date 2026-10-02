set -euo pipefail
cd /var/www/html/downloads/patches
OLD=$(cat addons.version)
[ "$OLD" = "1.00066" ] || { echo "unexpected version $OLD, stopping"; exit 1; }
NEW=1.00067
W=/tmp/addons-heirloomhex-build
cp -p addons.zip "addons.zip.bak-heirloomhex-$OLD"
cp -p addons.version "addons.version.bak-heirloomhex-$OLD"
cp "$W/addons.zip" addons.zip
A=$(md5sum "$W/addons.zip" | cut -c1-32); B=$(md5sum addons.zip | cut -c1-32)
echo "built=$A served=$B"
[ "$A" = "$B" ] || { echo "md5 mismatch, restoring backup"; cp -p "addons.zip.bak-heirloomhex-$OLD" addons.zip; exit 1; }
printf '%s' "$NEW" > addons.version
echo "addons.version=$(cat addons.version) ($(wc -c < addons.version) bytes)"
ls -la --time-style=long-iso addons.zip addons.version addons.zip.bak-heirloomhex-$OLD addons.version.bak-heirloomhex-$OLD
for host in 127.0.0.1 centurionpvp.com; do
  echo "http $host version: $(curl -s --max-time 10 http://$host/downloads/patches/addons.version)"
  echo "http $host zip md5: $(curl -s --max-time 30 http://$host/downloads/patches/addons.zip | md5sum | cut -c1-32)"
done
