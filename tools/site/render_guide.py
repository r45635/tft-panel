"""Renders docs/USER_GUIDE.md into the site's guide page (run by tools/site.sh)."""
import sys

import markdown

md = open("docs/USER_GUIDE.md", encoding="utf-8").read()
# The guide links the installer by its public URL (for GitHub); on the site it is a sibling page.
md = md.replace("https://r45635.github.io/tft-panel/install.html", "install.html")
html = markdown.markdown(md, extensions=["tables", "toc"])
page = open("tools/site/page.html", encoding="utf-8").read()
out = page.replace("{{TITLE}}", "TFT Panel - user guide").replace("{{CONTENT}}", html)
out = out.replace("{{VERSION}}", sys.argv[2] if len(sys.argv) > 2 else "")
open(sys.argv[1], "w", encoding="utf-8").write(out)
