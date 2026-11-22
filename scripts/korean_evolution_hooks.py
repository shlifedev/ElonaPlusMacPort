"""2.335 companion evolution identity, scoped to the original success message."""
import re


def chara_default_names(source):
    """Return {id: (japanese, english)} from the character DB name branches.

    The success hook calls refchara(id, 2, 1). Accept that call only while
    every dbmode 16 / dbspec 2 branch is a pure pair of literal returns, so
    the lookup has no effect other than the DB selector globals it restores.
    """
    lines = source.decode('cp932').splitlines()
    start, end = lines.index('*label_9484'), lines.index('*label_9485')
    names, current, mode16 = {}, None, False
    for i in range(start, end):
        command = lines[i].strip()
        match = re.fullmatch(r'if \( dbid == (\d+) \) \{', command)
        if match:
            current, mode16 = int(match[1]), False
        elif command == 'if ( dbmode == 16 ) {':
            mode16 = True
        elif command.startswith('if ( dbmode == '):
            mode16 = False
        elif command == 'if ( dbspec == 2 ) {':
            block = [line.strip() for line in lines[i + 1:i + 8]]
            ja = re.fullmatch(r'return "([^"\\]*)"', block[1])
            en = re.fullmatch(r'return "([^"\\]*)"', block[4])
            if (current is None or not mode16 or current in names or block[0] != 'if ( jp ) {' or
                    not ja or not en or block[2:4] != ['}', 'else {'] or block[5:7] != ['}', '}']):
                raise ValueError(f'2.335 character name branch changed at line {i + 1}')
            names[current] = (ja[1], en[1])
    if names.keys() != set(range(len(names))) or len(names) != 1165:
        raise ValueError('unexpected 2.335 character DB IDs')
    return names


def install_evolution_hooks(at, source):
    count = len(chara_default_names(source))
    indent = b'\t' * 5
    first_mode = indent + b'if ( evmode == 1 ) {\r\n' + indent + b'\t' + (
        'evname = lang("猫の王女", "EV")\r\n'.encode('cp932'))
    # Mode 101 overwrites cdata(214, tc) before the message. Species (27)
    # remains unchanged throughout all 167 branches.
    at(first_mode, indent + b'native_evolution_stage = cdata(214, tc)\r\n', 'evolution_prior_stage')
    success = indent + b'\t' + (
        'txt_select lang("おめでとう！" + cdatan(0, tc) + "は" + evname + "に進化した！", '
        '"Congratulations! Your friend has evolved!"), "", "", "", "", "", "", "", ""\r\n'.encode('cp932'))
    # English evold is the placeholder "aaaaaaa" in every mode, and a stage
    # mode's evold can differ from the saved name. An unnamed companion keeps
    # its DB name in cdatan(0, tc), so pass that name for the comparison.
    lines = [
        'native_evolution_dbmode = dbmode',
        'native_evolution_dbid = dbid',
        'native_evolution_dbspec = dbspec',
        'native_evolution_default = ""',
        f'if ( cdata(27, tc) >= 0 & cdata(27, tc) < {count} ) : '
        'native_evolution_default = refchara(cdata(27, tc), 2, 1)',
        'dbmode = native_evolution_dbmode',
        'dbid = native_evolution_dbid',
        'dbspec = native_evolution_dbspec',
        'native_log_identity = "@evolution/" + str(evmode) + "/" + str(cdata(27, tc)) + "/" + '
        'str(native_evolution_stage) + "\\t" + native_evolution_default + "\\t" + cdatan(0, tc)',
    ]
    at(success, b''.join(indent + b'\t' + line.encode() + b'\r\n' for line in lines), 'evolution_success_begin')
    at(success, indent + b'\tnative_log_identity = ""\r\n', 'evolution_success_end', after=True)
