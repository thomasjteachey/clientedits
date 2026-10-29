#!/bin/bash
# addons_replace_replay.sh: put the staged CENTURION_Replay files into the live
# addons.zip (replacing the ones there, adding CenturionReplaySpectate.lua and
# CenturionReplayScore.lua),
# every other entry byte for byte as it was, then bump addons.version LAST and
# read both back over HTTP. Run on the game box from the folder holding the
# staged CENTURION_Replay/ (copied up from clientedits/framexml/staged-replay).
set -e
TAG=replay-spectate
P=/var/www/html/downloads/patches
W=$(pwd)
D=CENTURION_Replay
FILES="CENTURION_Replay.toc SunwellAS.lua CenturionReplaySpectate.lua CenturionReplayScore.lua"
for f in $FILES; do [ -f "$D/$f" ] || { echo "no $D/$f in $W"; exit 1; }; done
CUR=$(cat $P/addons.version)
cp -p $P/addons.zip live.zip
unzip -l live.zip | grep -q " $D/SunwellAS.lua\$" || { echo "$D is not in the live zip"; exit 1; }
rm -rf old new stage && mkdir -p old new stage/$D
(cd old && unzip -q ../live.zip)
cp live.zip addons.zip
for f in $FILES; do cp $D/$f stage/$D/; done
(cd stage && zip -q -6 ../addons.zip $(for f in $FILES; do echo $D/$f; done))
(cd new && unzip -q ../addons.zip)
unzip -tq addons.zip
for f in $FILES; do cmp -s new/$D/$f $D/$f && echo "new $D/$f in the zip: ok" || { echo "NEW FILE MISMATCH $f"; exit 1; }; done
skip=" ./$D/\(CENTURION_Replay\.toc\|SunwellAS\.lua\|CenturionReplaySpectate\.lua\|CenturionReplayScore\.lua\)\$"
(cd old && find . -type f | sort | xargs -d '\n' md5sum) | grep -v "$skip" > old.md5
(cd new && find . -type f | sort | xargs -d '\n' md5sum) | grep -v "$skip" > new.md5
cmp -s old.md5 new.md5 && echo "other entries: $(wc -l < old.md5) identical" || { echo "OTHER ENTRIES CHANGED"; diff old.md5 new.md5 | head; exit 1; }
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
for f in $FILES; do unzip -p served.zip $D/$f | cmp -s - $D/$f && echo "served $D/$f matches" || { echo "SERVED FILE DIFFERS $f"; exit 1; }; done
rm -rf old new stage served.zip live.zip
