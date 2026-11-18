"""Select the 2.335 option's display context without changing its configured value."""
def option_inserts(source):
    anchors = {
        607321: ('if ( mode == 1 ) {', '18, "label"'),
        607387: ('cs_list s, wx + 56 + x, wy + 66 + cnt * 19 - 1, 19, 0', '18, "label"'),
        607400: ('if ( submenu == 1 ) {', '18, "" + submenu + "/" + cnt'),
        607770: ('if ( submenu != 0 ) {', '18, "label"'),
        607794: ('if ( keyrange != 0 ) {', '0, ""'),
    }
    offset = 0
    for number, line in enumerate(source.splitlines(keepends=True), 1):
        if number in anchors:
            expected, context = anchors[number]
            if line.strip() != expected.encode('ascii'):
                raise ValueError(f'options draw changed at line {number}')
            indent = line[:len(line) - len(line.lstrip(b'\t'))]
            yield offset, indent + f'native_ko_menu {context}\r\n'.encode(), 'option_context'
        offset += len(line)
        if number == 607794:
            break
