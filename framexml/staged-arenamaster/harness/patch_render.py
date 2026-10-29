"""Turn a copy of the bounty board's render_dump.py into render_am.py for this harness."""
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = r'C:\Projects\Gamedev\wow\clientedits\bountyboard\harness\render_dump.py'
DST = os.path.join(HERE, 'render_am.py')
shutil.copy(SRC, DST)
s = open(DST, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)


rep("BB = os.path.dirname(HERE)", "BB = r'C:\\Projects\\Gamedev\\wow\\clientedits\\bountyboard'")
rep("UIP = snap['uiparent']", "UIP = snap['uiparent']\nif hover is None:\n    hover = snap.get('hover')")
rep("btn = texture('Interface\\\\Buttons\\\\UI-Panel-Button-Up')",
    "btn = texture('Interface\\\\Buttons\\\\UI-Panel-Button-Disabled' if o.get('_disabled') "
    "else 'Interface\\\\Buttons\\\\UI-Panel-Button-Up')")
rep("""            if o['_kind'] == 'Texture':
                draw_texture(o, a)
            else:
                draw_text(o, a)""", """            if o['_kind'] == 'Texture':
                draw_texture(o, a)
            else:
                if f.get('_panelButton') and f.get('_disabled'):
                    o = dict(o, _textColour=[0.5, 0.5, 0.5])
                draw_text(o, a)""")
rep("root = snap['root']\ndraw_frame(root)",
    "root = snap['root']\ndraw_frame(root)\nfor extra in snap.get('extra') or []:\n    draw_frame(extra)")
open(DST, 'w', encoding='utf-8').write(s)
print('render_am.py written')
