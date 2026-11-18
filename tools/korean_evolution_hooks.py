"""2.335 companion evolution identity, scoped to the original success message."""


def install_evolution_hooks(at):
    indent = b'\t' * 5
    first_mode = indent + b'if ( evmode == 1 ) {\r\n' + indent + b'\t' + (
        'evname = lang("猫の王女", "EV")\r\n'.encode('cp932'))
    # Mode 101 overwrites cdata(214, tc) before the message. Species (27)
    # remains unchanged throughout all 167 branches.
    at(first_mode, indent + b'native_evolution_stage = cdata(214, tc)\r\n', 'evolution_prior_stage')
    success = indent + b'\t' + (
        'txt_select lang("おめでとう！" + cdatan(0, tc) + "は" + evname + "に進化した！", '
        '"Congratulations! Your friend has evolved!"), "", "", "", "", "", "", "", ""\r\n'.encode('cp932'))
    at(success, indent + b'\tnative_log_identity = "@evolution/" + str(evmode) + "/" + '
       b'str(cdata(27, tc)) + "/" + str(native_evolution_stage) + "\\t" + evold + "\\t" + cdatan(0, tc)\r\n',
       'evolution_success_begin')
    at(success, indent + b'\tnative_log_identity = ""\r\n', 'evolution_success_end', after=True)
