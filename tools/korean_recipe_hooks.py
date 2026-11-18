"""Scope only the original 2.335 blending window's display calls."""
from korean_inventory_hooks import catch_identity


def recipe_inserts(source):
    def area(first, last):
        start = source.index(first)
        return start, source[start:source.index(last, start)]

    panel = area(b'#deffunc window_recipe2\r\n', b'#deffunc clear_rprefmat')
    recipes = area(b'*label_5547\r\n\tif ( 1 == 0 )', b'*label_5550\r\n\tif ( p !=')
    logs = area(b'#deffunc clear_rprefmat', b'\r\n*label_5566\r\n')
    items = area(b'*label_5554\r\n\tif ( 1 == 0 )', b'*label_5557\r\n')

    def locate(region, command):
        base, data = region
        hits = [line for line in data.split(b'\r\n') if line.lstrip(b'\t') == command.encode('cp932')]
        if len(hits) != 1:
            raise ValueError(f'2.335 recipe display changed: {command}')
        line = hits[0] + b'\r\n'
        return base + data.index(line), line

    def draw(region, command, args, visible=None):
        start, line = locate(region, command)
        indent = line[:len(line)-len(line.lstrip(b'\t'))]
        if visible is None:
            visible = command.removeprefix('mes ').removeprefix('bmes ').removesuffix(', 235, 235, 235')
        parts = [*args, visible]
        payload = ' + "\\t" + '.join(parts)
        yield start, indent + ('native_ko_menu 28, ' + payload + '\r\n').encode('cp932'), 'recipe_begin'
        yield start+len(line), indent + b'native_ko_menu 0, ""\r\n', 'recipe_end'

    def insert(region, command, lines, after=False):
        start, line = locate(region, command)
        yield start + (len(line) if after else 0), (lines + '\r\n').encode('cp932'), 'recipe_capture'

    def identity(index):
        # Mirror the original inventory identity guards. An unidentified/custom
        # item must keep its displayed appearance even if it matches a UI label.
        item = 'inv(3, native_recipe_inventory)'
        return '\r\n'.join([
            'native_recipe_inventory = ' + index,
            'native_recipe_id = -1',
            'native_item_base = ""',
            'native_item_classifier = ""',
            f'if ( {item} >= 0 & {item} < maxitemid - 2 & {item} < length(ioriginalnameref) & {item} < length(iknownnameref) & inv(28, native_recipe_inventory) == 0 ) {{',
            f'if ( inv(8, native_recipe_inventory) > 0 | iknownnameref({item}) == ioriginalnameref({item}) ) {{',
            'native_recipe_id = ' + item,
            'native_item_base = ioriginalnameref(native_recipe_id)',
            'if ( en & native_recipe_id < length(ioriginalnameref2) ) : native_item_classifier = ioriginalnameref2(native_recipe_id)',
            'if ( native_recipe_id == 618 | native_recipe_id == 619 ) {',
            catch_identity('native_recipe_inventory', '').decode().rstrip(), '}', '}', '}'])

    recipe = ['rpname(rpid)', 'str(rpid * (rpdatan(rpid) == ""))']
    stepno = 'str(i@m195)'
    need = 'rpdata(20 + cnt, rpid)'
    extra = 'rpdata(40 + cnt, rpid)'
    meta = ['native_item_base', 'native_item_classifier', 'str(native_recipe_id)']
    yield from draw(panel, 'bmes lang("成功率　: ", "Success Rate : ") + s@m194, 235, 235, 235',
                    ['"rate"', 's@m194'])
    yield from draw(panel, 'bmes lang("必要時間: ", "Time: ") + s@m194, 235, 235, 235',
                    ['native_recipe_time', r'str(p@m194 \ 10000)', 'str(p@m194 / 10000)'])
    yield from insert(panel, 's@m194 = "" + p@m194 \\ 10000 + lang("ターン", " turns")',
                      'native_recipe_time = "time"\r\nif ( p@m194 >= 10000 ) : native_recipe_time = "hours"')
    yield from insert(panel, 's@m195(1) += strhint3', 'native_recipe_hint = "hint-close"')
    yield from insert(panel, 's@m195(1) += strhint3b', 'native_recipe_hint = "hint-back"')
    yield from draw(panel, 'mes s@m195(1)', ['native_recipe_hint', 'key_prev', 'key_next'])
    for command, key in [
            ('mes lang("調合の手順", "Blending Procedure")', 'procedure'),
            ('mes lang("必要なスキル:", "Required Skills:")', 'skills'),
            ('mes lang("必要な機材:", "Required equipment:")', 'equipment'),
            ('mes lang("注意：一度に交換できる金貨は合計9999枚までだ。", "Attention: Up to 9999 gold coins can be exchanged at a time.")', 'gold-limit'),
            ('mes lang("このアイテムは鑑定されていない。", "You haven\'t identified it yet.")', 'unidentified')]:
        yield from draw(panel, command, ['"'+key+'"'])
    yield from draw(panel, 'mes "" + i@m195 + "." + lang("レシピを選ぶ", "Choose a recipe")',
                    ['"choose"', stepno])
    yield from draw(panel, 'mes "" + i@m195 + "." + lang(rpname(rpid) + "のレシピを選んだ", "Chose the recipe of " + rpname(rpid))',
                    ['"chosen"', stepno, *recipe])
    yield from draw(panel, 'mes "" + i@m195 + "." + lang("調合を始める！", "Start blending!")',
                    ['"start"', stepno])
    yield from draw(panel, 'mes lang(rpname(rpid) + "のレシピ", "The recipe of " + rpname(rpid))',
                    ['"recipe"', *recipe])
    skill = 'rpdata(10 + cnt * 2, rpid)'
    value = 'rpdata(11 + cnt * 2, rpid)'
    yield from draw(panel, f'mes skillname({skill}) + "  " + {value} + "(" + sdata({skill}, 0) + ")"',
                    ['"skill"', f'skillname({skill})', f'str({value})', f'str(sdata({skill}, 0))'])
    add = 's@m195 = lang(rpmatname(cnt) + "を加える", "Add " + rpmatname(cnt) + "") + "(" + lang("所持:", "Stock:") + p@m195 + ")"'
    yield from insert(panel, add, 'native_recipe_line = "stock\\t" + i@m195 + "\\t" + rpmatname(cnt) + "\\t" + p@m195 + "\\t" + '+need+' + "\\t" + '+extra, True)
    chosen = 's@m195 = strmid(s@m195, 0, 44)'
    yield from insert(panel, chosen, identity('rpref(10 + cnt * 2)') + '\r\n' +
                      'native_recipe_chosen = strmid(s@m195, 9 * en, strlen(s@m195) - 9 * en - 8 * jp)\r\n' +
                      'native_recipe_line = "selected\\t" + i@m195 + "\\t" + native_recipe_chosen + "\\t" + native_item_base + "\\t" + native_item_classifier + "\\t" + native_recipe_id')
    yield from draw(panel, 'mes "" + i@m195 + "." + s@m195', ['native_recipe_line'])
    window = 'display_window (windoww - 780) / 2 + inf_screenx, winposy(445), 380, 432, 74'
    for region, column, key, count in [(recipes, 'レシピの名称', 'recipe-col', 'recipes'), (items, 'アイテムの名称', 'item-col', 'items')]:
        yield from draw(region, window, ['"title"'] if region is recipes else
                        ['"add"', 'rpmatname(step)', 'str(rpdata(20 + step, rpid))', 'str(rpdata(40 + step, rpid))'], 's')
        title = f'lang("{column}", "Name")'
        yield from draw(region, 'display_topic '+title+', wx + 28, wy + 30', ['"'+key+'"'], title)
        yield from draw(region, 'mes s', ['"'+count+'"', 'str(listmax)'])
    listcall = 'cs_list s, wx + 84, wy + 60 + cnt * 19 - 1, 19, 0, '
    yield from draw(recipes, listcall+'0, p', ['"recipe"', *recipe], 's')
    yield from insert(items, 's = strmid(s, 0, 28)', identity('p')+'\r\nnative_recipe_full_item = s')
    yield from insert(items, 's = strmid(s, 0, 28)',
                      'if ( cnt == cs ) {\r\nnative_recipe_preview = native_recipe_full_item\r\n'
                      'native_recipe_preview_fields = native_item_base + "\\t" + native_item_classifier + "\\t" + native_recipe_id\r\n}')
    yield from draw(panel, 'mes itemname(window_recipe_prm1)',
                    ['"item"', 'native_recipe_preview', 'native_recipe_preview_fields'], 'native_recipe_preview')
    yield from draw(items, listcall+'1, p', ['"item"', 'native_recipe_full_item', *meta], 's')

    # The normal log assembly already receives the completed original sentence.
    # Supply only the authored material/recipe identity here, so itemname() runs once.
    log_cases = [
        ('txt_select lang(rpmatname(step) + "を追加しよう。", "Add \\"" + rpmatname(step) + "\\"."), "", "", "", "", "", "", "", ""',
         'native_log_identity = "@recipe/add\\t" + rpmatname(step) + "\\t" + rpdata(20 + step, rpid) + "\\t" + rpdata(40 + step, rpid)'),
        ('txt_select lang(itemname(ci) + "を選んだ。", "You add " + itemname(ci) + "."), "", "", "", "", "", "", "", ""',
         identity('ci') + '\r\nnative_log_identity = "@recipe/added\\t" + native_item_base + "\\t" + native_item_classifier + "\\t" + native_recipe_id'),
    ]
    for command, context in log_cases:
        yield from insert(logs, command, context)
        yield from insert(logs, command, 'native_log_identity = ""', True)

    # rpmode is set exclusively around the recipe's final quantity prompt.
    prompt = area(b'\r\n*label_6185\r\n', b'\r\n*label_6189\r\n')
    prompt_draw = 'cs_list promptl(0, cnt), sx + 56, cnt * 20 + sy + 21, 19'
    yield from insert(prompt, prompt_draw, 'if ( rpmode ) {\r\n\tnative_ko_menu 28, "confirm" + cnt + "\\t" + promptl(0, cnt)\r\n}')
    yield from insert(prompt, prompt_draw, 'if ( rpmode ) {\r\n\tnative_ko_menu 0, ""\r\n}', True)

    # The original price function can initialise a trade rate. Observe its
    # return inside this draw instead of calling it again to obtain the price.
    gold = 'mes lang("交換金貨＋" + calcitemvalue(window_recipe_prm1, 3) + "枚。", "Add " + calcitemvalue(window_recipe_prm1, 3) + " gold.")'
    yield from insert(panel, gold, 'native_recipe_price_pending = 1')
    yield from insert(panel, gold, 'native_recipe_price_pending = 0\r\n\tnative_ko_menu 0, ""', True)
    start, data = area(b'#defcfunc calcitemvalue ', b'#defcfunc calcinvestnpc ')
    lines = list(data.splitlines(keepends=True))
    if sum(line.strip() == b'return value@m164' for line in lines) != 2:
        raise ValueError('2.335 price returns changed')
    for line in lines:
        if line.strip() == b'return value@m164':
            yield start, ('if ( native_recipe_price_pending ) {\r\n\tnative_ko_menu 28, "gold\\t" + value@m164 + "\\t" + '
                          'lang("交換金貨＋" + value@m164 + "枚。", "Add " + value@m164 + " gold.")\r\n}\r\n').encode('cp932'), 'recipe_price'
        start += len(line)
