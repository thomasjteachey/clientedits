#!/bin/bash
# addons_replace_allstar.sh: replace CENTURION_AllStar/CENTURION_AllStar.lua in
# the live addons.zip with the staged one, every other entry byte for byte as
# it was, then bump addons.version LAST and read both back over HTTP. The
# replace twin of addons_add_allstar.sh. Refuses if the folder is not in the
# zip yet, or if the staged file is already what the zip holds.
set -e
TAG=allstar-lives
P=/var/www/html/downloads/patches
W=~/allstar-20261028/client
D=CENTURION_AllStar
F=$D.lua
cd $W
[ -f "$F" ] || { echo "no $F in $W"; exit 1; }
CUR=$(cat $P/addons.version)
cp -p $P/addons.zip live.zip
unzip -l live.zip | grep -q " $D/$F\$" || { echo "$D/$F is not in the live zip"; exit 1; }
unzip -p live.zip $D/$F | cmp -s - $F && { echo "live $D/$F is already the staged one"; exit 1; }
rm -rf old new stage && mkdir -p old new stage/$D
(cd old && unzip -q ../live.zip)
cp live.zip addons.zip
cp $F stage/$D/
(cd stage && zip -q -6 ../addons.zip $D/$F)
(cd new && unzip -q ../addons.zip)
unzip -tq addons.zip
cmp -s new/$D/$F $F && echo "new $D/$F in the zip: ok" || { echo "NEW FILE MISMATCH $F"; exit 1; }
(cd old && find . -type f | sort | xargs -d '\n' md5sum) | grep -v " ./$D/$F\$" > old.md5
(cd new && find . -type f | sort | xargs -d '\n' md5sum) | grep -v " ./$D/$F\$" > new.md5
cmp -s old.md5 new.md5 && echo "other entries: $(wc -l < old.md5) identical" || { echo "OTHER ENTRIES CHANGED"; diff old.md5 new.md5 | head; exit 1; }
echo "entries: live $(unzip -l live.zip | tail -1 | awk '{print $2}'), new $(unzip -l addons.zip | tail -1 | awk '{print $2}')"
[ "$(cat $P/addons.version)" = "$CUR" ] && cmp -s live.zip $P/addons.zip || { echo "live addons moved while staging - rerun"; exit 1; }
cp -p $P/addons.zip $P/addons.zip.bak-$TAG-$CUR
cp -p $P/addons.version $P/addons.version.bak-$TAG-$CUR
cp addons.zip $P/addons.zip.new && mv $P/addons.zip.new $P/addons.zip
N=$(python3 -c "import sys; a,b=sys.argv[1].split('.'); print('%s.%05d' % (a, int(b)+1), end='')" "$CUR")
printf '%s' "$N" > $P/addons.version.new && mv $P/addons.version.new $P/addons.version
echo "addons $CUR -> $(cat $P/addons.version) ($(stat -c %s $P/addons.version) bytes)"
echo "served version: $(curl -s --max-time 10 http://127.0.0.1/downloads/patches/addons.version)"
curl -s --max-time 60 -o served.zip http://127.0.0.1/downloads/patches/addons.zip
cmp -s served.zip addons.zip && echo "served zip identical" || { echo "SERVED ZIP DIFFERS"; exit 1; }
unzip -p served.zip $D/$F | cmp -s - $F && echo "served $D/$F matches" || { echo "SERVED FILE DIFFERS"; exit 1; }
rm -rf old new stage served.zip live.zip
