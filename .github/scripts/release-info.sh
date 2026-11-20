#!/bin/bash
# Checks a release tag against the rpm metadata and writes the release notes.
#
#   .github/scripts/release-info.sh TAG NOTES_FILE
#
# - TAG must be v<Version> with Version from rpm/harbour-pocketssh.spec.
# - The newest entry in rpm/harbour-pocketssh.changes must be for that version.
# - NOTES_FILE gets the body of that newest entry (its "- ..." lines).
# Prints the version on stdout. Exits non-zero with a message if anything is off.
set -euo pipefail

tag=${1:?usage: release-info.sh TAG NOTES_FILE}
notes=${2:?usage: release-info.sh TAG NOTES_FILE}
cd "$(dirname "$0")/../.."
spec=rpm/harbour-pocketssh.spec
changes=rpm/harbour-pocketssh.changes

fail() { echo "::error::$*" >&2; exit 1; }

version=$(sed -n 's/^Version:[[:space:]]*//p' "$spec" | head -n1 | tr -d '[:space:]')
release=$(sed -n 's/^Release:[[:space:]]*//p' "$spec" | head -n1 | tr -d '[:space:]')
[ -n "$version" ] || fail "no Version: line in $spec"

if [ "$tag" != "v$version" ]; then
    fail "Tag '$tag' does not match the version in $spec (Version: $version). \
Either tag v$version, or bump Version in the spec (and add a $changes entry), commit, and tag again."
fi

# Newest entry: from the first "* " header line up to the next one.
# Lines starting with "#" are comments (sfdk/mb2 drops them too).
header=$(grep -m1 '^\* ' "$changes" || true)
[ -n "$header" ] || fail "no entries in $changes"
entry_ver=${header##* }          # last field, e.g. 1.0.0-1
if [ "${entry_ver%-*}" != "$version" ]; then
    fail "The newest entry in $changes is for $entry_ver, but the spec says $version-$release. \
Add a changelog entry for $version at the top of $changes."
fi

# Blank lines are held back until more text follows, so trailing ones are dropped.
awk '
    /^#/ { next }
    /^\* / { n++; next }
    n > 1 { exit }
    n == 1 && /^[[:space:]]*$/ { blank++; next }
    n == 1 { while (blank > 0) { print ""; blank-- } print }
' "$changes" > "$notes"
[ -s "$notes" ] || fail "the newest entry in $changes has no text"

echo "$version"
