# deduper
File De-duplication for Mac/Linux/*BSD

# Requires:
    g++ with support for -std=c++17
    make (or gmake)

# Status:
	The deduper utility is now usable to identify duplicate files. 

# Usage:

    $ ./deduper --help
    Usage: ./deduper [options] directories..

        -h, --help      Help info.
        -v, --verbose
        --version
        --threads n     Defaults to 4
        -s, --size n    Size >= n bytes
