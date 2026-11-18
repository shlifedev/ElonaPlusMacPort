"""Draw the 2.335 altar description after the original VM builds its complete buffer."""


def install_altar_hooks(at):
    assembled = b'\tgmestype = 1\r\n\tgosub *label_5441\r\n'
    at(assembled, b'\tnative_ko_gmes "@altar/" + str(inv(25, ci)), gmesx, gmesy, gmesw, gmestype, gmescol, gmescol(1), gmescol(2)\r\n'
       b'\tif ( stat >= 0 ) {\r\n\t\tgmesy += stat\r\n\t\tgoto *native_altar_body_done\r\n\t}\r\n',
       'altar_paragraph', after=True)
    at(assembled + b'\tgmes buff\r\n', b'*native_altar_body_done\r\n', 'altar_end', after=True)
    assign = b'\t\ts = listn(0, p)\r\n'
    draw = b'\t\tcs_list s, wx + 80, wy + dy + cnt * 20 - listmax * 20 - 18, 19\r\n'
    at(b'\t\tdisplay_key wx + 50, wy + dy + cnt * 20 - listmax * 20 - 18, cnt\r\n' + assign,
       b'\t\tif ( i == 2 ) {\r\n\t\t\tnative_ko_menu 20, "@altar/cancel\\t" + s\r\n\t\t}\r\n',
       'altar_cancel_begin', after=True)
    at(assign + draw, b'\t\tnative_ko_menu 0, ""\r\n', 'altar_cancel_end', after=True)
