#!/bin/bash
# addons_add_allstar.sh: add the CENTURION_AllStar folder (.toc + .lua) to the
# live addons.zip, every existing entry byte for byte as it was, then bump
# addons.version LAST and read both back over HTTP. Modelled on
# ~/addons-levelspam/publish.sh. Refuses if the folder is already in the zip.
set -e
TAG=allstar
P=/var/www/html/downloads/patches
W=~/allstar-20261028/client
D=CENTURION_AllStar
cd $W
for f in $D.toc $D.lua; do [ -f "$f" ] || { echo "no $f in $W"; exit 1; }; done
CUR=$(cat $P/addons.version)
cp -p $P/addons.zip live.zip
unzip -l live.zip | grep -q " $D/" && { echo "$D is already in the live zip"; exit 1; }
rm -rf old new stage && mkdir -p old new stage/$D
(cd old && unzip -q ../live.zip)
cp live.zip addons.zip
cp $D.toc $D.lua stage/$D/
(cd stage && zip -q -6 ../addons.zip $D/$D.toc $D/$D.lua)
(cd new && unzip -q ../addons.zip)
unzip -tq addons.zip
for f in $D.toc $D.lua; do cmp -s new/$D/$f $f && echo "new $D/$f in the zip: ok" || { echo "NEW FILE MISMATCH $f"; exit 1; }; done
(cd old && find . -type f | sort | xargs -d '\n' md5sum) > old.md5
(cd new && find . -type f | sort | xargs -d '\n' md5sum) | grep -v " ./$D/" > new.md5
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
unzip -p served.zip $D/$D.lua | cmp -s - $D.lua && echo "served $D/$D.lua matches" || { echo "SERVED FILE DIFFERS"; exit 1; }
rm -rf old new stage served.zip live.zip
