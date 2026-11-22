"""Bound text arguments to authored 2.335 story event branches."""

# The fifth home line has separate EN/JP assignments. Copy the exact VM field.
# The two templates with {alias} use the stock title vocabulary at draw time.
NAME_LINES = {
    373810: (373810, 'cdatan(0, 0)'),
    373856: (373856, 'cdatan(1, 0)'),
    374293: (374293, 'cdatan(0, 0)'),
    374319: (374319, 'cdatan(1, 0)'),
    418807: (418807, 'cdatan(0, 0)'),
    418874: (418807, 'cdatan(0, 0)'),
}


def story_inserts(source):
    lines = source.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))

    for line_num, (identity, variable) in NAME_LINES.items():
        line = lines[line_num - 1]
        if not line.strip().startswith(b'buff = ') or variable.encode() not in line:
            raise ValueError(f'home dialogue argument differs at {line_num}')
        capture = ('\tnative_dialog_original = buff\r\n'
                   f'\tnative_dialog_pending = "@home-name/{identity}/" + {variable}\r\n')
        yield offsets[line_num], capture.encode(), 'dialogue_argument'

    for line_num, event in [(614737, 38), (614803, 12)]:
        if b'Reunion with your pet' not in lines[line_num - 1]:
            raise ValueError(f'pet event title differs at {line_num}')
        end = next(i for i in range(line_num, line_num + 40)
                   if lines[i].strip() == b'gosub *label_5487')
        yield offsets[end], f'\tnative_story_event = {event}\r\n'.encode(), 'event_begin'
        yield offsets[end+1], b'\tnative_story_event = 0\r\n', 'event_end'
    # The normal Vernis reunion creates a fresh animal, then immediately recruits it.
    # The selection ID must be copied before characreate reuses the VM scratch variable p.
    if lines[614836 - 1].strip() != b'characreate -1, p, cdata(1, 0), cdata(2, 0)' or lines[614838 - 1].strip() != b'gosub *label_9492':
        raise ValueError('Vernis starter pet recruitment changed')
    yield offsets[614835], b'\t\tnative_starter_pet_id = p\r\n', 'starter_pet_begin'
    yield offsets[614838], b'\t\tnative_starter_pet_id = 0\r\n', 'starter_pet_end'
    # Original wrapping still sets window height, and the regular draw remains
    # available for uncatalogued events and untranslated launches.
    for line_num, command, key, x, y, width, shadow, rgb in [
        (282505, b'bmes q, 245, 235, 225', 'title', 'wx + 40', 'wy + 16', 'dx - 70', 1, '245, 235, 225'),
        (282509, b'mes buff', 'body', 'wx + 24', 'wy + ty + 20', 'dx - 48', 0, '30, 30, 30'),
    ]:
        if lines[line_num-1].strip() != command:
            raise ValueError(f'pet event draw differs at {line_num}')
        before = (f'\tif ( native_story_event != 0 ) {{\r\n'
                  f'\t\tnative_ko_gmes "@event/" + native_story_event + "/{key}", '
                  f'{x}, {y}, {width}, {shadow}, {rgb}\r\n'
                  f'\t\tif ( stat >= 0 ) {{\r\n\t\t\tgoto *native_event_{key}_done\r\n\t\t}}\r\n\t}}\r\n')
        yield offsets[line_num-1], before.encode(), 'event_' + key
        yield offsets[line_num], f'*native_event_{key}_done\r\n'.encode(), 'event_draw_end'
