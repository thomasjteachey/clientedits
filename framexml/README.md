# FrameXML customisations

Source for the `BPlus*.lua` modules that ship inside `patch-enUS-A.MPQ` under
`Interface/FrameXML/`, plus the patched `FrameXML.toc` that loads them.

**These had no source tree.** Until 2026-09-02 the only copy of
`BPlusPetTrainingData.lua`, `BPlusBeastTrainingTradeSkill.lua` and
`BPlusGuildTabFix.lua` was the one inside the MPQ - repack the archive from a
stale extract and they are gone with no diff to show for it. Anything added here
should also be extracted back out of the archive so this directory stays the
authority rather than a partial copy.

To ship a change:

    smpq -e -f patch-enUS-A.MPQ                    # from a staging dir
    cp <file> Interface/FrameXML/
    smpq -c -M 1 -f patch-enUS-A.new.MPQ <files>   # from that staging dir
    # verify by extracting again, then zip and bump patch-enUS-A.version

New modules go in FrameXML.toc above the `## add new modules above here` marker,
alongside the other BPlus entries.
