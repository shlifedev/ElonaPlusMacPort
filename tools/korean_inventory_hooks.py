"""Insert display-only item identity at the two 2.335 inventory row draws."""


def install_item_hooks(at):
    inventory = b"\t\tcs_list s, wx + 84, wy + 60 + cnt * 19 - 1, 19, 0, 1, p\r\n"
    inventory += b"\t\tpos wx + 600 - strlen(s(1)) * 7, wy + 60 + cnt * 19 + 2\r\n"
    equipped = b"\t\tcs_list s, wx + 140 - 26, wy + 60 + cnt * 19 - 1, 19, 30, p(1) != (-1), p(1)\r\n"
    for line, index in [(inventory, b"p"), (equipped, b"p(1)")]:
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
                   b"\t\t\t\t\tnative_ko_item s, ioriginalnameref(" + item + b"), native_item_classifier, " +
                   item + b"\r\n"
                   b"\t\t\t\t}\r\n\t\t\t}\r\n\t\t}\r\n")
        at(line, capture, "inventory_item_label")
