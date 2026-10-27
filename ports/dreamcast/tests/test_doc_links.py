"""Check relative documentation links without fetching remote URLs (AI-assisted)."""
from pathlib import Path
import re
import unittest
from urllib.parse import unquote, urlsplit

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parents[1]
DESTINATION = r'(?:<([^>]+)>|((?:\\.|\([^()]*\)|[^\s()])+))'


def relative_links(text):
    # Examples inside fenced/inline code are not Markdown links.
    text = re.sub(r'(?ms)^\s*(`{3,}|~{3,}).*?^\s*\1\s*$', '', text)
    text = re.sub(r'(`+)[^`\n]*?\1', '', text)
    destinations = [a or b for a, b in re.findall(r'!?\[[^\]\n]*\]\(\s*' + DESTINATION, text)]
    destinations += [a or b for a, b in re.findall(r'(?m)^\s*\[[^\]]+\]:\s*' + DESTINATION, text)]
    for destination in destinations:
        destination = re.sub(r'\\([() ])', r'\1', destination)
        parts = urlsplit(destination)
        if parts.scheme or parts.netloc or not parts.path:
            continue
        yield unquote(parts.path)


def broken_links():
    broken = []
    for path in [ROOT / 'README.md', *sorted(PORT.glob('*.md'))]:
        for link in relative_links(path.read_text(encoding='utf-8')):
            target = ROOT / link.lstrip('/') if link.startswith('/') else path.parent / link
            if not target.is_file():
                broken.append((path.relative_to(ROOT).as_posix(), link))
    return broken


class DocumentationLinksTest(unittest.TestCase):
    def test_relative_documentation_targets_exist(self):
        self.assertEqual(broken_links(), [])

    def test_link_syntax_anchors_remote_code_and_reference_definitions(self):
        text = '''[one](a.md#part) ![pic](images/pic.png)
        [space](<two%20words.md> "Title") [nested](f(a).md)
        [web](https://example.org/missing) [anchor](#here)
        ` [not a link](ignored.md) `
[ref]: reference.md#title
```
[example](also-ignored.md)
```
'''
        self.assertEqual(list(relative_links(text)),
                         ['a.md', 'images/pic.png', 'two words.md', 'f(a).md', 'reference.md'])
