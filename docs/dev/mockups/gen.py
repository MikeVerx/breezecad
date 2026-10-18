#!/usr/bin/env python3
"""Mockups of the phone UI (BreezeCAD Android), 915x412 dp, rendered to PNG."""
import os, re, sys
from playwright.sync_api import sync_playwright

ICONS = '/home/claude/breezecad/assets/icons'
HERE = os.path.dirname(os.path.abspath(__file__))


def icon(name, size=18, color='#AEB4BD', style='', dark=True):
    p = os.path.join(ICONS, name + '.svg')
    s = open(p).read()
    s = s.replace('currentColor', color)
    if dark: s = s.replace('#3D4552', '#C9D1DB')   # proposed: dark arrows turn light in the dark theme
    s = re.sub(r'<svg ', f'<svg width="{size}" height="{size}" style="flex:none;{style}" ', s, count=1)
    return s


CSS = """
@font-face{font-family:Plex;src:url(file:///home/claude/breezecad/assets/fonts/IBMPlexSans-Regular.ttf);font-weight:400}
@font-face{font-family:Plex;src:url(file:///home/claude/breezecad/assets/fonts/IBMPlexSans-Medium.ttf);font-weight:500}
@font-face{font-family:Plex;src:url(file:///home/claude/breezecad/assets/fonts/IBMPlexSans-SemiBold.ttf);font-weight:600}
:root{--bg:#16181C;--ribbon:#1D2025;--panel:#1B1E22;--field:#23272D;--border:#353A42;--div:#262A30;--text:#E6E8EB;--t2:#AEB4BD;--mut:#9AA1AB;--acc:#3F6FEA;--accSoft:#1F2A40;--accB:#3A5BA0;--warn:#F5A524;--red:#F06A6E;--sel:#6E9BFF}
*{box-sizing:border-box;margin:0;padding:0}
body{width:915px;height:412px;background:var(--bg);font-family:Plex;color:var(--text);overflow:hidden;position:relative;font-size:12.5px}
.abs{position:absolute}
.cam{left:0;top:0;bottom:0;width:40px;background:#0c0d0f;display:flex;flex-direction:column;align-items:center;padding:10px 0 10px}
.cam .hb{width:32px;height:32px;border-radius:9px;display:flex;align-items:center;justify-content:center;margin-top:6px}
.cutout{left:12px;top:198px;width:16px;height:16px;border-radius:50%;background:#000;border:2px solid #222}
.b1{left:40px;top:0;bottom:0;width:186px;background:var(--panel);border-right:1px solid var(--div)}
.b2{right:0;top:0;bottom:0;width:180px;background:var(--panel);border-left:1px solid var(--div)}
.brow{height:44px;display:flex;align-items:center;gap:16px;padding:0 14px}
.tabs{display:flex;gap:6px;padding:0 8px;height:38px;align-items:center}
.tab{height:30px;border-radius:8px;display:flex;align-items:center;gap:6px;padding:0 10px;color:var(--t2)}
.tab.on{background:var(--field);color:var(--text)}
.tree{padding:8px 12px;line-height:34px;color:var(--text)}
.tree div{display:flex;align-items:center;gap:8px}
.row{left:0;right:0;bottom:0;height:56px;display:flex;gap:8px;padding:8px 10px;border-top:1px solid var(--div)}
.rb{flex:1;border-radius:10px;background:var(--field);display:flex;align-items:center;justify-content:center}
.rb.ok{background:var(--acc)}
.view{left:226px;right:180px;top:0;bottom:40px;background:linear-gradient(#2A2E35,#17191D);overflow:hidden}
.isl{background:rgba(27,30,34,.96);border:1px solid var(--div);border-radius:12px;display:flex;align-items:center;gap:4px;padding:0 6px;height:40px}
.isl .t{padding:0 10px;height:30px;display:flex;align-items:center;border-radius:8px;color:var(--t2)}
.isl .t.on{background:var(--accSoft);color:#9db6ff}
.strip{left:226px;right:180px;bottom:0;height:40px;border-top:1px solid var(--div);background:var(--bg);display:flex;align-items:center;gap:8px;padding:0 8px}
.chip{height:28px;border-radius:8px;border:1px solid var(--accB);background:var(--accSoft);padding:0 12px;display:flex;align-items:center;color:#9db6ff;font-size:12px}
.chip.off{border-color:var(--border);background:none;color:var(--t2)}
.b2 .grp{font-size:10.5px;letter-spacing:.8px;color:var(--mut);padding:12px 14px 4px}
.b2 .it{display:flex;align-items:center;gap:10px;padding:0 14px;height:44px}
.search{height:30px;border-radius:9px;background:var(--field);display:flex;align-items:center;gap:6px;padding:0 10px;color:var(--mut);flex:1}
.beard{left:0;right:0;bottom:0;height:26px;display:flex;align-items:center;padding:0 12px;font-size:10.5px;color:var(--mut);gap:8px;border-top:1px solid var(--div)}
.note{font-size:11px;color:#ffcf6b;background:rgba(0,0,0,.55);border:1px dashed rgba(255,207,107,.6);border-radius:8px;padding:5px 9px;line-height:1.35;z-index:50}
"""

MODEL = """<svg width="300" height="200" viewBox="0 0 300 200"><path d="M40 120l110-55 115 55-110 55z" fill="#A7AFBA"/><path d="M40 120v18l115 55v-18z" fill="#7b8390"/><path d="M155 175v18l110-55v-18z" fill="#5f6672"/><path d="M95 92l45-22 45 22v-55l-45-22-45 22z" fill="#c3c9d1"/><path d="M95 92v-55l45 22v55z" fill="#8c95a3"/><ellipse cx="200" cy="122" rx="16" ry="8" fill="#3a3f47"/><ellipse cx="90" cy="128" rx="12" ry="6" fill="#3a3f47"/></svg>"""


def camera(hotbar_bottom='ui-eye-off', bottom_on=False, extra=''):
    hb = lambda n, on=False: f'<div class="hb" style="{"background:var(--accSoft);border:1px solid var(--accB)" if on else ""}">{icon(n, 18, "#9db6ff" if on else "#AEB4BD")}</div>'
    return f'''<div class="abs cam" style="{extra}">{icon('brand-mark', 22)}{hb('ui-plus')}{hb('section')}{hb('measure')}
<div style="margin-top:auto">{hb(hotbar_bottom, bottom_on)}</div></div><div class="abs cutout"></div>'''


def block1(style=''):
    return f'''<div class="abs b1" style="{style}"><div class="brow">{icon('ui-save')}{icon('ui-open')}{icon('ui-ruler', 18, '#6E9BFF')}</div>
<div class="tabs"><div class="tab on">{icon('ui-tree', 16, '#E6E8EB')}Дерево</div><div class="tab">{icon('ui-wrench', 16)}</div><div class="tab">{icon('ui-dimension', 16)}</div></div>
<div class="tree"><div>{icon('tree-part', 16)}Кронштейн</div><div style="padding-left:14px">{icon('tree-origin', 16)}Начало координат</div>
<div style="padding-left:14px">{icon('tree-sketch', 16)}Эскиз1</div><div style="padding-left:14px">{icon('tree-extrude', 16)}Выдавливание1</div><div style="padding-left:14px">{icon('tree-hole', 16)}Отверстие1</div></div>
<div class="abs row"><div class="rb">{icon('ui-frame')}</div><div class="rb">{icon('ui-cursor')}</div><div class="rb">{icon('ui-close')}</div><div class="rb ok">{icon('ui-check', 18, '#fff')}</div></div></div>'''


def block2_list(style=''):
    it = lambda i, t, more=False: f'<div class="it">{icon(i, 22, "#fff")}<span>{t}</span>{"<span style=\'margin-left:auto\'>" + icon("ui-chevron-right", 14) + "</span>" if more else ""}</div>'
    return f'''<div class="abs b2" style="{style}"><div class="brow" style="gap:8px;padding:0 10px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">60 fps</span><div class="search">{icon('ui-search', 14)}Поиск</div></div>
<div class="grp">ЭСКИЗ</div>{it('sketch', 'Начать эскиз')}<div class="grp">СОЗДАТЬ</div>{it('extrude', 'Выдавливание')}{it('revolve', 'Вращение', True)}{it('hole', 'Отверстие')}
<div class="abs beard">Готово<div style="flex:1;height:3px;border-radius:2px;background:var(--field)"></div></div></div>'''


def view(inner='', style='', islands=True):
    isl = ''
    if islands:
        isl = f'''<div class="abs isl" style="left:8px;top:8px">{icon('ui-undo')}<span style="width:6px"></span>{icon('ui-redo')}</div>
<div class="abs isl" style="left:84px;top:8px">{icon('ui-arrow-left', 16)}<div class="t on">3D-модель</div><div class="t">Эскиз</div><div class="t">Вид</div></div>
<div class="abs isl" style="right:8px;top:8px;width:40px;justify-content:center">{icon('ui-cube', 20)}</div>'''
    return f'''<div class="abs view" style="{style}"><div class="abs" style="left:110px;top:90px">{MODEL}</div>
<svg class="abs" style="left:18px;bottom:16px" width="44" height="44" viewBox="0 0 44 44"><path d="M14 30V8" stroke="#4f7df5" stroke-width="2"/><path d="M14 30l20 8" stroke="#4CBB7F" stroke-width="2"/><path d="M14 30l-8 8" stroke="#F06A6E" stroke-width="2"/></svg>
{isl}{inner}</div>'''


def strip(style=''):
    return f'''<div class="abs strip" style="{style}"><div style="width:30px;height:30px;border-radius:8px;display:flex;align-items:center;justify-content:center">{icon('ui-home', 18)}</div><div class="chip">Кронштейн</div><div class="chip off">Фланец</div></div>'''


def page(body, extra_css=''):
    return f'<!doctype html><html><head><meta charset="utf-8"><style>{CSS}{extra_css}</style></head><body>{body}</body></html>'


def note(text, style):
    return f'<div class="abs note" style="{style}">{text}</div>'


# ---------------------------------------------------------------------------------
pages = {}
SIZES = {}

# 1a. The hotbar: "Скрыть управление" at the bottom (was the palette).
pages['1a-hotbar'] = page(camera() + block1() + view() + block2_list() + strip() +
    note('Внизу хотбара вместо палитры —<br><b>«Скрыть управление»</b>', 'left:240px;top:300px'))

# 1b. Controls hidden: only the view, the logo and the hotbar.
hidden_view = f'''<div class="abs" style="left:40px;right:0;top:0;bottom:0;background:linear-gradient(#2A2E35,#17191D)">
<div class="abs" style="left:300px;top:110px;transform:scale(1.25);transform-origin:0 0">{MODEL}</div>
<svg class="abs" style="left:18px;bottom:16px" width="44" height="44" viewBox="0 0 44 44"><path d="M14 30V8" stroke="#4f7df5" stroke-width="2"/><path d="M14 30l20 8" stroke="#4CBB7F" stroke-width="2"/><path d="M14 30l-8 8" stroke="#F06A6E" stroke-width="2"/></svg></div>'''
pages['1b-hidden'] = page(camera('ui-eye', True) + hidden_view +
    note('Управление скрыто: вид на весь экран, слева — лого и хотбар.<br>Кнопка подсвечена — тап возвращает всё. Жесты по виду работают.', 'left:60px;top:14px'))

# 2. The layout editor.
EDITOR_CSS = """
.ed-top{left:40px;right:0;top:0;height:0}
.close{right:10px;top:10px;width:40px;height:40px;border-radius:12px;background:#E5484D;display:flex;align-items:center;justify-content:center;z-index:40;box-shadow:0 4px 14px rgba(0,0,0,.4)}
.hint{left:226px;right:180px;top:150px;text-align:center;color:var(--t2);font-size:15px;font-weight:500}
.hint small{display:block;font-size:11.5px;color:var(--mut);font-weight:400;margin-top:6px}
.dim{opacity:.38}
.num{width:46px;height:46px;border-radius:50%;background:var(--acc);color:#fff;font-weight:600;font-size:22px;display:flex;align-items:center;justify-content:center;box-shadow:0 0 0 4px rgba(63,111,234,.25);z-index:30}
.edge{width:4px;background:#6E9BFF;border-radius:2px;z-index:30;box-shadow:0 0 10px rgba(110,155,255,.8)}
.pill{width:84px;height:34px;border-radius:17px;background:#1B1E22;border:2px solid #6E9BFF;display:flex;align-items:center;justify-content:space-between;padding:0 9px;z-index:31;box-shadow:0 4px 12px rgba(0,0,0,.5)}
.pill i{display:block;width:2px;height:14px;background:#9db6ff;border-radius:1px}
.swap{width:120px;height:40px;border-radius:20px;background:var(--acc);color:#fff;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:500;z-index:31;box-shadow:0 4px 14px rgba(0,0,0,.45)}
.selbox{border:2px solid #6E9BFF;border-radius:14px;z-index:29;box-shadow:0 0 0 4px rgba(110,155,255,.18)}
.guide{border:1.5px dashed rgba(110,155,255,.55);border-radius:12px;z-index:20}
.slider{height:34px;border-radius:17px;background:rgba(27,30,34,.96);border:1px solid var(--border);display:flex;align-items:center;gap:10px;padding:0 14px;z-index:31;color:var(--t2);font-size:11.5px}
.track{flex:1;height:4px;border-radius:2px;background:#15171B;position:relative}
.track b{position:absolute;left:0;top:0;bottom:0;border-radius:2px;background:var(--acc)}
.track u{position:absolute;top:-6px;width:16px;height:16px;border-radius:50%;background:#2E333B;border:2px solid var(--acc)}
.tag{font-size:11px;color:#9db6ff;background:var(--accSoft);border:1px solid var(--accB);border-radius:7px;padding:3px 8px;z-index:31}
"""
close = f'<div class="abs close">{icon("ui-close", 20, "#fff")}</div>'
empty_view = '<div class="abs" style="left:226px;right:180px;top:0;bottom:40px;background:repeating-linear-gradient(45deg,#17191D 0 10px,#1a1c21 10px 20px)"></div>'

# 2a: nothing chosen yet.
pages['2a-editor-start'] = page(camera() + block1() + empty_view + block2_list() + strip() + close +
    f'<div class="abs hint">Выберите элемент управления<small>Тап по блоку, островку, хотбару или полосе проектов —<br>появится то, что с ним можно сделать</small></div>' +
    view('', 'background:none;pointer-events:none', True).replace('<div class="abs" style="left:110px;top:90px">' + MODEL + '</div>', ''), EDITOR_CSS)

# 2b: a block chosen (block 1): numbers, see-through content, swap, the pill on the edge.
pages['2b-editor-blocks'] = page(camera() + block1('opacity:1') +
    '<div class="abs" style="left:40px;top:0;bottom:0;width:186px;background:rgba(22,24,28,.55)"></div>' + empty_view +
    block2_list() + '<div class="abs" style="right:0;top:0;bottom:0;width:180px;background:rgba(22,24,28,.55)"></div>' + strip() + close +
    '<div class="abs selbox" style="left:40px;top:0;width:186px;height:412px;border-radius:0"></div>' +
    '<div class="abs num" style="left:110px;top:150px">1</div><div class="abs num" style="right:67px;top:150px;opacity:.85">2</div>' +
    '<div class="abs edge" style="left:224px;top:0;height:412px"></div>' +
    '<div class="abs pill" style="left:183px;top:189px"><span style="color:#9db6ff;font-size:13px">‹</span><i></i><i></i><span style="color:#9db6ff;font-size:13px">›</span></div>' +
    '<div class="abs swap" style="left:420px;top:150px;font-size:16px">1 &nbsp;⇄&nbsp; 2</div>' +
    '<div class="abs edge" style="right:178px;top:0;height:412px;opacity:.45"></div>' +
    '<div class="abs pill" style="right:137px;top:189px;opacity:.55"><span style="color:#9db6ff;font-size:13px">‹</span><i></i><i></i><span style="color:#9db6ff;font-size:13px">›</span></div>' +
    '<div class="abs tag" style="left:400px;top:206px">Блок 1 · ширина 20 %</div>' +
    note('Содержимое блоков полупрозрачное, но читается. Цифры — какой блок где.<br>Ползунок-таблетка на границе со стороны вида тянет ширину;<br>граница подсвечена толстой линией. «1 ⇄ 2» — поменять местами.', 'left:300px;top:268px'),
    EDITOR_CSS)

# 2c: an island chosen (the tabs): slides along the top edge; the size slider in the middle of the view.
pages['2c-editor-island'] = page(camera() + '<div class="dim">' + block1() + '</div>' + empty_view + '<div class="dim">' + block2_list() + strip() + '</div>' + close +
    view(f'''<style>.view .isl{{opacity:.4}} .view .isl.selected{{opacity:1}}</style><div class="abs guide" style="left:4px;right:4px;top:4px;height:48px"></div>
<div class="abs selbox" style="left:80px;top:4px;width:262px;height:48px"></div>
<div class="abs tag" style="left:160px;top:58px">◀ тяни вдоль края ▶</div>
<div class="abs slider" style="left:90px;right:90px;top:170px">Размер<div class="track"><b style="width:55%"></b><u style="left:52%"></u></div>110 %</div>
<div class="abs tag" style="left:150px;top:214px">Вкладки · верхний край</div>''', 'background:none', True).replace('<div class="abs" style="left:110px;top:90px">' + MODEL + '</div>', '').replace('class="abs isl" style="left:84px', 'class="abs isl selected" style="left:84px') +
    note('Островок тянется пальцем вдоль края (приклеен к граням: верх, низ, бока).<br>Пунктир — куда можно поставить. Ползунок посередине вида — размер.', 'left:250px;top:300px'),
    EDITOR_CSS)

# 2d: the hotbar (width) and the projects strip (height).
pages['2d-editor-hotbar-strip'] = page(camera(extra='width:58px') + '<div class="abs" style="left:58px;top:0;bottom:0;width:4px" ></div>' +
    '<div class="dim">' + block1('left:58px') + block2_list() + '</div>' + empty_view.replace('left:226px', 'left:244px') + close +
    strip('left:244px;height:52px;border-top:none') +
    '<div class="abs edge" style="left:56px;top:0;height:412px"></div>' +
    '<div class="abs pill" style="left:15px;top:300px"><span style="color:#9db6ff;font-size:13px">‹</span><i></i><i></i><span style="color:#9db6ff;font-size:13px">›</span></div>' +
    '<div class="abs" style="left:244px;right:180px;bottom:50px;height:4px;background:#6E9BFF;border-radius:2px;box-shadow:0 0 10px rgba(110,155,255,.8);z-index:30"></div>' +
    '<div class="abs pill" style="left:450px;bottom:35px;width:34px;height:84px;flex-direction:column;padding:8px 0;justify-content:space-between;align-items:center"><span style="color:#9db6ff;font-size:12px">˄</span><i style="width:14px;height:2px"></i><i style="width:14px;height:2px"></i><span style="color:#9db6ff;font-size:12px">˅</span></div>' +
    '<div class="abs tag" style="left:70px;top:14px">Хотбар · 58 dp (не уже безопасной зоны 40)</div>' +
    '<div class="abs tag" style="left:500px;bottom:70px">Полоса проектов · 52 dp</div>' +
    note('Два примера в одной картинке (в среде выбирается что-то одно):<br>хотбар — ползунок ширины на его границе; полоса проектов — ползунок высоты.', 'left:300px;top:150px'),
    EDITOR_CSS)

# 3. Block 2, compact: four equal buttons per group, "›" opens a strip over the view.
COMPACT_CSS = """
.cg{padding:6px 10px 2px}
.cg .h{font-size:10.5px;letter-spacing:.8px;color:var(--mut);padding:6px 4px}
.cg .r{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.cg .r div{height:38px;border-radius:9px;background:var(--field);display:flex;align-items:center;justify-content:center}
.cg .r .more{background:var(--accSoft);border:1px solid var(--accB)}
.fly{left:226px;right:180px;top:116px;height:78px;background:rgba(27,30,34,.97);border-top:1px solid var(--accB);border-bottom:1px solid var(--accB);display:flex;align-items:center;gap:6px;padding:0 10px;overflow:hidden;z-index:20;box-shadow:0 8px 20px rgba(0,0,0,.4)}
.fly .ti{width:72px;height:62px;border-radius:10px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;font-size:10.5px;color:var(--t2);flex:none}
.fly .ti.on{background:var(--field);color:var(--text)}
"""
def cgroup(title, icons_, opened=False):
    cells = ''.join(f'<div>{icon(i, 22, "#fff")}</div>' for i in icons_[:3])
    more = f'<div class="more">{icon("ui-arrow-left", 16, "#9db6ff")}</div>' if not opened else f'<div class="more" style="background:var(--acc);border-color:var(--acc)">{icon("ui-chevron-right", 16, "#fff")}</div>'
    return f'<div class="cg"><div class="h">{title}</div><div class="r">{more}{cells}</div></div>'

b2c = f'''<div class="abs b2"><div class="brow" style="gap:8px;padding:0 10px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">60 fps</span><div class="search">{icon('ui-search', 14)}Поиск</div></div>
{cgroup('ЭСКИЗ', ['sketch', 'sk-project', 'work-plane'])}{cgroup('СОЗДАТЬ', ['extrude', 'revolve', 'hole'], True)}{cgroup('ИЗМЕНИТЬ', ['fillet', 'chamfer', 'shell'])}{cgroup('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])}
<div class="abs beard">Готово<div style="flex:1;height:3px;border-radius:2px;background:var(--field)"></div></div></div>'''
fly_items = [('extrude', 'Выдавить'), ('revolve', 'Вращение'), ('hole', 'Отверстие'), ('loft', 'По сечениям'), ('sweep', 'Сдвиг'), ('coil', 'Пружина'), ('thread', 'Резьба'), ('shell', 'Оболочка')]
fly = '<div class="abs fly">' + ''.join(f'<div class="ti{" on" if n == "extrude" else ""}">{icon(n, 26, "#fff")}{t}</div>' for n, t in fly_items) + \
      '<div class="abs" style="right:0;top:0;bottom:0;width:40px;background:linear-gradient(90deg,transparent,rgba(27,30,34,.97))"></div></div>'
pages['3-block2-compact'] = page(camera() + block1() + view() + b2c + strip() + fly +
    note('Компактный режим (по умолчанию): на группу — 4 равные кнопки,<br>ближняя к виду кнопка ← открывает полосу поверх вида (не залезая на блок 1).<br>Полоса листается вбок; тап вне — закрыть.', 'left:240px;top:250px'),
    COMPACT_CSS)

# 4. Settings, Minecraft-like: big tabs on the left, a two-column grid of wide buttons/sliders.
SET_CSS = """
.sw{left:40px;right:0;top:0;bottom:0;background:var(--bg)}
.st{left:40px;top:0;bottom:0;width:190px;background:var(--ribbon);border-right:1px solid var(--div);padding:14px 12px;display:flex;flex-direction:column;gap:8px}
.st h1{font-size:17px;font-weight:600;margin:2px 4px 8px}
.st .tb{height:48px;border-radius:12px;display:flex;align-items:center;gap:10px;padding:0 14px;color:var(--t2);font-size:13.5px;font-weight:500;background:var(--panel);border:1px solid var(--div)}
.st .tb.on{background:var(--accSoft);border-color:var(--accB);color:var(--text)}
.sub{left:246px;right:16px;top:14px;display:flex;gap:6px}
.sub div{height:34px;padding:0 16px;border-radius:17px;display:flex;align-items:center;color:var(--t2);background:var(--panel);border:1px solid var(--div);font-size:12.5px}
.sub .on{background:var(--field);color:var(--text);border-color:var(--border)}
.grid{left:246px;right:16px;top:60px;display:grid;grid-template-columns:1fr 1fr;gap:8px}
.cell{height:44px;border-radius:12px;background:var(--panel);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;position:relative;overflow:hidden;font-size:13px;font-weight:500}
.cell .fill{position:absolute;left:0;top:0;bottom:0;background:rgba(63,111,234,.20)}
.cell .knob{position:absolute;top:0;bottom:0;width:4px;background:var(--sel);box-shadow:0 0 8px rgba(110,155,255,.7)}
.cell span{z-index:2;text-shadow:0 0 6px #1B1E22,0 0 3px #1B1E22}
.cell span{position:relative}
.cell.btn{background:var(--field)}
.cell.wide{grid-column:1/3}
"""
def sl(label, val, pct):
    return f'<div class="cell"><div class="fill" style="width:{pct}%"></div><div class="knob" style="left:calc({pct}% - 3px)"></div><span>{label}: {val}</span></div>'
def bt(label, wide=False, extra=''):
    return f'<div class="cell btn{" wide" if wide else ""}" style="{extra}"><span>{label}</span></div>'
grid = ''.join([sl('Вращение', '150 %', 33), sl('Сдвиг', '100 %', 27), sl('Масштаб (щипок)', '100 %', 27), sl('Прокрутка списков', '100 %', 27),
                sl('Скорость курсора', '100 %', 100), bt('Порог касания: Обычный'), bt('Долгое нажатие: 0,5 с'), bt('Кончик курсора: 48 dp'),
                bt('Рука: правая'), bt('Блок инструментов: компактный'),
                f'<div class="cell btn" style="justify-content:flex-start;padding-left:16px;gap:10px;border-color:var(--accB)">{icon("ui-edit", 18, "#9db6ff")}<span>Настроить раскладку…</span></div>',
                f'<div class="cell btn" style="justify-content:flex-end;padding-right:16px;gap:10px">{icon("ui-trash", 16, "#F06A6E")}<span style="color:#F06A6E">Сбросить настройки</span></div>'])
pages['4-settings-minecraft'] = page(camera() + '<div class="abs sw"></div>' +
    f'''<div class="abs st"><h1>Настройки</h1><div class="tb">{icon('ui-settings', 18)}Общие</div><div class="tb on">{icon('ui-sliders', 18, '#9db6ff')}Управление</div>
<div class="tb">{icon('ui-display-style', 18)}Графика</div><div class="tb">{icon('library', 18, '#AEB4BD')}Модули</div>
<div style="margin-top:auto" class="tb">{icon('ui-check', 18, '#4CBB7F')}Готово</div></div>
<div class="abs sub"><div>Клавиатура</div><div>Мышь</div><div class="on">Сенсор</div><div>Жесты</div></div>
<div class="abs grid">{grid}</div>''' +
    note('Как в Minecraft: вкладки слева, настройки сеткой в 2 столбца. Ползунок — сама кнопка (тянешь по ней). Переключатель — тап листает варианты.', 'left:52px;top:268px;width:166px;font-size:10px;padding:4px 7px'),
    SET_CSS)



# 5. The first start: sensitivity tuned in place, then the layout (the author, 17.10).
WIZ_CSS = EDITOR_CSS + """
.sp{background:rgba(27,30,34,.97);border:1px solid var(--border);border-radius:14px;padding:12px 14px;z-index:30}
.sp h3{font-size:13px;font-weight:600;margin-bottom:2px}
.sp .s{margin-top:10px}
.sp .s .l{display:flex;justify-content:space-between;font-size:11.5px;color:var(--t2);margin-bottom:7px}
.sp .s .l b{color:var(--text);font-weight:500}
.step{font-size:11px;color:var(--mut);display:flex;gap:6px;align-items:center}
.step i{width:18px;height:4px;border-radius:2px;background:var(--border);display:block}
.step i.on{background:var(--acc)}
.next{height:40px;border-radius:12px;background:var(--acc);color:#fff;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:500;z-index:31;padding:0 18px}
.wt{z-index:31}
"""
def sld(label, val, pct):
    return f'<div class="s"><div class="l">{label}<b>{val}</b></div><div class="track"><b style="width:{pct}%"></b><u style="left:calc({pct}% - 8px)"></u></div></div>'
wtabs = lambda on: f'''<div class="abs isl wt" style="left:{{L}}px;top:8px"><div class="t{" on" if on == 0 else ""}">Рабочий слой</div><div class="t{" on" if on == 1 else ""}">Навигация</div></div>'''
steps = lambda k: '<div class="step">' + ''.join(f'<i class="{"on" if i <= k else ""}"></i>' for i in range(3)) + f'&nbsp;Шаг {k + 1} из 3</div>'

work_panel = f'''<div class="abs sp" style="left:48px;top:8px;bottom:8px;width:200px"><h3>Рабочий слой</h3>{steps(0)}
{sld('Вращение', '150 %', 33)}{sld('Сдвиг', '100 %', 27)}{sld('Масштаб (щипок)', '100 %', 27)}{sld('Скорость курсора', '100 %', 100)}{sld('Кончик над пальцем', '48 dp', 33)}</div>'''
big_view = f'''<div class="abs" style="left:40px;right:0;top:0;bottom:0;background:linear-gradient(#2A2E35,#17191D)">
<div class="abs" style="left:330px;top:95px;transform:scale(1.2);transform-origin:0 0">{MODEL}</div>
<svg class="abs" style="left:226px;bottom:16px" width="44" height="44" viewBox="0 0 44 44"><path d="M14 30V8" stroke="#4f7df5" stroke-width="2"/><path d="M14 30l20 8" stroke="#4CBB7F" stroke-width="2"/><path d="M14 30l-8 8" stroke="#F06A6E" stroke-width="2"/></svg></div>'''
pages['5a-first-start-work'] = page(camera() + big_view + work_panel + close +
    wtabs(0).replace('{L}', '420') +
    '<div class="abs tag" style="left:390px;top:60px">1 палец — вращение · 2 — сдвиг · щипок — масштаб</div>' +
    f'<div class="abs next" style="right:12px;bottom:12px">Дальше: навигация {icon("ui-chevron-right", 16, "#fff")}</div>' +
    note('Полноэкранное окно (как в PUBG / WoT Blitz). Ползунки слева, модель заранее<br>подготовлена — крутишь её и сразу подбираешь скорость, не бегая по настройкам.', 'left:280px;top:330px'),
    WIZ_CSS)

nav_panel = f'''<div class="abs sp" style="left:300px;top:96px;width:320px;max-height:260px;overflow:hidden">
<div style="display:flex;justify-content:space-between;align-items:center"><h3>Навигация</h3>{steps(1)}</div>
{sld('Прокрутка списков', '100 %', 27)}{sld('Инерция списков', 'средняя', 50)}{sld('Долгое нажатие', '0,5 с', 30)}{sld('Порог касания', '10 dp', 25)}
<div class="abs" style="left:0;right:0;bottom:0;height:26px;border-radius:0 0 14px 14px;background:linear-gradient(transparent,rgba(27,30,34,.97))"></div></div>'''
pages['5b-first-start-nav'] = page(camera() + block1() + view('', 'background:#17191D', False).replace('<div class="abs" style="left:110px;top:90px">' + MODEL + '</div>', '') +
    block2_list() + strip() + close + wtabs(1).replace('{L}', '330') + nav_panel +
    f'<div class="abs next" style="left:350px;bottom:52px">Дальше: раскладка {icon("ui-chevron-right", 16, "#fff")}</div>' +
    note('Ползунки переехали в центр, блоки и кнопки на месте и живые:<br>листаешь дерево и список инструментов, жмёшь — подбираешь<br>прокрутку и нажатия. Много ползунков — окно листается.', 'left:236px;top:8px;display:none'),
    WIZ_CSS)


# 6. The library on the phone: full screen, as the settings (PR #39 packs, 17.10).
LIB_CSS = SET_CSS + """
.fam{display:flex;gap:4px;background:var(--panel);border:1px solid var(--div);border-radius:10px;padding:3px;margin:0 2px 4px}
.fam div{flex:1;height:26px;border-radius:7px;display:flex;align-items:center;justify-content:center;font-size:11.5px;color:var(--t2)}
.fam .on{background:var(--field);color:var(--text)}
.st .tb{height:40px;font-size:12.5px}
.mid{left:254px;top:0;bottom:0;width:190px;border-right:1px solid var(--div);padding:14px 10px;display:flex;flex-direction:column;gap:6px}
.mid .v{font-size:10.5px;letter-spacing:.8px;color:var(--mut);padding:6px 6px 2px}
.mid .s{height:48px;border-radius:12px;background:var(--panel);border:1px solid var(--div);padding:6px 12px;display:flex;flex-direction:column;justify-content:center}
.mid .s small{color:var(--mut);font-size:10.5px}
.mid .s.on{background:var(--accSoft);border-color:var(--accB)}
.part{left:460px;right:16px;top:14px;bottom:0}
.part h2{font-size:15px;font-weight:600;line-height:1.25;padding-right:48px}
.lb{font-size:11px;color:var(--mut);margin:10px 0 6px}
.chips{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.chips div{height:34px;border-radius:9px;background:var(--panel);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;font-size:12px}
.chips .on{background:var(--accSoft);border-color:var(--accB);color:#9db6ff}
.fld{height:40px;border-radius:12px;background:var(--panel);border:1px solid var(--border);display:flex;align-items:center;padding:0 14px;gap:8px;font-size:13px}
.go{position:absolute;left:0;right:0;bottom:12px;height:44px;border-radius:12px;background:var(--acc);color:#fff;display:flex;align-items:center;justify-content:center;font-weight:600;gap:8px}
.desig{position:absolute;left:0;right:0;bottom:64px;font-size:11px;color:var(--t2);font-family:monospace}
.fade{left:0;right:0;height:28px;background:linear-gradient(transparent,var(--ribbon))}
.sw2{display:flex;gap:6px}
.sw2 i{width:24px;height:24px;border-radius:6px;display:block;border:1px solid var(--border)}
"""
rclose = f'<div class="abs" style="right:12px;top:10px;width:36px;height:36px;border-radius:10px;background:#E5484D;display:flex;align-items:center;justify-content:center">{icon("ui-close", 18, "#fff")}</div>'
folders = ['Болты', 'Гайки', 'Шайбы', 'Уголки', 'Двутавры', 'Профили', 'Трубы', 'Листы', 'Мои изделия']
fl = ''.join(f'<div class="tb{" on" if f == "Профили" else ""}">{icon("ui-folder", 16, "#9db6ff" if f == "Профили" else "#AEB4BD")}{f}</div>' for f in folders)
lib_left = f'''<div class="abs st" style="width:214px;gap:6px;overflow:hidden"><h1 style="margin-bottom:4px">Библиотека</h1>
<div class="fam"><div class="on">ГОСТ</div><div>ISO</div><div>DIN</div></div>{fl}
<div class="abs fade" style="bottom:56px"></div>
<div class="abs" style="left:12px;right:12px;bottom:0;height:68px;background:var(--ribbon)"></div>
<div class="abs" style="left:12px;right:12px;bottom:12px;height:40px;border-radius:12px;border:1px solid var(--border);background:var(--panel);display:flex;align-items:center;justify-content:center;gap:8px;font-size:12px;color:var(--t2)">{icon("ui-plus", 14)}Пакет / моя деталь</div></div>'''
lib_mid = '''<div class="abs mid"><div class="v">КВАДРАТНЫЕ ЗАМКНУТЫЕ</div><div class="s on">ГОСТ 30245-2003<small>длина — любая</small></div>
<div class="v">ПРЯМОУГОЛЬНЫЕ ЗАМКНУТЫЕ</div><div class="s">ГОСТ 30245-2003<small>длина — любая</small></div></div>'''
sizes = ['40×40×2', '40×40×2,5', '40×40×3', '40×40×3,5', '50×50×2', '50×50×3', '50×50×4', '60×60×3']
chips = ''.join(f'<div class="{"on" if z == "40×40×3" else ""}">{z}</div>' for z in sizes)
lib_part = f'''<div class="abs part"><h2>Профиль гнутый замкнутый сварной квадратный ГОСТ 30245-2003</h2>
<div class="lb" style="display:flex;justify-content:space-between;align-items:center">Сечение a×a×t, мм <span class="search" style="flex:none;width:170px;height:28px">{icon('ui-search', 13)}Найти размер</span></div>
<div class="chips">{chips}</div>
<div style="display:flex;gap:10px;align-items:flex-end"><div style="flex:1"><div class="lb">Длина, мм (1–13000)</div><div class="fld">1000<span style="margin-left:auto;color:var(--mut);font-size:11px">нампад</span></div></div>
<div><div class="lb">Вид</div><div class="sw2" style="height:40px;align-items:center"><i style="background:#C8CDD4;border-color:var(--sel)"></i><i style="background:#8E959F"></i><i style="background:#2A2F38"></i><i style="background:#C87A3F"></i></div></div></div>
<div class="desig">Профиль 40×40×3 ГОСТ 30245-2003, L=1000</div>
<div class="go">{icon('ui-check', 18, '#fff')}Создать деталь</div></div>'''
pages['6a-library'] = page(camera() + '<div class="abs sw"></div>' + lib_left + lib_mid + lib_part + rclose +
    note('Как настройки: во весь экран, три столбца — папки · стандарты · изделие.<br>Размеры — плитками (листаются); «Найти размер» — системная клавиатура;<br>длина — нампадом. В сборке кнопка — «Вставить в сборку».', 'left:260px;top:248px;width:186px;font-size:10px;padding:4px 7px'),
    LIB_CSS)

# 6b. "В мою библиотеку" on the phone: the open part, its variables as a table.
SAVE_CSS = LIB_CSS + """
.vars{display:flex;flex-wrap:wrap;gap:6px}
.vars div{height:34px;padding:0 14px;border-radius:17px;display:flex;align-items:center;gap:6px;background:var(--panel);border:1px solid var(--border);font-size:12.5px}
.vars .on{background:var(--accSoft);border-color:var(--accB);color:#9db6ff}
.tbl{display:grid;grid-template-columns:repeat(3,1fr) 34px;gap:6px;align-items:center}
.tbl .h{font-size:11px;color:var(--mut);padding-left:4px}
.tbl .c{height:36px;border-radius:10px;background:var(--panel);border:1px solid var(--border);display:flex;align-items:center;padding:0 12px;font-size:13px}
.tbl .c.on{border-color:var(--sel)}
.tbl .x{width:34px;height:34px;display:flex;align-items:center;justify-content:center}
"""
chk = icon('ui-check', 13, '#9db6ff')
save_left = f'''<div class="abs st" style="width:300px;gap:2px"><h1>В мою библиотеку</h1>
<div class="lb">Название изделия</div><div class="fld">Стойка</div>
<div class="lb">Папка</div><div class="fld">Мои изделия</div>
<div class="lb">Переменные детали → столбцы (L — длина, любая)</div>
<div class="vars"><div class="on">{chk}a</div><div class="on">{chk}t</div><div class="on">{chk}R</div><div class="on">{chk}L</div><div>k</div></div></div>'''
rows = [('40', '2', '4'), ('60', '3', '6'), ('80', '4', '8')]
tb = '<div class="h">a</div><div class="h">t</div><div class="h">R</div><div></div>' + ''.join(
    ''.join(f'<div class="c{" on" if (r, k) == (1, 0) else ""}">{v}</div>' for k, v in enumerate(row)) + f'<div class="x">{icon("ui-close", 14)}</div>'
    for r, row in enumerate(rows))
save_right = f'''<div class="abs part" style="left:356px"><h2>Размеры</h2><div class="lb">Первая строка — деталь как есть. Тап по ячейке — нампад.</div>
<div class="tbl">{tb}</div>
<div style="margin-top:8px;height:36px;border-radius:10px;border:1px dashed var(--border);display:flex;align-items:center;justify-content:center;gap:6px;color:#9db6ff;font-size:12.5px">{icon('ui-plus', 13, '#9db6ff')}Добавить размер</div>
<div class="go">{icon('ui-check', 18, '#fff')}Сохранить в библиотеку</div></div>'''
pages['6b-save-to-library'] = page(camera() + '<div class="abs sw"></div>' + save_left + save_right + rclose, SAVE_CSS)
# 6. The strip over the view — three looks to choose from (17.10, after "the strip looks bad").
# Shared: opened from СОЗДАТЬ's "›", ends at the view's edge (never over block 1), scrolls sideways.
STRIP_CSS = COMPACT_CSS + """
.fa{left:234px;right:188px;top:112px;height:86px;border-radius:16px;background:rgba(29,32,37,.97);border:1px solid var(--border);
    box-shadow:0 10px 28px rgba(0,0,0,.45);display:flex;align-items:center;gap:4px;padding:0 10px;overflow:hidden;z-index:20}
.fa .ti{width:76px;height:68px;border-radius:12px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;font-size:10.5px;color:var(--t2);flex:none}
.fa .ti.on{background:var(--accSoft);color:#cfdcff;outline:1px solid var(--accB)}
.fa .h{position:absolute;left:14px;top:-1px;font-size:9.5px;letter-spacing:.8px;color:var(--mut)}
.tail{width:14px;height:14px;background:rgba(29,32,37,.97);border-right:1px solid var(--border);border-top:1px solid var(--border);transform:rotate(45deg);z-index:21}
.fadeL,.fadeR{position:absolute;top:0;bottom:0;width:44px;pointer-events:none}
.fadeR{right:0;background:linear-gradient(90deg,transparent,rgba(29,32,37,.98))}
.fadeL{left:0;background:linear-gradient(270deg,transparent,rgba(29,32,37,.98))}
.pills{left:236px;right:190px;top:118px;height:70px;display:flex;gap:8px;overflow:hidden;z-index:20;align-items:center}
.pills .p{flex:none;height:62px;width:74px;border-radius:31px/22px;background:rgba(35,39,45,.94);border:1px solid var(--border);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;font-size:10px;color:var(--t2);box-shadow:0 6px 16px rgba(0,0,0,.45)}
.pills .p.on{background:var(--acc);border-color:var(--acc);color:#fff}
.dots{left:0;right:0;display:flex;justify-content:center;gap:5px;z-index:21}
.dots i{width:6px;height:6px;border-radius:3px;background:#4a505a}.dots i.on{width:16px;background:var(--sel)}
.dr{right:180px;top:96px;height:112px;left:226px;background:var(--panel);border:1px solid var(--div);border-right:none;border-radius:14px 0 0 14px;z-index:20;box-shadow:-10px 8px 24px rgba(0,0,0,.4)}
.dr .hd{height:30px;display:flex;align-items:center;padding:0 10px 0 14px;font-size:10.5px;letter-spacing:.8px;color:var(--mut);border-bottom:1px solid var(--div)}
.dr .rw{display:flex;gap:4px;padding:6px 8px;overflow:hidden;position:relative}
.dr .ti{width:74px;height:66px;border-radius:10px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;font-size:10.5px;color:var(--t2);flex:none;background:var(--field)}
.dr .ti.on{background:var(--accSoft);color:#cfdcff}
.b2c-open .cg:nth-child(3){background:var(--panel)}
"""
def b2c_with(extra=''):
    return f'''<div class="abs b2" style="{extra}"><div class="brow" style="gap:8px;padding:0 10px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">60 fps</span><div class="search">{icon('ui-search', 14)}Поиск</div></div>
{cgroup('ЭСКИЗ', ['sketch', 'sk-project', 'work-plane'])}{cgroup('СОЗДАТЬ', ['extrude', 'revolve', 'hole'], True)}{cgroup('ИЗМЕНИТЬ', ['fillet', 'chamfer', 'shell'])}{cgroup('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])}
<div class="abs beard">Готово<div style="flex:1;height:3px;border-radius:2px;background:var(--field)"></div></div></div>'''
items6 = [('extrude', 'Выдавить'), ('revolve', 'Вращение'), ('hole', 'Отверстие'), ('loft', 'По сечениям'), ('sweep', 'Сдвиг'), ('coil', 'Пружина'), ('thread', 'Резьба')]

# A. Tray: a floating rounded card with a little tail pointing at the "›" that opened it; fades at the ends.
tray = '<div class="abs fa"><div class="fadeL"></div>' + ''.join(f'<div class="ti{" on" if n == "extrude" else ""}">{icon(n, 28, "#fff")}{t}</div>' for n, t in items6) + '<div class="fadeR"></div></div>' + \
       '<div class="abs tail" style="right:182px;top:159px"></div>'
pages['6a-strip-tray'] = page(camera() + block1() + view() + b2c_with() + strip() + tray +
    note('<b>А. «Лоток»</b> — карточка со скруглением и тенью, «хвостик» указывает на стрелку,<br>из которой выехала. Края гаснут — видно, что листается.<br>Не доходит до блока 1 (поля 8 dp).', 'left:240px;top:262px'), STRIP_CSS)

# B. Pills: no common plate — separate floating buttons, the model shows between them; a page indicator under them.
pills = '<div class="abs pills">' + ''.join(f'<div class="p{" on" if n == "extrude" else ""}">{icon(n, 24, "#fff")}{t}</div>' for n, t in items6) + '</div>' + \
        '<div class="abs dots" style="left:226px;right:180px;top:194px"><i class="on"></i><i></i></div>'
pages['6b-strip-pills'] = page(camera() + block1() + view() + b2c_with() + strip() + pills +
    note('<b>Б. «Пилюли»</b> — без общей подложки: каждая кнопка<br>парит над моделью с тенью, модель видна сильнее всего.<br> Под ними — точки прокрутки.', 'left:240px;top:262px'), STRIP_CSS)

# C. Drawer: the strip is block 2 pulled out over the view — same colour, no gap, the group's name and ✕.
drawer = f'''<div class="abs dr"><div class="hd">СОЗДАТЬ<span style="margin-left:auto">{icon('ui-close', 14)}</span></div>
<div class="rw">''' + ''.join(f'<div class="ti{" on" if n == "extrude" else ""}">{icon(n, 26, "#fff")}{t}</div>' for n, t in items6) + \
         '<div class="fadeR" style="background:linear-gradient(90deg,transparent,var(--panel))"></div></div></div>'
pages['6c-strip-drawer'] = page(camera() + block1() + view() + b2c_with() + strip() + drawer +
    note('<b>В. «Ящик»</b> — полоса выдвигается из блока 2 как его продолжение:<br>тот же цвет, без зазора, сверху имя группы и ✕.<br> Читается как одна панель.', 'left:240px;top:262px'), STRIP_CSS)


# 7. The author's redesign 18.10: compact grids, the tool's options in the top island, the action row at the very
# bottom, save/open/ruler in the hotbar, the mode and the tool's title in block 2's bottom line, a green ✓.
R7_CSS = """
.b1 .tabs{padding:6px 6px 0;gap:4px}
.b1 .tab{padding:0 8px;gap:5px;font-size:12px}
.g7{padding:0 8px}
.g7 .h{font-size:9.5px;letter-spacing:.8px;color:var(--mut);padding:7px 2px 3px}
.g7 .r{display:grid;grid-template-columns:repeat(4,1fr);gap:4px}
.g7 .r div{height:36px;border-radius:8px;background:var(--field);display:flex;align-items:center;justify-content:center}
.g7 .r .on{background:var(--acc)}
.fin{margin:0 8px 2px;height:34px;border-radius:9px;background:var(--accSoft);border:1px solid var(--accB);display:flex;align-items:center;gap:8px;padding:0 10px;color:#9db6ff;font-weight:500;font-size:12px}
.st{left:0;right:0;bottom:0;height:28px;display:flex;white-space:nowrap;overflow:hidden;align-items:center;gap:6px;padding:0 10px;font-size:11px;color:var(--text);border-top:1px solid var(--div);background:var(--panel)}
.st i{font-style:normal;color:var(--mut)}
.row7{left:0;right:0;bottom:0;height:52px;display:flex;gap:6px;padding:6px 8px;border-top:1px solid var(--div);background:var(--panel)}
.row7 .rb{border-radius:10px}
.row7 .on{background:var(--accSoft);outline:1px solid var(--accB)}
.row7 .go{background:#2E9E64;box-shadow:0 0 0 3px rgba(46,158,100,.28)}
.ti7{left:84px;right:56px;top:8px;height:40px;padding:0 4px;gap:3px;overflow:hidden}
.ti7 .m{height:30px;min-width:32px;border-radius:8px;display:flex;align-items:center;justify-content:center;gap:5px;padding:0 7px;color:var(--t2);font-size:11.5px;flex:none}
.ti7 .m.on{background:var(--accSoft);color:#cfdcff;outline:1px solid var(--accB)}
.ti7 .sep{width:1px;height:22px;background:var(--border);margin:0 3px;flex:none}
.ti7 .sb{position:absolute;left:10px;right:10px;bottom:2px;height:2px;border-radius:1px;background:#2a2e35}
.ti7 .sb b{position:absolute;left:0;width:42%;top:0;bottom:0;border-radius:1px;background:#6E9BFF}
.fadeR7{position:absolute;right:0;top:0;bottom:0;width:36px;background:linear-gradient(90deg,transparent,rgba(27,30,34,.98))}
.fl{color:var(--mut);font-size:10.5px;padding:8px 12px 4px}
.fld{margin:0 10px;height:32px;border-radius:8px;background:var(--field);border:1px solid var(--border);display:flex;align-items:center;padding:0 10px;gap:6px}
.chipx{margin:3px 10px 0;height:30px;border-radius:8px;background:var(--accSoft);border:1px solid var(--accB);display:flex;align-items:center;padding:0 8px 0 10px;color:#cfdcff;font-size:11.5px}
.vscroll{right:3px;width:3px;border-radius:2px;background:#3a3f47}
"""


def hotbar7():
    hb = lambda n, c='#AEB4BD': f'<div class="hb">{icon(n, 18, c)}</div>'
    return (f'<div class="abs cam">{icon("brand-mark", 22)}{hb("ui-save")}{hb("ui-open")}{hb("ui-ruler", "#6E9BFF")}'
            '<div style="width:20px;height:1px;background:#2a2d33;margin-top:8px"></div>'
            f'<div style="margin-top:52px">{hb("ui-plus")}</div>{hb("section")}<div style="margin-top:auto">{hb("ui-eye-off")}</div></div>'
            '<div class="abs cutout"></div>')


def g7(title, names, on=()):
    cells = ''.join(f'<div class="{"on" if n in on else ""}">{icon(n, 20, "#fff")}</div>' for n in names)
    return f'<div class="g7"><div class="h">{title}</div><div class="r">{cells}</div></div>'


def tabs7():
    return (f'<div class="tabs"><div class="tab">{icon("ui-tree", 16)}</div>'
            f'<div class="tab on" style="flex:1">{icon("ui-wrench", 16, "#E6E8EB")}Инструмент</div><div class="tab">{icon("ui-dimension", 16)}</div></div>')


def row7(ok='', cursor=True):
    okc = 'rb go' if ok == 'go' else 'rb ok'
    cur = ' on' if cursor else ''
    return (f'<div class="abs row7"><div class="rb">{icon("ui-frame")}</div><div class="rb{cur}">{icon("ui-cursor", 18, "#9db6ff" if cursor else "#AEB4BD")}</div>'
            f'<div class="rb">{icon("ui-close")}</div><div class="{okc}">{icon("ui-check", 20, "#fff")}</div></div>')


def status7(ic, tool, mode, right=''):
    return f'<div class="abs st">{icon(ic, 14, "#9db6ff")}<span>{tool}</span><i>· {mode}</i><span style="margin-left:auto;color:var(--mut)">{right}</span></div>'


def islands7(tool_island):
    return (f'<div class="abs isl" style="left:8px;top:8px">{icon("ui-undo")}<span style="width:6px"></span>{icon("ui-redo")}</div>'
            f'{tool_island}<div class="abs isl" style="right:8px;top:8px;width:40px;justify-content:center">{icon("ui-cube", 20)}</div>')


GRID = ('<div class="abs" style="inset:0;background-image:linear-gradient(#2c3037 1px,transparent 1px),'
        'linear-gradient(90deg,#2c3037 1px,transparent 1px);background-size:28px 28px;background-color:#1e2126"></div>')
AXES = ('<div class="abs" style="left:0;right:0;top:196px;height:1px;background:#8a4447"></div>'
        '<div class="abs" style="top:0;bottom:0;left:254px;width:1px;background:#3f7a57"></div>')
DROP = ('<svg class="abs" style="left:340px;top:192px" width="60" height="110" viewBox="0 0 60 110">'
        '<path d="M30 4 L42 58 A16 16 0 1 1 18 58 Z" fill="rgba(110,155,255,.10)" stroke="#6E9BFF" stroke-width="1.6"/>'
        '<circle cx="30" cy="4" r="3" fill="#fff"/><circle cx="30" cy="72" r="22" fill="none" stroke="#6E9BFF" stroke-dasharray="3 4" opacity=".7"/></svg>')
AXIS3 = ('<svg class="abs" style="left:18px;bottom:16px" width="44" height="44" viewBox="0 0 44 44"><path d="M14 30V8" stroke="#4f7df5" stroke-width="2"/>'
         '<path d="M14 30l20 8" stroke="#4CBB7F" stroke-width="2"/><path d="M14 30l-8 8" stroke="#F06A6E" stroke-width="2"/></svg>')

# 7a. Sketch, the rectangle tool.
b1_7a = (f'<div class="abs b1">{tabs7()}<div class="fl">Подсказка</div>'
         '<div style="padding:0 12px;color:var(--t2);line-height:1.45;font-size:12px">Укажите первый угол.<br>Размер — числом на нампаде.</div>'
         f'{row7()}</div>')
b2_7a = ('<div class="abs b2"><div class="brow" style="height:38px;gap:8px;padding:0 8px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">107</span>'
         f'<div class="search">{icon("ui-search", 14)}Поиск</div></div>'
         f'<div class="fin">{icon("ui-check", 16, "#9db6ff")}Завершить эскиз</div>'
         + g7('РАЗМЕРЫ', ['sk-dimension', 'dim-arrange'])
         + g7('СОЗДАТЬ', ['sk-line', 'sk-circle', 'sk-arc', 'sk-rectangle', 'sk-point', 'sk-spline', 'sk-text'], ('sk-rectangle',))
         + g7('ЗАВИСИМОСТИ', ['c-coincident', 'c-horizontal', 'c-vertical', 'c-fix', 'c-parallel', 'c-perpendicular', 'c-equal',
                              'c-midpoint', 'c-tangent', 'c-concentric', 'c-symmetric', 'c-show-all'])
         + g7('ИЗМЕНИТЬ', ['sk-trim', 'sk-extend', 'sk-offset', 'sk-fillet'])
         + '<div class="abs vscroll" style="top:96px;height:120px"></div>'
         + status7('sk-rectangle', 'Прямоугольник', 'Эскиз') + '</div>')
ti_7a = (f'<div class="abs isl ti7"><div class="m on">{icon("sk-rectangle", 18, "#cfdcff")}2 точки</div><div class="m">{icon("sk-rect-centre", 18)}</div>'
         f'<div class="m">{icon("sk-rect-3pt", 18)}</div><div class="m">{icon("sk-rect-3pt-centre", 18)}</div><div class="sep"></div>'
         f'<div class="m on">{icon("sk-line", 18, "#cfdcff")}Основная</div><div class="m">{icon("sk-construction", 18)}</div><div class="m">{icon("sk-centerline", 18)}</div></div>')
view_7a = (f'<div class="abs view" style="background:none">{GRID}{AXES}'
           '<div class="abs" style="left:170px;top:116px;width:200px;height:80px;border:1.6px dashed #6E9BFF;background:rgba(110,155,255,.06)"></div>'
           f'<div class="abs" style="left:166px;top:112px;width:9px;height:9px;border-radius:50%;background:#fff"></div>{DROP}'
           f'{islands7(ti_7a)}</div>')
pages['7a-sketch-tool'] = page(hotbar7() + b1_7a + view_7a + b2_7a + strip()
    + note('<b>Верхний островок</b> — настройки инструмента вместо «3D-модель / Эскиз / Вид»:<br>способ построения · тип линии. Не влезает — листается вбок.', 'left:240px;top:58px')
    + note('Сохранить, открыть, линейка — в хотбаре.<br>Рамка · курсор · ✕ · ✓ — в самом низу блока 1.', 'left:50px;top:150px;width:170px')
    + note('Инструмент и режим — строкой<br>внизу блока 2.', 'right:188px;bottom:48px'), R7_CSS)

# 7b. 3D, the extrude: everything needed is chosen -> ✓ is green and applies (the tool stays open).
b1_7b = (f'<div class="abs b1">{tabs7()}'
         f'<div class="fl">Профиль · 2</div><div class="chipx">Контур 1<span style="margin-left:auto">{icon("ui-close", 13, "#9db6ff")}</span></div>'
         f'<div class="chipx">Контур 2<span style="margin-left:auto">{icon("ui-close", 13, "#9db6ff")}</span></div>'
         f'<div class="fl">Расстояние</div><div class="fld">20 мм<span style="margin-left:auto">{icon("ui-fx", 14)}</span></div>'
         '<div class="fl">Уклон</div><div class="fld">0°</div>'
         f'{row7("go", cursor=False)}</div>')
b2_7b = ('<div class="abs b2"><div class="brow" style="height:38px;gap:8px;padding:0 8px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">72</span>'
         f'<div class="search">{icon("ui-search", 14)}Поиск</div></div>'
         + g7('ЭСКИЗ И РАБ. ЭЛЕМЕНТЫ', ['sketch', 'work-plane', 'work-axis', 'work-point'])
         + g7('СОЗДАТЬ', ['extrude', 'revolve', 'hole', 'loft', 'sweep', 'coil', 'thread'], ('extrude',))
         + g7('ИЗМЕНИТЬ', ['fillet', 'chamfer', 'shell'])
         + g7('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])
         + status7('extrude', 'Выдавливание', '3D-модель') + '</div>')
ti_7b = ('<div class="abs isl ti7"><div class="m on">Добавить</div><div class="m">Новое тело</div><div class="m">Вырезать</div><div class="m">Пересечь</div>'
         '<div class="sep"></div><div class="m on">Прямое</div><div class="m">Обратное</div><div class="m">В обе стороны</div><div class="sep"></div>'
         '<div class="m on">На расстояние</div><div class="fadeR7"></div><div class="sb"><b></b></div></div>')
PREVIEW = ('<svg class="abs" style="left:110px;top:50px" width="300" height="200" viewBox="0 0 300 200">'
           '<path d="M95 92l45-22 45 22v-40l-45-22-45 22z" fill="rgba(110,155,255,.25)" stroke="#6E9BFF" stroke-width="1.4"/>'
           '<path d="M95 52l45 22 45-22" fill="none" stroke="#6E9BFF" stroke-width="1.2"/></svg>')
view_7b = f'<div class="abs view"><div class="abs" style="left:110px;top:90px">{MODEL}</div>{PREVIEW}{AXIS3}{islands7(ti_7b)}</div>'
pages['7b-extrude-ready'] = page(hotbar7() + b1_7b + view_7b + b2_7b + strip()
    + note('<b>✓ зелёная</b> — всё нужное выбрано: тап применяет, инструмент остаётся открыт.<br>'
           '<b>✕</b> снимает выбор по одному с конца (Контур 2, затем Контур 1),<br>когда снимать нечего — закрывает инструмент.', 'left:240px;top:290px')
    + note('Не влезло — край гаснет, полоска снизу,<br>листается вбок.', 'left:420px;top:56px'), R7_CSS)


# 7c. The extrude on the phone with icon buttons in the top island (the author 18.10: "нужны маленькие картинки").
OPS = [('op-join', 'Добавить к телу'), ('op-cut', 'Вырезать из тела'), ('op-intersect', 'Оставить пересечение'), ('op-new', 'Новое тело')]
DIRS = [('dir-up', 'Вверх (по нормали эскиза)'), ('dir-down', 'Вниз (против нормали)'), ('dir-both', 'В обе стороны поровну'),
        ('dir-asym', 'В обе стороны, разные расстояния')]
DEPTH = [('ext-distance', 'На расстояние'), ('ext-to', 'До плоскости или грани'), ('ext-through', 'Насквозь')]
R7C_CSS = R7_CSS + """
.ti7 .m.ic{padding:0;width:30px;min-width:30px}
.ti7 .sep{margin:0 2px}
.tip{background:#0f1113;border:1px solid var(--border);border-radius:8px;padding:5px 9px;font-size:11.5px;color:var(--text);z-index:60;box-shadow:0 6px 16px rgba(0,0,0,.5)}
.tip:after{content:'';position:absolute;left:50%;top:-6px;margin-left:-5px;border:5px solid transparent;border-top:none;border-bottom-color:#0f1113}
"""


def icon_group(items, on):
    return ''.join(f'<div class="m ic{" on" if n == on else ""}">{icon(n, 22)}</div>' for n, _ in items)


ti_7c = ('<div class="abs isl ti7" style="padding:0 3px;gap:1px">' + icon_group(OPS, 'op-join') + '<div class="sep"></div>'
         + icon_group(DIRS, 'dir-up') + '<div class="sep"></div>' + icon_group(DEPTH, 'ext-distance') + '</div>')
view_7c = f'<div class="abs view"><div class="abs" style="left:110px;top:90px">{MODEL}</div>{PREVIEW}{AXIS3}{islands7(ti_7c)}</div>'
pages['7c-extrude-icons'] = page(hotbar7() + b1_7b + view_7c + b2_7b + strip()
    + '<div class="abs tip" style="left:614px;top:62px">Насквозь</div>'
    + note('Островок: <b>операция</b> (добавить · вырезать · пересечение · новое тело) │ <b>направление</b><br>'
           '(вверх · вниз · поровну в обе · по-разному в обе) │ <b>глубина</b> (расстояние · до грани · насквозь).<br>'
           'Помещается целиком. Зажатие кнопки — подсказка с названием.', 'left:240px;top:290px'), R7C_CSS)

# 7a with the line types as an island of their own (the author 18.10).
LT_ISL = (f'<div class="abs isl ti7" style="left:auto;right:56px;width:auto;padding:0 3px;gap:2px"><div class="m ic on">{icon("sk-line", 20, "#cfdcff")}</div>'
          f'<div class="m ic">{icon("sk-construction", 20)}</div><div class="m ic">{icon("sk-centerline", 20)}</div></div>')
ti_7a2 = (f'<div class="abs isl ti7" style="right:auto;padding:0 3px;gap:2px"><div class="m on">{icon("sk-rectangle", 18, "#cfdcff")}2 точки</div>'
          f'<div class="m ic">{icon("sk-rect-centre", 18)}</div><div class="m ic">{icon("sk-rect-3pt", 18)}</div><div class="m ic">{icon("sk-rect-3pt-centre", 18)}</div></div>'
          + LT_ISL)
view_7a2 = (f'<div class="abs view" style="background:none">{GRID}{AXES}'
            '<div class="abs" style="left:170px;top:116px;width:200px;height:80px;border:1.6px dashed #6E9BFF;background:rgba(110,155,255,.06)"></div>'
            f'<div class="abs" style="left:166px;top:112px;width:9px;height:9px;border-radius:50%;background:#fff"></div>{DROP}'
            f'{islands7(ti_7a2)}</div>')
pages['7a-sketch-tool'] = page(hotbar7() + b1_7a + view_7a2 + b2_7a + strip()
    + note('Два островка: <b>способ построения</b> и отдельно <b>тип линии</b><br>(основная · вспомогательная · осевая).', 'left:240px;top:58px')
    + note('Сохранить, открыть, линейка — в хотбаре.<br>Рамка · курсор · ✕ · ✓ — в самом низу блока 1.', 'left:50px;top:150px;width:170px')
    + note('Инструмент и режим — строкой<br>внизу блока 2.', 'right:188px;bottom:48px'), R7C_CSS + '.ti7 .m.ic{padding:0;width:32px;min-width:32px}')

# 8a. The desktop: the same icon buttons in the tool panel, rows like KOMPAS-3D (caption on the left, buttons on the right).
SIZES['8a-desktop-extrude'] = (1060, 640)
D_CSS = """
body{width:1060px;height:640px;background:#EEF0F3;color:#1F2328;font-size:12.5px}
.rail{left:0;top:0;bottom:0;width:52px;background:#F4F5F7;border-right:1px solid #E1E4E8;display:flex;flex-direction:column;align-items:center;gap:22px;padding-top:18px}
.pn{left:52px;top:0;bottom:0;width:330px;background:#FBFBFC;border-right:1px solid #E1E4E8}
.pn .hd{height:52px;display:flex;align-items:center;gap:10px;padding:0 16px;font-weight:600;border-bottom:1px solid #E8EBEE}
.pn .sec{font-size:11.5px;color:#6B7280;padding:14px 16px 6px}
.pn .chip{margin:0 16px 6px;height:34px;border-radius:8px;border:1.5px solid #3F6FEA;background:#EEF3FF;display:flex;align-items:center;padding:0 12px;gap:8px;font-weight:500;color:#1F3A8A;font-size:12.5px}
.pn .rw{display:flex;align-items:center;padding:5px 16px}
.pn .rw .lb{width:92px;color:#4B5563}
.pn .bt{width:34px;height:34px;border-radius:8px;border:1px solid #D7DBE0;background:#fff;display:flex;align-items:center;justify-content:center;margin-right:4px}
.pn .bt.on{background:#E6EDFF;border:1.5px solid #3F6FEA}
.pn .fld{margin:0 16px;height:36px;border-radius:8px;border:1px solid #D7DBE0;background:#fff;display:flex;align-items:center;padding:0 10px;gap:6px}
.pn .ft{position:absolute;left:0;right:0;bottom:0;height:64px;border-top:1px solid #E8EBEE;display:flex;gap:8px;padding:14px 16px}
.pn .ft div{flex:1;border-radius:8px;border:1px solid #D7DBE0;background:#fff;display:flex;align-items:center;justify-content:center;font-weight:500}
.pn .ft .go{background:#1D4ED8;border-color:#1D4ED8;color:#fff}
.dv{left:382px;right:0;top:0;bottom:0;background:linear-gradient(#F7F8FA,#E6E9ED)}
.dtip{background:#1F2328;color:#fff;border-radius:6px;padding:5px 9px;font-size:11.5px;z-index:60}
.note{color:#7A4B00;background:#FFF6DD;border-color:#E2B33C}
"""


def drow(label, items, on, hover=None):
    hv = ' style="border-color:#9AA8C2;background:#F3F5F8"'
    bts = ''.join(f'<div class="bt{" on" if n == on else ""}"{hv if n == hover else ""}>{icon(n, 22, dark=False)}</div>' for n, _ in items)
    return f'<div class="rw"><span class="lb">{label}</span>{bts}</div>'


pn = (f'<div class="abs pn"><div class="hd">{icon("extrude", 22, dark=False)}Выдавливание1<span style="margin-left:auto">{icon("ui-close", 16, "#6B7280")}</span></div>'
      f'<div class="sec">Профиль · 2</div><div class="chip">{icon("ui-check", 14, "#3F6FEA")}Контур 1<span style="margin-left:auto">{icon("ui-close", 13, "#3F6FEA")}</span></div>'
      f'<div class="chip">{icon("ui-check", 14, "#3F6FEA")}Контур 2<span style="margin-left:auto">{icon("ui-close", 13, "#3F6FEA")}</span></div>'
      '<div style="height:8px"></div>'
      + drow('Операция', OPS, 'op-join') + drow('Направление', DIRS, 'dir-up') + drow('Глубина', DEPTH, 'ext-distance', 'ext-through')
      + f'<div class="sec">Расстояние</div><div class="fld">20<span style="margin-left:auto;color:#9AA1AB">мм</span>{icon("ui-fx", 14, "#6B7280")}</div>'
      + '<div class="sec">Уклон</div><div class="fld">0<span style="margin-left:auto;color:#9AA1AB">°</span></div>'
      + '<div class="ft"><div>Отмена</div><div>Применить</div><div class="go">Готово</div></div></div>')
rail = f'<div class="abs rail">{icon("ui-tree", 20, "#4B5563")}{icon("ui-sliders", 20, "#3F6FEA")}{icon("ui-fx", 20, "#4B5563")}</div>'
dview = f'<div class="abs dv"><div class="abs" style="left:170px;top:170px;transform:scale(1.5);transform-origin:0 0">{MODEL}{PREVIEW.replace("left:110px;top:50px", "left:0;top:-40px")}</div></div>'
pages['8a-desktop-extrude'] = page(rail + pn + dview
    + '<div class="abs dtip" style="left:266px;top:296px">Насквозь</div>'
    + note('ПК: те же иконки в панели инструмента, строками как в КОМПАС-3D —<br>подпись слева, кнопки справа. Наведение — подсказка с названием.<br>'
           'Текстовые полосы «Прямое / Обратное / В обе стороны» уходят.', 'left:420px;top:24px'), D_CSS)


# 9. Appearance on the phone (the author 18.10): the top island chooses Тело / Грань; block 1 has three buttons —
# the library, my colours, create a colour; lists open over block 1 with ✕ in the top right corner; ✓ / ✕ at the bottom.
P9_CSS = R7C_CSS + """
.sw{border-radius:50%;flex:none;border:1px solid rgba(255,255,255,.18)}
.big3{margin:4px 10px 0;height:40px;border-radius:10px;background:var(--field);display:flex;align-items:center;gap:10px;padding:0 12px;font-size:12.5px}
.cur{margin:4px 10px 2px;display:flex;align-items:center;gap:10px;padding:6px 8px;border-radius:10px;background:#20242a}
.cur small{display:block;color:var(--mut);font-size:10.5px}
.recent{display:flex;gap:8px;padding:2px 12px}
.pop{left:40px;top:0;width:186px;bottom:52px;background:#20242a;border-right:1px solid var(--div);z-index:40;box-shadow:8px 0 22px rgba(0,0,0,.45)}
.pop .ph{height:42px;display:flex;align-items:center;padding:0 6px 0 12px;font-weight:600;border-bottom:1px solid var(--div)}
.pop .ph .x{margin-left:auto;width:32px;height:32px;border-radius:9px;background:var(--field);display:flex;align-items:center;justify-content:center}
.pop .gh{font-size:9.5px;letter-spacing:.8px;color:var(--mut);padding:8px 12px 3px}
.pop .li{height:34px;display:flex;align-items:center;gap:9px;padding:0 10px 0 12px;font-size:12px;white-space:nowrap;overflow:hidden}
.pop .li.on{background:var(--accSoft);color:#cfdcff}
.seg2{margin:0 10px;display:flex;gap:4px}
.seg2 div{flex:1;height:30px;border-radius:8px;background:var(--field);display:flex;align-items:center;justify-content:center;gap:6px;font-size:11.5px;color:var(--t2)}
.seg2 .on{background:var(--accSoft);outline:1px solid var(--accB);color:#cfdcff}
.sv{margin:0 10px;height:92px;border-radius:8px;background:linear-gradient(transparent,#000),linear-gradient(90deg,#fff,#CDA955);position:relative}
.hue{margin:6px 10px 0;height:12px;border-radius:6px;background:linear-gradient(90deg,red,#ff0,lime,cyan,blue,#f0f,red);position:relative}
.knob{position:absolute;width:14px;height:14px;border-radius:50%;border:2px solid #fff;box-shadow:0 0 0 1px #000}
"""


def swatch(hexc, finish='matte', size=24, alpha=1.0):
    c = '#' + hexc
    if finish == 'metal':
        bg = f'radial-gradient(circle at 32% 28%,#fff 0,{c} 45%,#20242a 130%)'
    elif finish == 'glass':
        bg = f'radial-gradient(circle at 32% 28%,rgba(255,255,255,.9) 0,rgba(180,200,220,{alpha}) 50%,rgba(40,50,60,.6) 120%)'
    else:
        bg = c
    return f'<div class="sw" style="width:{size}px;height:{size}px;background:{bg}"></div>'


LOOKS = [('МЕТАЛЛЫ', [('B9C0C8', 'Сталь', 'metal'), ('D3D8DE', 'Нержавеющая сталь', 'metal'), ('DCE1E6', 'Алюминий', 'metal'),
                      ('2F3742', 'Воронёная сталь', 'metal'), ('5C6066', 'Чугун', 'matte'), ('A9A79F', 'Титан', 'metal'),
                      ('C77845', 'Медь', 'metal'), ('CDA955', 'Латунь', 'metal'), ('E6BC48', 'Золото', 'metal')]),
         ('ПЛАСТИК', [('F1F1EE', 'Пластик белый', 'matte'), ('2A2C2F', 'Пластик чёрный', 'matte'), ('8E959D', 'Пластик серый', 'matte')])]

ti_9 = ('<div class="abs isl ti7" style="right:auto;padding:0 3px;gap:3px">'
        f'<div class="m on">{icon("tree-part", 16)}Тело</div><div class="m">{icon("work-plane", 16)}Грань</div></div>')
view_9 = f'<div class="abs view"><div class="abs" style="left:110px;top:90px">{MODEL}</div>{AXIS3}{islands7(ti_9)}</div>'
b2_9 = ('<div class="abs b2"><div class="brow" style="height:38px;gap:8px;padding:0 8px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">74</span>'
        f'<div class="search">{icon("ui-search", 14)}Поиск</div></div>'
        + g7('ЭСКИЗ И РАБ. ЭЛЕМЕНТЫ', ['sketch', 'work-plane', 'work-axis', 'work-point'])
        + g7('СОЗДАТЬ', ['extrude', 'revolve', 'hole', 'loft', 'sweep', 'coil', 'thread'])
        + g7('ИЗМЕНИТЬ', ['fillet', 'chamfer', 'shell'])
        + g7('ВИД', ['appearance', 'section', 'measure'], ('appearance',))
        + status7('appearance', 'Внешний вид', '3D-модель') + '</div>')

b1_9a = (f'<div class="abs b1">{tabs7()}'
         f'<div class="fl">Сейчас</div><div class="cur">{swatch("CDA955", "metal", 30)}<div>Латунь<small>металл · из библиотеки</small></div></div>'
         f'<div class="big3">{icon("ui-materials", 18)}Из библиотеки<span style="margin-left:auto">{icon("ui-chevron-right", 14)}</span></div>'
         f'<div class="big3">{icon("ui-palette", 18)}Мои цвета<span style="margin-left:auto">{icon("ui-chevron-right", 14)}</span></div>'
         f'<div class="big3">{icon("ui-plus", 18)}Создать цвет<span style="margin-left:auto">{icon("ui-chevron-right", 14)}</span></div>'
         '<div class="fl">Недавние</div><div class="recent">'
         + swatch('CDA955', 'metal') + swatch('2A2C2F') + swatch('D13B32') + swatch('B9C0C8', 'metal') + swatch('E6F0F8', 'glass', 24, .5)
         + f'</div>{row7(cursor=False)}</div>')
pages['9a-look-tool'] = page(hotbar7() + b1_9a + view_9 + b2_9 + strip()
    + note('Островок сверху: <b>Тело / Грань</b> (что красит тап по модели).<br>'
           'Блок 1: текущий вид, три кнопки, недавние — тап по кружку сразу берёт вид.<br>'
           '<b>✓</b> — готово, <b>✕</b> — откатить покраску по шагам, потом закрыть.', 'left:240px;top:292px'), P9_CSS)

# 9b. «Из библиотеки» opened: a scrolling list over block 1, ✕ in the top right corner closes it (the action row stays).
lst = ''
for grp, items in LOOKS:
    lst += f'<div class="gh">{grp}</div>' + ''.join(
        f'<div class="li{" on" if n == "Латунь" else ""}">{swatch(h, f, 22)}{n}{"<span style=margin-left:auto>" + icon("ui-check", 14, "#9db6ff") + "</span>" if n == "Латунь" else ""}</div>'
        for h, n, f in items)
pop_b = (f'<div class="abs pop"><div class="ph">Библиотека<div class="x">{icon("ui-close", 16)}</div></div>'
         f'<div style="height:306px;overflow:hidden;position:relative">{lst}<div class="abs vscroll" style="top:8px;height:110px"></div></div></div>')
pages['9b-look-library'] = page(hotbar7() + b1_9a + pop_b + view_9 + b2_9 + strip()
    + note('Список поверх блока 1, листается. Тап по строке — вид выбран<br>(список остаётся открытым, можно сразу тапать по модели).<br>'
           '<b>✕ вверху справа</b> — закрыть список. ✓ / ✕ внизу видны всегда.<br>'
           'Так же открываются любые длинные списки: размеры резьбы, «Мои цвета» и т. п.', 'left:240px;top:276px'), P9_CSS)

# 9c. «Создать цвет»: the same pop-up with the editor of a new look.
pop_c = (f'<div class="abs pop"><div class="ph">Новый цвет<div class="x">{icon("ui-close", 16)}</div></div>'
         '<div style="height:306px;overflow:hidden;position:relative">'
         '<div class="fl">Название</div><div class="fld" style="height:30px">Латунь тёмная</div>'
         '<div class="fl">Вид</div><div class="seg2"><div class="on">Цвет</div><div>Картинка</div></div>'
         '<div class="fl">Цвет граней</div><div class="sv"><div class="knob" style="left:120px;top:30px"></div></div>'
         '<div class="hue"><div class="knob" style="left:16px;top:-1px"></div></div>'
         '<div class="fl">Поверхность</div><div class="seg2">'
         f'<div>{swatch("A88A45", "matte", 16)}Мат.</div><div class="on">{swatch("A88A45", "metal", 16)}Мет.</div><div>{swatch("A88A45", "glass", 16, .5)}Стекло</div></div>'
         '<div class="abs vscroll" style="top:8px;height:200px"></div></div>'
         '<div class="abs" style="left:10px;right:10px;bottom:8px;height:34px;border-radius:9px;background:var(--acc);display:flex;align-items:center;justify-content:center;font-weight:500">Сохранить в «Мои»</div></div>')
pages['9c-look-create'] = page(hotbar7() + b1_9a + pop_c + view_9 + b2_9 + strip()
    + note('«Создать цвет» — та же всплывашка: название, цвет или картинка, оттенок,<br>поверхность (матовая · металл · стекло); ниже прокруткой — цвет рёбер, прозрачность.<br>'
           '«Сохранить в «Мои»» — вид сохранён и сразу выбран, всплывашка закрывается.', 'left:240px;top:292px'), P9_CSS)


# 10. Touch cursor concepts (the author 18.10: "предложи красивые варианты курсоров"). Each: tip above the finger
# (offset as in the settings), the finger's place, and how it shows a snap (green).
SIZES['10-cursors'] = (1120, 600)
C_CSS = """
body{width:1120px;height:600px;background:#121418}
.cc{position:absolute;width:268px;height:280px;border-radius:14px;overflow:hidden;border:1px solid #2a2e35;
    background-image:linear-gradient(#2a2e35 1px,transparent 1px),linear-gradient(90deg,#2a2e35 1px,transparent 1px);background-size:22px 22px;background-color:#1c1f24}
.cc .t{position:absolute;left:12px;top:10px;font-weight:600;font-size:14px;color:#E6E8EB}
.cc .d{position:absolute;left:12px;right:12px;bottom:10px;font-size:11px;color:#AEB4BD;line-height:1.35}
.fing{position:absolute;width:44px;height:44px;border-radius:50%;background:radial-gradient(rgba(255,214,180,.22),rgba(255,214,180,.05));border:1px dashed rgba(255,214,180,.35)}
.lineA{position:absolute;left:0;right:0;top:118px;height:1px;background:#8a4447}
"""
B = '#6E9BFF'; G = '#4CD07D'


def cell(x, y, title, svg_idle, svg_snap, desc):
    return (f'<div class="cc" style="left:{x}px;top:{y}px"><div class="t">{title}</div><div class="lineA"></div>'
            f'<svg style="position:absolute;left:0;top:0" width="268" height="280">{svg_idle}{svg_snap}</svg>'
            f'<div class="fing" style="left:56px;top:178px"></div><div class="fing" style="left:168px;top:178px"></div>'
            f'<div class="d">{desc}</div></div>')

# finger centres: (78,200) and (190,200); tips 64 px above: (78,136) / snap at (190,118) on the red line
def drop(cx, cy, tx, ty, c):
    return (f'<path d="M{tx} {ty} L{cx+13} {cy-14} A18 18 0 1 1 {cx-13} {cy-14} Z" fill="{c}22" stroke="{c}" stroke-width="1.8"/>'
            f'<circle cx="{tx}" cy="{ty}" r="2.6" fill="#fff"/>')
def reticle(cx, cy, tx, ty, c, snap=False):
    s = (f'<line x1="{cx}" y1="{cy-22}" x2="{tx}" y2="{ty+11}" stroke="{c}" stroke-width="1.2" stroke-dasharray="3 3" opacity=".8"/>'
         f'<circle cx="{tx}" cy="{ty}" r="9" fill="none" stroke="{c}" stroke-width="1.8"/>'
         f'<path d="M{tx-16} {ty}h6M{tx+10} {ty}h6M{tx} {ty-16}v6M{tx} {ty+10}v6" stroke="{c}" stroke-width="1.8" stroke-linecap="round"/>'
         f'<circle cx="{tx}" cy="{ty}" r="1.8" fill="#fff"/><circle cx="{cx}" cy="{cy}" r="22" fill="none" stroke="{c}" stroke-width="1.5" opacity=".55"/>')
    if snap: s += f'<rect x="{tx-5}" y="{ty-5}" width="10" height="10" fill="none" stroke="{c}" stroke-width="1.6" transform="rotate(45 {tx} {ty})"/>'
    return s
def nib(cx, cy, tx, ty, c):
    return (f'<path d="M{tx} {ty} L{tx+9} {ty+30} Q{cx+20} {cy-6} {cx} {cy+22} Q{cx-20} {cy-6} {tx-9} {ty+30} Z" fill="{c}26" stroke="{c}" stroke-width="1.6" stroke-linejoin="round"/>'
            f'<line x1="{tx}" y1="{ty+4}" x2="{tx}" y2="{ty+22}" stroke="{c}" stroke-width="1.2"/><circle cx="{tx}" cy="{ty+24}" r="2.4" fill="{c}"/>'
            f'<circle cx="{tx}" cy="{ty}" r="2.4" fill="#fff"/>')
def orbit(cx, cy, tx, ty, c):
    return (f'<circle cx="{cx}" cy="{cy}" r="24" fill="none" stroke="{c}" stroke-width="3" stroke-dasharray="30 8" opacity=".85"/>'
            f'<path d="M{cx-8} {cy-23} L{tx} {ty+8} L{cx+8} {cy-23}" fill="{c}30" stroke="{c}" stroke-width="1.2"/>'
            f'<circle cx="{tx}" cy="{ty}" r="7" fill="{c}" opacity=".25"/><circle cx="{tx}" cy="{ty}" r="3.5" fill="#fff" stroke="{c}" stroke-width="1.6"/>')
def needle(cx, cy, tx, ty, c):
    return (f'<line x1="{tx}" y1="{ty}" x2="{cx}" y2="{cy-14}" stroke="{c}" stroke-width="2.2" stroke-linecap="round"/>'
            f'<circle cx="{cx}" cy="{cy}" r="14" fill="{c}" opacity=".9"/><circle cx="{cx-4}" cy="{cy-4}" r="4" fill="#fff" opacity=".55"/>'
            f'<circle cx="{tx}" cy="{ty}" r="5" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="{tx}" cy="{ty}" r="1.6" fill="#fff"/>')
def arrow(cx, cy, tx, ty, c):
    return (f'<path d="M{tx} {ty} l0 26 l7 -6 l5 11 l5 -2 l-5 -11 l9 0 Z" fill="#fff" stroke="#111" stroke-width="1.3" stroke-linejoin="round"/>'
            f'<path d="M{tx+12} {ty+36} Q{cx+6} {cy-30} {cx} {cy-20}" fill="none" stroke="{c}" stroke-width="1.3" stroke-dasharray="3 3"/>'
            f'<circle cx="{cx}" cy="{cy}" r="20" fill="{c}18" stroke="{c}" stroke-width="1.6"/>')

def two(fn, **kw):
    return fn(78, 200, 78, 136, B, **kw) if fn is not reticle else fn(78, 200, 78, 136, B)

cells = [
    ('1. Капля (сейчас)', drop(78, 200, 78, 136, B), drop(190, 200, 190, 118, G),
     'Классика: острие — кончик, круг под пальцем. Привязка — капля зеленеет.'),
    ('2. Прицел', reticle(78, 200, 78, 136, B), reticle(190, 200, 190, 118, G, True),
     'Как в шутере: перекрестье в точке, тонкий поводок к кольцу пальца. Привязка — ромб в центре.'),
    ('3. Перо', nib(78, 200, 78, 136, B), nib(190, 200, 190, 118, G),
     'Перьевая ручка: щель указывает точку. «Черчение» буквально, ощущение инструмента.'),
    ('4. Орбита', orbit(78, 200, 78, 136, B), orbit(190, 200, 190, 118, G),
     'Кольцо-джойстик вокруг пальца и светящаяся точка. Игровой, лёгкий, почти не закрывает чертёж.'),
    ('5. Игла', needle(78, 200, 78, 136, B), needle(190, 200, 190, 118, G),
     'Булавка вверх ногами: шарик под пальцем, игла к кончику. Очень тонкий — видно всё вокруг точки.'),
    ('6. Стрелка с ручкой', arrow(78, 200, 78, 136, B), arrow(190, 200, 190, 118, G),
     'Привычная стрелка мыши, пунктир к пальцу. Понятно с первого раза, но меньше «изюма».'),
    ('7. Неон', '', '', ''),
    ('8. Минимал', '', '', ''),
]
# 7. Neon: glowing ring at the tip with a soft trail; 8. Minimal: just a dot + a hair-thin ring, the finger ring hidden.
NEON = lambda cx, cy, tx, ty, c: (f'<defs><filter id="gl{cx}" x="-80%" y="-80%" width="260%" height="260%"><feGaussianBlur stdDeviation="3"/></filter></defs>'
    f'<circle cx="{tx}" cy="{ty}" r="10" fill="none" stroke="{c}" stroke-width="3" filter="url(#gl{cx})"/>'
    f'<circle cx="{tx}" cy="{ty}" r="10" fill="none" stroke="#fff" stroke-width="1.2"/><circle cx="{tx}" cy="{ty}" r="2" fill="#fff"/>'
    f'<path d="M{tx} {ty+12} L{cx} {cy-20}" stroke="{c}" stroke-width="4" opacity=".35" filter="url(#gl{cx})"/>'
    f'<path d="M{tx} {ty+12} L{cx} {cy-20}" stroke="{c}" stroke-width="1"/>')
MINI = lambda cx, cy, tx, ty, c: (f'<circle cx="{tx}" cy="{ty}" r="12" fill="none" stroke="{c}" stroke-width="1" opacity=".8"/>'
    f'<circle cx="{tx}" cy="{ty}" r="3" fill="{c}"/><circle cx="{tx}" cy="{ty}" r="1.2" fill="#fff"/>'
    f'<path d="M{cx-6} {cy-26}l6 -6 6 6" fill="none" stroke="{c}" stroke-width="1.6" stroke-linecap="round" opacity=".7"/>')
cells[6] = ('7. Неон', NEON(78, 200, 78, 136, B), NEON(190, 200, 190, 118, G),
            'Светящееся кольцо в точке, светящийся поводок. Красиво на тёмной теме, «как в игре».')
cells[7] = ('8. Минимал', MINI(78, 200, 78, 136, B), MINI(190, 200, 190, 118, G),
            'Только точка с тонким кольцом и уголок над пальцем. Чертёж закрыт меньше всего.')
body = ''.join(cell(12 + (i % 4) * 276, 12 + (i // 4) * 292, *c) for i, c in enumerate(cells))
pages['10-cursors'] = page(body + '<div class="abs" style="left:12px;bottom:-2px;color:#9AA1AB;font-size:11px"></div>', C_CSS)


# 11. The drop cursor redrawn (the author 18.10: "а как бы ты каплю нарисовал?") and the full 3D tab of block 2.
import math

SIZES['11-drop-cursor'] = (1120, 600)
D11_CSS = C_CSS + """
.cc.w{width:544px}
.st11{position:absolute;font-size:10.5px;color:#9AA1AB;text-align:center;width:90px}
"""


def tear(tx, ty, cx, cy, r):
    d = cy - ty
    a = math.asin(r / d)
    x1, y1 = cx + r * math.cos(a), cy - r * math.sin(a)
    x2, y2 = cx - r * math.cos(a), cy - r * math.sin(a)
    return f'M{tx} {ty} L{x1:.1f} {y1:.1f} A{r} {r} 0 1 1 {x2:.1f} {y2:.1f} Z'


def dropA(cx, cy, tx, ty, c, state='idle'):
    """Lens: a frosted round end (the finger shows through), a needle tip that never covers the point, a halo dot."""
    uid = f'{cx}{cy}{state}'
    fill = {'idle': c + '1f', 'snap': c + '24', 'press': c + '55'}[state]
    s = (f'<defs><radialGradient id="lg{uid}" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#fff" stop-opacity=".22"/>'
         f'<stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>'
         f'<filter id="sh{uid}" x="-50%" y="-50%" width="200%" height="200%"><feDropShadow dx="0" dy="2" stdDeviation="2.5" flood-color="#000" flood-opacity=".55"/></filter></defs>'
         f'<g filter="url(#sh{uid})"><path d="{tear(tx, ty + 9, cx, cy, 21)}" fill="{fill}" stroke="{c}" stroke-width="1.8" stroke-linejoin="round"/></g>'
         f'<circle cx="{cx}" cy="{cy}" r="21" fill="url(#lg{uid})"/>'
         f'<line x1="{tx}" y1="{ty + 9}" x2="{tx}" y2="{ty + 4}" stroke="{c}" stroke-width="1.4"/>'
         f'<circle cx="{tx}" cy="{ty}" r="7" fill="none" stroke="{c}" stroke-width="1" opacity=".55"/>'
         f'<circle cx="{tx}" cy="{ty}" r="2.4" fill="#fff"/>')
    if state == 'snap':
        s += f'<rect x="{tx - 6}" y="{ty - 6}" width="12" height="12" rx="1.5" fill="none" stroke="{c}" stroke-width="1.6"/>'
    return s


def dropB(cx, cy, tx, ty, c, state='idle'):
    """Notch: an outline drop open at the very tip — the point sits in the gap, nothing of the cursor covers it."""
    p = tear(tx, ty, cx, cy, 20)
    return (f'<path d="{p}" fill="{c}14" stroke="{c}" stroke-width="2" stroke-linejoin="round"/>'
            f'<circle cx="{tx}" cy="{ty}" r="8" fill="#1c1f24"/>'
            f'<circle cx="{tx}" cy="{ty}" r="2.4" fill="{c}"/><circle cx="{cx}" cy="{cy}" r="6" fill="none" stroke="{c}" stroke-width="1.4" opacity=".6"/>')


def dropC(cx, cy, tx, ty, c, state='idle'):
    """Solid gem: a filled drop with a gradient and a white facet; the tip is a crisp white dot."""
    uid = f'g{cx}{cy}'
    return (f'<defs><linearGradient id="{uid}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".55"/>'
            f'<stop offset=".45" stop-color="{c}"/><stop offset="1" stop-color="{c}" stop-opacity=".55"/></linearGradient></defs>'
            f'<path d="{tear(tx, ty + 6, cx, cy, 19)}" fill="url(#{uid})" opacity=".85" stroke="#fff" stroke-opacity=".5" stroke-width="1"/>'
            f'<path d="M{cx - 10} {cy - 6} Q{cx - 9} {cy - 15} {cx - 2} {cy - 18}" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round" opacity=".7"/>'
            f'<circle cx="{tx}" cy="{ty}" r="3" fill="#fff" stroke="{c}" stroke-width="1.4"/>')


def dropD(cx, cy, tx, ty, c, state='idle'):
    """Liquid: the drop stretches while the finger moves fast and snaps back; a ripple ring when it sticks to a point."""
    s = (f'<path d="{tear(tx, ty, cx, cy, 18)}" fill="{c}22" stroke="{c}" stroke-width="1.8"/>'
         f'<ellipse cx="{cx}" cy="{cy + 6}" rx="22" ry="7" fill="none" stroke="{c}" stroke-width="1" opacity=".35"/>'
         f'<circle cx="{tx}" cy="{ty}" r="2.4" fill="#fff"/>')
    return s


def cell11(x, y, title, fn, desc):
    return cell(x, y, title, fn(78, 200, 78, 136, B), fn(190, 200, 190, 118, G), desc)


body11 = (cell11(12, 12, 'А. Линза', dropA, 'Круг-линза: палец просвечивает, лёгкий блик и тень. Кончик — игла с ореолом: сама точка ничем не закрыта.')
          + cell11(288, 12, 'Б. С вырезом', dropB, 'Контур открыт у самого кончика — точка в «окошке», вокруг пусто. Самый точный.')
          + cell11(564, 12, 'В. Самоцвет', dropC, 'Залитая капля с бликом, как камень. Нарядно, хорошо видно на любом фоне.')
          + cell11(840, 12, 'Г. Жидкая', dropD, 'Тянется при быстром движении и «плюхается» обратно; тень-лужица под пальцем.'))
# The states of «А» (my pick): idle, snap to a point, pressed (OK held), the precise mode (tip slower than the finger).
states = (f'<div class="cc w" style="left:12px;top:304px"><div class="t">А — состояния</div><div class="lineA"></div>'
          f'<svg style="position:absolute;left:0;top:0" width="544" height="280">'
          + dropA(70, 200, 70, 136, B) + dropA(196, 200, 196, 118, G, 'snap') + dropA(322, 200, 322, 136, B, 'press')
          + dropA(448, 200, 448, 136, '#F5A524')
          + '<path d="M448 214 v26" stroke="#F5A524" stroke-width="1" stroke-dasharray="2 3"/></svg>'
          + ''.join(f'<div class="st11" style="left:{x - 45}px;top:84px">{t}</div>' for x, t in
                    [(70, 'покой'), (196, 'привязка к точке'), (322, 'нажата ✓'), (448, 'точный режим')])
          + '<div class="d">Привязка: значок формы цели (квадрат — точка, треугольник — середина, ✕ — на кривой). '
            'Нажатие — капля заливается. Точный режим — оранжевая, кончик идёт медленнее пальца.</div></div>')
states += ('<div class="cc w" style="left:564px;top:304px;background:#1c1f24"><div class="t">Почему так</div>'
           '<div class="d" style="top:44px;bottom:auto;font-size:12.5px;line-height:1.55;color:#cfd4da">'
           '• Нынешняя капля закрывает точку своим острием — у «А» острие кончается иглой, точка свободна.<br>'
           '• Круг под пальцем полупрозрачный — видно, что под пальцем.<br>'
           '• Тень отделяет курсор от чертежа на любом фоне, в том числе в светлой теме.<br>'
           '• Цвета состояний: синий — работа, зелёный — привязка, оранжевый — точно.<br>'
           '• Форма та же, что сейчас, — привыкать не нужно, это доводка, а не замена.</div></div>')
pages['11-drop-cursor'] = page(body11 + states, D11_CSS)

# 12. Block 2, the full 3D-model tab (what the ribbon really has), shown taller than the phone so the fold is visible.
SIZES['12-model-tab'] = (420, 560)
T12 = R7_CSS + """body{width:420px;height:560px}
.fold{left:0;width:180px;height:0;border-top:2px dashed #F5A524;z-index:30}
"""
tab = ('<div class="abs b2" style="left:20px;right:auto;width:180px;height:560px;bottom:auto">'
       '<div class="brow" style="height:38px;gap:8px;padding:0 8px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">74</span>'
       f'<div class="search">{icon("ui-search", 14)}Поиск</div></div>'
       + g7('ЭСКИЗ И РАБ. ЭЛЕМЕНТЫ', ['sketch', 'work-plane', 'work-axis', 'work-point'])
       + g7('СОЗДАТЬ', ['extrude', 'revolve', 'sweep', 'loft', 'coil'])
       + g7('ИЗМЕНИТЬ', ['hole', 'fillet', 'chamfer', 'thread', 'shell', 'cut-section'])
       + g7('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])
       + g7('ДИАГНОСТИКА', ['measure', 'section', 'mass-properties', 'interference'])
       + g7('ИЗДЕЛИЯ И ЭКСПОРТ', ['library', 'export'])
       + '</div><div class="abs fold" style="left:20px;top:384px"></div>')
pages['12-model-tab'] = page(tab + note('Край экрана<br>телефона (412 dp)', 'left:212px;top:368px')
    + note('Всё самое частое — без прокрутки.<br>Ниже края — диагностика,<br>библиотека и экспорт<br>(экспорт — один значок,<br>формат выбирается в списке).', 'left:212px;top:130px'), T12)


# 13. Block 2, every other tab (the author 18.10): Эскиз in full, Вид, Сборка, Вид in the assembly. The dashed line is
# the phone's screen edge: everything above it is reachable without scrolling.
SIZES['13-other-tabs'] = (1000, 730)
T13 = R7_CSS + """body{width:1000px;height:730px}
.col{position:absolute;top:30px;width:180px;background:var(--panel);border:1px solid var(--div);border-radius:0 0 10px 10px}
.ct{position:absolute;top:6px;font-size:12px;font-weight:600;color:var(--text)}
.fold{position:absolute;width:200px;height:0;border-top:2px dashed #F5A524;z-index:30}
.g7 .r div{position:relative}
.g7 .r div b{position:absolute;right:3px;bottom:1px;font-size:9px;font-weight:600;color:#cfdcff}
"""


def g7x(title, cells):
    out = ''
    for c in cells:
        n, lab = (c, '') if isinstance(c, str) else c
        out += f'<div>{icon(n, 20, "#fff")}{"<b>" + lab + "</b>" if lab else ""}</div>'
    return f'<div class="g7"><div class="h">{title}</div><div class="r">{out}</div></div>'


HEAD = ('<div class="brow" style="height:38px;gap:8px;padding:0 8px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">74</span>'
        f'<div class="search">{icon("ui-search", 14)}Поиск</div></div>')
sketch_col = (HEAD + f'<div class="fin">{icon("ui-check", 16, "#9db6ff")}Завершить эскиз</div>'
              + g7x('РАЗМЕРЫ', ['sk-dimension', 'dim-arrange'])
              + g7x('СОЗДАТЬ', ['sk-line', 'sk-circle', 'sk-arc', 'sk-rectangle', 'sk-point', 'sk-spline', 'sk-text'])
              + g7x('ЗАВИСИМОСТИ', ['c-coincident', 'c-horizontal', 'c-vertical', 'c-fix', 'c-parallel', 'c-perpendicular', 'c-equal',
                                    'c-midpoint', 'c-tangent', 'c-concentric', 'c-symmetric', 'c-show-all'])
              + g7x('ИЗМЕНИТЬ', ['sk-trim', 'sk-extend', 'sk-offset', 'sk-fillet', 'sk-move', 'sk-rotate', 'sk-copy', 'sk-scale', 'sk-split', 'ui-trash'])
              + g7x('МАССИВ И МОДЕЛЬ', ['sk-pattern-rect', 'sk-pattern-circular', 'sk-mirror', 'sk-project', 'sk-project-fixed'])
              + g7x('ФОРМАТ И ВИД', ['sk-construction', 'sk-centerline', 'c-perpendicular', 'sk-look-at']))
view_col = (HEAD
            + g7x('СТИЛЬ ОТОБРАЖЕНИЯ', ['ds-shaded', 'ds-shaded-edges', 'ds-wireframe', 'ds-hidden-lines', 'ds-transparent'])
            + g7x('ЦВЕТА', ['appearance', 'view-background'])
            + g7x('ОРИЕНТАЦИЯ', ['view-home', 'view-fit', ('tree-plane', 'Ф'), ('tree-plane', 'В'), ('tree-plane', 'П')])
            + g7x('ОФОРМЛЕНИЕ', ['theme', 'ui-sun', 'ui-moon']))
asm_col = (HEAD
           + g7x('КОМПОНЕНТ', ['place-component', 'library', 'pattern-rect'])
           + g7x('ЗАВИСИМОСТИ', ['a-mate', 'a-insert', 'a-angle', 'a-tangent', 'a-axis'])
           + g7x('СОЕДИНЕНИЯ', ['j-rotational', 'j-slider', 'j-cylindrical', 'j-planar', 'j-ball', 'j-rigid'])
           + g7x('ПОЛОЖЕНИЕ', ['move-component', 'rotate-component', 'ui-lock'])
           + g7x('ДИАГНОСТИКА И ЭКСПОРТ', ['measure', 'section', 'interference', 'export']))


def col(x, title, inner, h):
    return f'<div class="ct" style="left:{x}px">{title}</div><div class="col" style="left:{x}px;height:{h}px">{inner}</div>'


FOLD = 30 + 412 - 28   # the tab's top + the phone's height - the status line
body13 = (col(16, 'Эскиз (целиком)', sketch_col, 690) + col(266, 'Вид', view_col, 375) + col(516, 'Сборка', asm_col, 440)
          + ''.join(f'<div class="fold" style="left:{x - 10}px;top:{FOLD}px"></div>' for x in (16, 266, 516))
          + note('Пунктир — край экрана телефона.<br><br><b>Эскиз:</b> над краем — размеры, создание<br>и все зависимости. Ниже — «Изменить»<br>(обрезка, сдвиг, копия…), массивы,<br>проецирование, формат.<br><br>'
                 '<b>Вид</b> помещается целиком.<br><b>Сборка</b> — всё, кроме последнего ряда<br>(диагностика и экспорт), он чуть ниже края.<br><br>'
                 'Буквы Ф / В / П на плоскостях —<br>спереди, сверху, справа.', 'left:740px;top:40px')
          + note('Предложение: «Оформление» (тема) на<br>телефоне убрать в настройки — там ему место.', 'left:740px;top:330px'))
pages['13-other-tabs'] = page(body13, T13)


# 14. Folding groups (the author 18.10): «Зависимости» and «Изменить» show one row — the opener + 3 working buttons.
# The opener is always on the side of the view (block 2 on the right -> the opener is the leftmost; mirrored for lefties).
SIZES['14-fold-groups'] = (1000, 620)
T14 = T13.replace('body{width:1000px;height:730px}', 'body{width:1000px;height:620px}') + """
.g7 .r .op{background:var(--accSoft);outline:1px solid var(--accB);flex-direction:column;gap:0}
.g7 .r .op i{font-style:normal;font-size:9px;color:#9db6ff;line-height:1}
.g7 .r .op.open{background:var(--acc);outline-color:var(--acc)}
.g7 .r .op.open i{color:#fff}
"""


def fold(title, shown, rest, opened=False):
    chev = icon('ui-chevron-down', 14, '#fff' if opened else '#9db6ff', 'transform:rotate(180deg)' if opened else '')
    cells = f'<div class="op{" open" if opened else ""}">{chev}<i>{"" if opened else "+" + str(len(rest))}</i></div>'
    cells += ''.join(f'<div>{icon(n, 20, "#fff")}</div>' for n in shown)
    if opened:
        cells += ''.join(f'<div>{icon(n, 20, "#fff")}</div>' for n in rest)
    return f'<div class="g7"><div class="h">{title}</div><div class="r">{cells}</div></div>'


C_SHOWN, C_REST = ['c-coincident', 'c-horizontal', 'c-vertical'], ['c-fix', 'c-parallel', 'c-perpendicular', 'c-equal', 'c-midpoint',
                                                                       'c-tangent', 'c-concentric', 'c-symmetric', 'c-show-all']
M_SHOWN, M_REST = ['sk-trim', 'sk-extend', 'sk-offset'], ['sk-fillet', 'sk-move', 'sk-rotate', 'sk-copy', 'sk-scale', 'sk-split', 'ui-trash']


def sketch14(open_c=False):
    return (HEAD + f'<div class="fin">{icon("ui-check", 16, "#9db6ff")}Завершить эскиз</div>'
            + g7x('РАЗМЕРЫ', ['sk-dimension', 'dim-arrange'])
            + g7x('СОЗДАТЬ', ['sk-line', 'sk-circle', 'sk-arc', 'sk-rectangle', 'sk-point', 'sk-spline', 'sk-text'])
            + fold('ЗАВИСИМОСТИ', C_SHOWN, C_REST, open_c)
            + fold('ИЗМЕНИТЬ', M_SHOWN, M_REST)
            + g7x('МАССИВ И МОДЕЛЬ', ['sk-pattern-rect', 'sk-pattern-circular', 'sk-mirror', 'sk-project', 'sk-project-fixed'])
            + g7x('ФОРМАТ И ВИД', ['sk-construction', 'sk-centerline', 'c-perpendicular', 'sk-look-at']))


model14 = (HEAD + g7x('ЭСКИЗ И РАБ. ЭЛЕМЕНТЫ', ['sketch', 'work-plane', 'work-axis', 'work-point'])
           + g7x('СОЗДАТЬ', ['extrude', 'revolve', 'sweep', 'loft', 'coil'])
           + fold('ИЗМЕНИТЬ', ['hole', 'fillet', 'chamfer'], ['thread', 'shell', 'cut-section'])
           + g7x('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])
           + g7x('ДИАГНОСТИКА', ['measure', 'section', 'mass-properties', 'interference'])
           + g7x('ИЗДЕЛИЯ И ЭКСПОРТ', ['library', 'export']))
FOLD14 = 30 + 412 - 28
body14 = (col(16, 'Эскиз — свёрнуто', sketch14(), 530) + col(266, 'Эскиз — «Зависимости» раскрыты', sketch14(True), 575)
          + col(516, '3D-модель', model14, 445)
          + ''.join(f'<div class="fold" style="left:{x - 10}px;top:{FOLD14}px"></div>' for x in (16, 266, 516))
          + note('Свёрнуто: раскрывашка + 3 рабочие кнопки.<br>На раскрывашке — сколько спрятано («+9»).<br>'
                 'Раскрывашка всегда со стороны вида:<br>блок 2 справа — она слева; у левши — справа.<br><br>'
                 'Тап — группа раскрывается на месте<br>(первый ряд не двигается), стрелка вверх.<br>'
                 'Повторный тап — свернуть. Раскрытость<br>запоминается.<br><br>'
                 'Эскиз свёрнутый: размеры, создание,<br>зависимости и «Изменить» — без прокрутки<br>(было — только до зависимостей).', 'left:740px;top:40px')
          + note('Какие 3 кнопки на виду — моё<br>предположение: совпадение, горизонталь,<br>вертикаль; обрезка, удлинение,<br>эквидистанта; отверстие, скругление, фаска.', 'left:740px;top:390px'))
pages['14-fold-groups'] = page(body14, T14)


# 15. FINAL set (the author 18.10): every tab and state on the phone, everything approved in one style.
FIN_CSS = R7C_CSS + P9_CSS.replace(R7C_CSS, '') + """
.g7 .r .op{background:var(--accSoft);outline:1px solid var(--accB);flex-direction:column;gap:0}
.g7 .r .op i{font-style:normal;font-size:9px;color:#9db6ff;line-height:1}
.tree7{padding:4px 10px;line-height:32px}
.tree7 div{display:flex;align-items:center;gap:8px;white-space:nowrap}
.tree7 .on{background:var(--accSoft);border-radius:7px;margin:0 -6px;padding:0 6px;color:#cfdcff}
"""


def tabs_tree():
    return (f'<div class="tabs"><div class="tab on" style="flex:1">{icon("ui-tree", 16, "#E6E8EB")}Дерево</div>'
            f'<div class="tab">{icon("ui-wrench", 16)}</div><div class="tab">{icon("ui-dimension", 16)}</div></div>')


def b1_tree(items, sel=None):
    rows = ''.join(f'<div class="{"on" if t == sel else ""}" style="padding-left:{lvl * 14}px">{icon(i, 16)}{t}</div>' for lvl, i, t in items)
    return f'<div class="abs b1">{tabs_tree()}<div class="tree7">{rows}</div>{row7(cursor=False)}</div>'


PART_TREE = [(0, 'tree-part', 'Кронштейн'), (1, 'tree-origin', 'Начало координат'), (1, 'tree-sketch', 'Эскиз1'),
             (1, 'tree-extrude', 'Выдавливание1'), (1, 'tree-sketch', 'Эскиз2'), (1, 'tree-hole', 'Отверстие1'), (1, 'tree-fillet', 'Скругление1')]
ASM_TREE = [(0, 'tree-assembly', 'Узел крепления'), (1, 'tree-origin', 'Начало координат'), (1, 'tree-component', 'Кронштейн:1'),
            (1, 'tree-component', 'Фланец:1'), (1, 'tree-component', 'Винт M6:1'), (1, 'tree-component', 'Винт M6:2'), (1, 'a-mate', 'Зависимости (4)')]


def b2_full(groups, status, finish=False, fps='74'):
    return ('<div class="abs b2"><div class="brow" style="height:38px;gap:8px;padding:0 8px">'
            f'<span style="font-size:11px;color:#4CBB7F;font-family:monospace">{fps}</span><div class="search">{icon("ui-search", 14)}Поиск</div></div>'
            + (f'<div class="fin">{icon("ui-check", 16, "#9db6ff")}Завершить эскиз</div>' if finish else '')
            + groups + status + '</div>')


def tabs_island(names, on):
    t = ''.join(f'<div class="t{" on" if n == on else ""}">{n}</div>' for n in names)
    return f'<div class="abs isl" style="left:84px;top:8px">{icon("ui-arrow-left", 16)}{t}</div>'


def view_full(inner, top_island, sketch=False):
    bg = (f'<div class="abs view" style="background:none">{GRID}{AXES}' if sketch else '<div class="abs view">')
    return bg + inner + AXIS3 * (not sketch) + islands7(top_island) + '</div>'


MODEL_AT = f'<div class="abs" style="left:110px;top:90px">{MODEL}</div>'
SK_PROFILE = ('<svg class="abs" style="left:110px;top:70px" width="300" height="200"><path d="M30 126H250V100H150V20H110V100H30Z" fill="rgba(110,155,255,.07)" '
              'stroke="#E6E8EB" stroke-width="1.6"/><circle cx="70" cy="113" r="7" fill="none" stroke="#E6E8EB" stroke-width="1.4"/>'
              '<circle cx="210" cy="113" r="7" fill="none" stroke="#E6E8EB" stroke-width="1.4"/>'
              '<path d="M30 150H250M30 144v12M250 144v12" stroke="#6E9BFF" stroke-width="1"/><text x="128" y="146" fill="#9db6ff" font-size="11" font-family="Plex">120</text></svg>')

G_MODEL = (g7x('ЭСКИЗ И РАБ. ЭЛЕМЕНТЫ', ['sketch', 'work-plane', 'work-axis', 'work-point'])
           + g7x('СОЗДАТЬ', ['extrude', 'revolve', 'sweep', 'loft', 'coil'])
           + fold('ИЗМЕНИТЬ', ['hole', 'fillet', 'chamfer'], ['thread', 'shell', 'cut-section'])
           + g7x('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])
           + g7x('ДИАГНОСТИКА', ['measure', 'section', 'mass-properties', 'interference'])
           + g7x('ИЗДЕЛИЯ И ЭКСПОРТ', ['library', 'export']))


def g_sketch(active=None):
    def gx(title, names):
        return g7(title, names, (active,) if active else ())
    return (gx('РАЗМЕРЫ', ['sk-dimension', 'dim-arrange'])
            + gx('СОЗДАТЬ', ['sk-line', 'sk-circle', 'sk-arc', 'sk-rectangle', 'sk-point', 'sk-spline', 'sk-text'])
            + fold('ЗАВИСИМОСТИ', C_SHOWN, C_REST) + fold('ИЗМЕНИТЬ', M_SHOWN, M_REST)
            + g7x('МАССИВ И МОДЕЛЬ', ['sk-pattern-rect', 'sk-pattern-circular', 'sk-mirror', 'sk-project', 'sk-project-fixed']))


G_VIEW = (g7x('СТИЛЬ ОТОБРАЖЕНИЯ', ['ds-shaded', 'ds-shaded-edges', 'ds-wireframe', 'ds-hidden-lines', 'ds-transparent'])
          + g7x('ЦВЕТА', ['appearance', 'view-background'])
          + g7x('ОРИЕНТАЦИЯ', ['view-home', 'view-fit', ('tree-plane', 'Ф'), ('tree-plane', 'В'), ('tree-plane', 'П')]))
G_ASM = (g7x('КОМПОНЕНТ', ['place-component', 'library', 'pattern-rect'])
         + fold('ЗАВИСИМОСТИ', ['a-mate', 'a-insert', 'a-axis'], ['a-angle', 'a-tangent'])
         + fold('СОЕДИНЕНИЯ', ['j-rotational', 'j-slider', 'j-rigid'], ['j-cylindrical', 'j-planar', 'j-ball'])
         + g7x('ПОЛОЖЕНИЕ', ['move-component', 'rotate-component', 'ui-lock'])
         + g7x('ДИАГНОСТИКА И ЭКСПОРТ', ['measure', 'section', 'interference', 'export']))
ASM_MODEL = ('<div class="abs" style="left:90px;top:60px;transform:scale(.9);transform-origin:0 0">' + MODEL + '</div>'
             '<svg class="abs" style="left:330px;top:110px" width="90" height="110"><path d="M10 40l35-17 35 17v40l-35 17-35-17z" fill="#8c95a3"/>'
             '<path d="M10 40l35 17 35-17" fill="none" stroke="#c3c9d1"/><path d="M45 57v40" stroke="#5f6672"/></svg>')
STRIP_ASM = strip().replace('Кронштейн</div><div class="chip off">Фланец', 'Кронштейн</div><div class="chip off">Фланец</div><div class="chip">Узел крепления')

FIN = {}
# F1. 3D-model, nothing open.
FIN['F1-model'] = (hotbar7() + b1_tree(PART_TREE) + view_full(MODEL_AT, tabs_island(['3D-модель', 'Эскиз', 'Вид'], '3D-модель'))
                   + b2_full(G_MODEL, status7('tree-part', 'Кронштейн', '3D-модель')) + strip())
# F2. Sketch, nothing open (the Линза cursor resting at the centre).
FIN['F2-sketch'] = (hotbar7() + b1_tree(PART_TREE, 'Эскиз2')
                    + view_full(SK_PROFILE + '<svg class="abs" style="left:0;top:0" width="509" height="372">' + dropA(330, 290, 330, 226, B) + '</svg>',
                                tabs_island(['3D-модель', 'Эскиз', 'Вид'], 'Эскиз'), True)
                    + b2_full(g_sketch(), status7('tree-sketch', 'Эскиз2', 'Эскиз'), True, '107') + strip())
# F3. Sketch, the rectangle tool: the way island + the line type island; the cursor snapped to a corner.
ti_f3 = ti_7a2
FIN['F3-sketch-rect'] = (hotbar7() + b1_7a
                         + view_full('<div class="abs" style="left:170px;top:116px;width:200px;height:80px;border:1.6px dashed #6E9BFF;background:rgba(110,155,255,.06)"></div>'
                                     '<div class="abs" style="left:166px;top:112px;width:9px;height:9px;border-radius:50%;background:#fff"></div>'
                                     + '<svg class="abs" style="left:0;top:0" width="509" height="372">' + dropA(370, 260, 370, 196, G, 'snap') + '</svg>', ti_f3, True)
                         + b2_full(g_sketch('sk-rectangle'), status7('sk-rectangle', 'Прямоугольник', 'Эскиз'), True, '107') + strip())
# F4. The extrude: the icon island, contours in block 1, a green ✓.
FIN['F4-extrude'] = (hotbar7() + b1_7b + view_full(MODEL_AT + PREVIEW, ti_7c)
                     + b2_full(G_MODEL.replace(g7x('СОЗДАТЬ', ['extrude', 'revolve', 'sweep', 'loft', 'coil']), g7('СОЗДАТЬ', ['extrude', 'revolve', 'sweep', 'loft', 'coil'], ('extrude',))),
                               status7('extrude', 'Выдавливание', '3D-модель'), False, '72') + strip()
                     + '<div class="abs tip" style="left:614px;top:62px">Насквозь</div>')
# F5. Вид.
FIN['F5-view'] = (hotbar7() + b1_tree(PART_TREE) + view_full(MODEL_AT, tabs_island(['3D-модель', 'Эскиз', 'Вид'], 'Вид'))
                  + b2_full(G_VIEW, status7('tree-part', 'Кронштейн', 'Вид')) + strip())
# F6. The assembly.
FIN['F6-assembly'] = (hotbar7() + b1_tree(ASM_TREE, 'Кронштейн:1') + view_full(ASM_MODEL, tabs_island(['Сборка', 'Вид'], 'Сборка'))
                      + b2_full(G_ASM, status7('tree-assembly', 'Узел крепления', 'Сборка')) + STRIP_ASM)
# F7–F9. Appearance: the tool, the library over block 1, «Создать цвет».
B2_LOOK = b2_full(G_VIEW.replace(g7x('ЦВЕТА', ['appearance', 'view-background']), g7('ЦВЕТА', ['appearance', 'view-background'], ('appearance',))),
                  status7('appearance', 'Внешний вид', 'Вид'))
FIN['F7-look'] = hotbar7() + b1_9a + view_9 + B2_LOOK + strip()
FIN['F8-look-library'] = hotbar7() + b1_9a + pop_b + view_9 + B2_LOOK + strip()
FIN['F9-look-create'] = hotbar7() + b1_9a + pop_c + view_9 + B2_LOOK + strip()
for k, v in FIN.items():
    pages[k] = page(v, FIN_CSS)

# F10. The cursor — «Линза», final: the states, and how it looks on the light theme too.
SIZES['F10-cursor'] = (1120, 330)
LIGHT_GRID = ('background-image:linear-gradient(#dfe3e8 1px,transparent 1px),linear-gradient(90deg,#dfe3e8 1px,transparent 1px);'
              'background-size:22px 22px;background-color:#f6f7f9')
cur_states = (f'<div class="cc w" style="left:12px;top:12px;width:740px;height:300px"><div class="t">Курсор «Линза» — состояния</div><div class="lineA"></div>'
              '<svg style="position:absolute;left:0;top:0" width="740" height="300">'
              + dropA(80, 200, 80, 136, B) + dropA(230, 200, 230, 118, G, 'snap') + dropA(380, 200, 380, 136, B, 'press')
              + dropA(530, 200, 530, 136, '#F5A524') + dropA(670, 200, 670, 136, '#8E959D')
              + '</svg>' + ''.join(f'<div class="st11" style="left:{x - 45}px;top:84px">{t}</div>' for x, t in
                                   [(80, 'покой'), (230, 'привязка к точке'), (380, 'нажата ✓'), (530, 'точный режим'), (670, 'курсор выкл.<br>(тап — выбор)')])
              + '<div class="d">Синий — работа, зелёный — привязка (значок формы цели), заливка — нажатие ✓, оранжевый — точный режим. '
                'Серый — подсказка, где был курсор, когда он выключен.</div></div>')
cur_light = (f'<div class="cc" style="left:764px;top:12px;width:344px;height:300px;{LIGHT_GRID};border-color:#d7dbe0">'
             '<div class="t" style="color:#1F2328">Светлая тема</div><div class="lineA" style="background:#e08a8d"></div>'
             '<svg style="position:absolute;left:0;top:0" width="344" height="300">' + dropA(110, 200, 110, 136, '#2F5CD0')
             + dropA(240, 200, 240, 118, '#1E9E57', 'snap') + '</svg>'
             '<div class="d" style="color:#4B5563">Тень и ореол отделяют курсор от чертежа и на светлом фоне.</div></div>')
pages['F10-cursor'] = page(cur_states + cur_light, D11_CSS + 'body{height:330px}')


if __name__ == '__main__':
    only = sys.argv[1:]
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={'width': 915, 'height': 412}, device_scale_factor=2)
        for name, html in pages.items():
            if only and name not in only: continue
            f = os.path.join(HERE, name + '.html')
            open(f, 'w').write(html)
            pg.set_viewport_size(dict(zip(('width', 'height'), SIZES.get(name, (915, 412)))))
            pg.goto('file://' + f)
            pg.wait_for_timeout(250)
            pg.screenshot(path=os.path.join(HERE, name + '.png'))
            print(name)
        b.close()

