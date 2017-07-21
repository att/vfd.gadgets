# tfm leaves a leading space on each line which must be stripped for .md
MARKDOWN=1 tfm README.xfm stdout|awk '{ print substr( $0, 2 ); }' >README.md

tfm README.xfm README
pfm README.xfm README.ps
