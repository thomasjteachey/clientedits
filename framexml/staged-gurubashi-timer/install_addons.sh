set -euo pipefail
cd /var/www/html/downloads/patches
OLD=$(cat addons.version)
[ "$OLD" = "1.00067" ] || { echo "unexpected version $OLD, stopping"; exit 1; }
NEW=1.00068
W=/tmp/addons-gurubashitimer-build
cp -p addons.zip "addons.zip.bak-gurubashitimer-$OLD"
cp -p addons.version "addons.version.bak-gurubashitimer-$OLD"
cp "$W/addons.zip" addons.zip
A=$(md5sum "$W/addons.zip" | cut -c1-32); B=$(md5sum addons.zip | cut -c1-32)
echo "built=$A served=$B"
[ "$A" = "$B" ] || { echo "md5 mismatch, restoring backup"; cp -p "addons.zip.bak-gurubashitimer-$OLD" addons.zip; exit 1; }
printf '%s' "$NEW" > addons.version
echo "addons.version=$(cat addons.version) ($(wc -c < addons.version) bytes)"
ls -la --time-style=long-iso addons.zip addons.version "addons.zip.bak-gurubashitimer-$OLD" "addons.version.bak-gurubashitimer-$OLD"
for host in 127.0.0.1 centurionpvp.com; do
  echo "http $host version: $(curl -s --max-time 10 http://$host/downloads/patches/addons.version)"
  echo "http $host zip md5: $(curl -s --max-time 30 http://$host/downloads/patches/addons.zip | md5sum | cut -c1-32)"
done
