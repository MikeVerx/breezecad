#!/usr/bin/env python3
"""Mockups of the phone UI (BreezeCAD Android), 915x412 dp, rendered to PNG."""
import os, re, sys
from playwright.sync_api import sync_playwright

ICONS = '/home/claude/breezecad/assets/icons'
HERE = os.path.dirname(os.path.abspath(__file__))


def icon(name, size=18, color='#AEB4BD', style=''):
    p = os.path.join(ICONS, name + '.svg')
    s = open(p).read()
    s = s.replace('currentColor', color)
    s = re.sub(r'<svg ', f'<svg width="{size}" height="{size}" style="flex:none;{style}" ', s, count=1)
    return s


CSS = """
@font-face{font-family:Plex;src:url(IBMPlexSans-Regular.ttf);font-weight:400}
@font-face{font-family:Plex;src:url(IBMPlexSans-Medium.ttf);font-weight:500}
@font-face{font-family:Plex;src:url(IBMPlexSans-SemiBold.ttf);font-weight:600}
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
    more = f'<div class="more">{icon("ui-chevron-right", 16, "#9db6ff")}</div>' if not opened else f'<div class="more" style="background:var(--acc);border-color:var(--acc)">{icon("ui-chevron-right", 16, "#fff", "transform:rotate(180deg)")}</div>'
    return f'<div class="cg"><div class="h">{title}</div><div class="r">{cells}{more}</div></div>'

b2c = f'''<div class="abs b2"><div class="brow" style="gap:8px;padding:0 10px"><span style="font-size:11px;color:#4CBB7F;font-family:monospace">60 fps</span><div class="search">{icon('ui-search', 14)}Поиск</div></div>
{cgroup('ЭСКИЗ', ['sketch', 'sk-project', 'work-plane'])}{cgroup('СОЗДАТЬ', ['extrude', 'revolve', 'hole'], True)}{cgroup('ИЗМЕНИТЬ', ['fillet', 'chamfer', 'shell'])}{cgroup('МАССИВ', ['pattern-rect', 'pattern-circular', 'mirror'])}
<div class="abs beard">Готово<div style="flex:1;height:3px;border-radius:2px;background:var(--field)"></div></div></div>'''
fly_items = [('extrude', 'Выдавить'), ('revolve', 'Вращение'), ('hole', 'Отверстие'), ('loft', 'По сечениям'), ('sweep', 'Сдвиг'), ('coil', 'Пружина'), ('thread', 'Резьба'), ('shell', 'Оболочка')]
fly = '<div class="abs fly">' + ''.join(f'<div class="ti{" on" if n == "extrude" else ""}">{icon(n, 26, "#fff")}{t}</div>' for n, t in fly_items) + \
      '<div class="abs" style="right:0;top:0;bottom:0;width:40px;background:linear-gradient(90deg,transparent,rgba(27,30,34,.97))"></div></div>'
pages['3-block2-compact'] = page(camera() + block1() + view() + b2c + strip() + fly +
    note('Компактный режим (по умолчанию): на группу — 4 равные кнопки,<br>ближняя к виду кнопка ›  открывает полосу поверх вида (не залезая на блок 1).<br>Полоса листается вбок; тап вне — закрыть.', 'left:240px;top:250px'),
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


if __name__ == '__main__':
    only = sys.argv[1:]
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={'width': 915, 'height': 412}, device_scale_factor=2)
        for name, html in pages.items():
            if only and name not in only: continue
            f = os.path.join(HERE, name + '.html')
            open(f, 'w').write(html)
            pg.goto('file://' + f)
            pg.wait_for_timeout(250)
            pg.screenshot(path=os.path.join(HERE, name + '.png'))
            print(name)
        b.close()

