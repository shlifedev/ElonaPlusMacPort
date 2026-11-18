"""Read the completed journal and save headers only at their 2.335 display sites."""
JOURNAL_DRAWS = (
    (205433, 'mes cnven(strmid(mdatan(0), 0, 16 - (maplevel() != "") * 4))', 19, 'mdatan(0)'),
    (205436, 'mes cnven(mdatan(0))', 19, 'mdatan(0)'),
    (289854, 'gcopy 4, 0, 0, 736, 448', 13, 'buff'),
    (289897, 'mes s', 15, 'str(p) + "/" + s'),
    (289906, 'mes "(more)"', 4, '"(more)"'),
    (308706, 'cs_list listn(1, cnt), x + 48, y + 8, 19', 14, 'listn(1, cnt)'),
    (308714, 'mes "No save files found"', 4, '"No save files found"'),
    (308847, 'cs_list listn(1, cnt), x + 48, y + 8, 19', 14, 'listn(1, cnt)'),
    (308855, 'mes "No gene files found"', 4, '"No gene files found"'),
)
