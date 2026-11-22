"""Insert display-only item identity at the two 2.335 inventory row draws."""


def catch_identity(index, indent):
    # 618/619 have empty base names. inv(23) indexes the original fishdatan
    # in both languages. Check the stored type as well as the array bounds.
    lines = [
        'native_catch_index = inv(23, ' + index + ')',
        'if ( native_catch_index > 0 & native_catch_index < length(fishdatan) ) {',
        '    if ( fishdata(7, native_catch_index) == inv(3, ' + index + ') ) {',
        '        native_item_base = fishdatan(native_catch_index)',
        '        native_item_classifier = str(native_catch_index)',
        '    }',
        '}',
    ]
    return ''.join(indent + line + '\r\n' for line in lines).encode()


def install_item_hooks(at):
    inventory = b"\t\tcs_list s, wx + 84, wy + 60 + cnt * 19 - 1, 19, 0, 1, p\r\n"
    inventory += b"\t\tpos wx + 600 - strlen(s(1)) * 7, wy + 60 + cnt * 19 + 2\r\n"
    equipped = b"\t\tcs_list s, wx + 140 - 26, wy + 60 + cnt * 19 - 1, 19, 30, p(1) != (-1), p(1)\r\n"
    def identify(index, on_known):
        # Never read an empty equipment slot. The game itself already built s
        # with itemname; read just this item's original name without re-running it.
        item = b"inv(3, " + index + b")"
        capture = (b"\t\tif ( " + index + b" >= 0 ) {\r\n"
                   b"\t\t\tif ( " + item + b" >= 0 & " + item + b" < maxitemid - 2 & " +
                   item + b" < length(ioriginalnameref) & " + item + b" < length(iknownnameref) & inv(28, " + index + b") == 0 ) {\r\n"
                   b"\t\t\t\tif ( inv(8, " + index + b") > 0 | iknownnameref(" + item +
                   b") == ioriginalnameref(" + item + b") ) {\r\n"
                   # The JP definitions never populate the English classifier array.
                   b"\t\t\t\t\tnative_item_classifier = \"\"\r\n"
                   b"\t\t\t\t\tif ( en & " + item + b" < length(ioriginalnameref2) ) {\r\n"
                   b"\t\t\t\t\t\tnative_item_classifier = ioriginalnameref2(" + item + b")\r\n"
                   b"\t\t\t\t\t}\r\n"
                   b"\t\t\t\t\tnative_item_base = ioriginalnameref(" + item + b")\r\n"
                   b"\t\t\t\t\tif ( " + item + b" == 618 | " + item + b" == 619 ) {\r\n" +
                   catch_identity(index.decode(), "\t\t\t\t\t\t") +
                   b"\t\t\t\t\t}\r\n"
                   b"\t\t\t\t\t" + on_known + b"\r\n"
                   b"\t\t\t\t}\r\n\t\t\t}\r\n\t\t}\r\n")
        return capture

    for line, index in [(inventory, b"p"), (equipped, b"p(1)")]:
        draw = b"native_ko_item s, native_item_base, native_item_classifier, inv(3, " + index + b")"
        at(line, identify(index, draw), "inventory_item_label")

    detail = b"\tdisplay_topic itemname(ci), wx + 28, wy + 34\r\n"
    at(detail, b"\tnative_detail_active = 0\r\n" +
       identify(b"ci", b"native_detail_active = 1"), "item_detail_begin")
    at(detail, b"\tnative_detail_active = 0\r\n", "item_detail_end", after=True)
    at(b"\tmes display_topic_prm0\r\n",
       b"\tif ( native_detail_active ) {\r\n"
       b"\t\tnative_ko_item display_topic_prm0, native_item_base, native_item_classifier, inv(3, ci)\r\n"
       b"\t}\r\n", "item_detail_label")
    detail_line = b"\t\tmes listn(0, p)\r\n\t\tif ( list(0, p) > 0 ) {\r\n"
    at(detail_line, b'\t\tif ( list(0, p) >= 0 ) {\r\n'
       b'\t\t\tnative_ko_item listn(0, p), "", "detail", -1\r\n'
       b'\t\t}\r\n', "item_detail_line")

    material_name = b"\t\tcs_list s, wx + 96, wy + 66 + cnt * 19 - 1, 19, 0, 0\r\n\t\ts = matdesc(i)\r\n"
    at(material_name, b'\t\tnative_ko_item s, matname(i), str(i), -2\r\n', "gather_name")
    material_desc = b"\t\tmes s\r\n\t\tp(1) = matref(2, i)\r\n"
    at(material_desc, b'\t\tnative_ko_item s, matdesc(i), str(i), -3\r\n', "gather_description")
