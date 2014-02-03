#!/usr/bin/env python3
import sys

for file in sys.argv[1:]:
    for line in open(file, encoding='utf-8'):
        if line.strip():
            try:
                id, title, body = line.split('\t', 3)
            except ValueError as e:
                print(line, file=sys.stderr)
                continue
            id = id.strip()
            print('<div id=' + id + '>')
            print('<h1>' + title.strip())
            print('<br/><span style="font-size:0.5em;"><a href="http://www.ncbi.nlm.nih.gov/pubmed/' + id + '">' + id + '</a></span>')
            print('</h1>')
            print('<p>' + body.strip() + '</p>')
            print('</div>')
