#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/unbuilt-branches.sh [--gate <profile>,...] [--keep]
       tools/unbuilt-branches.sh --self-test

Lists what the gate never compiles in kernel/: the sources and the #if branches no gate
profile compiles, the objects no source builds, and the -D macros no source reads.

It copies the tree, marks every conditional branch under kernel/, then for each profile
asks build.sh what the kernel compiles (--print-compile-commands) and runs the
preprocessor on it: a branch is compiled by a profile when its mark comes out.

Every entry must be declared in tools/unbuilt-branches.declared with the reason the gate
does not compile it, and a declaration that matches no entry must be deleted.

  --gate a,b,...  the profiles the gate builds (default: server,client,plain)
  --keep          keep the working copy and the lists of each profile, and say where
  --self-test     check the marking on a fixture: comments, continuations, nesting

Profiles: server, client, plain, satellite, server-release, client-release, no-console,
azerty, multiboot2. All but plain need LplPlugin, LplAssistant and LplKnowledge next to
the tree, or at $<NAME>_ROOT (tools/deps.sh fetch).

Exit status: 0 when every entry is declared and every declaration matches one, 1 when
not, 2 when the siblings are missing, a profile cannot be configured, or one of its
sources does not preprocess.
EOF
}

REPOSITORY="$(git rev-parse --show-toplevel)"
DECLARED="$REPOSITORY/tools/unbuilt-branches.declared"

# Each profile: its name, the options build.sh takes for it, and the environment it needs.
# `plain` is what the continuous integration builds: no option, and no sibling.
PROFILES=(
    "server|--server|"
    "client|--client|"
    "plain||LPLPLUGIN_ROOT=none LPLASSISTANT_ROOT=none LPLKNOWLEDGE_ROOT=none"
    "satellite|--satellite|"
    "server-release|--server|SMOKE_TESTS=0"
    "client-release|--client|SMOKE_TESTS=0"
    "no-console|--server|KERNEL_CONSOLE=0"
    "azerty|--client --azerty|"
    "multiboot2|--server|MULTIBOOT_VERSION=2"
)
GATE="server,client,plain"
KEEP=0
SELF_TEST=0

while [ $# -gt 0 ]; do
    case "$1" in
        --gate) GATE="${2:?--gate needs a list of profiles}"; shift ;;
        --keep) KEEP=1 ;;
        --self-test) SELF_TEST=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done

WORK="$(mktemp -d)"
TREE="$WORK/tree"
cleanup() {
    if [ "$KEEP" -eq 1 ]; then
        echo "kept: $WORK" >&2
    else
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT

# Marks every branch of one file, in place under $TREE: after each #if, #ifdef, #ifndef,
# #elif and #else, once its continuation lines end, a string literal naming the file and the
# line. The preprocessor passes a string through untouched, so the mark comes out exactly
# when its branch is compiled. A directive inside a block comment is not one, and is left
# alone. Appends each mark to the index with its key: the directive, normalised, and for
# #elif and #else the #if they belong to.
mark_branches() {
    awk -v path="$1" -v index_file="$2" '
        function normalise(text) {
            gsub(/\\\n/, " ", text)
            gsub(/\/\*([^*]|\*+[^*\/])*\*+\//, " ", text)
            sub(/\/\/.*$/, "", text)
            gsub(/[ \t]+/, " ", text)
            sub(/^ ?# ?/, "#", text)
            sub(/ $/, "", text)
            return text
        }
        function comment_state(text, inside,    rest, opens, ends, slashes) {
            gsub(/"([^"\\]|\\.)*"/, "\"\"", text)
            gsub(/'"'"'([^'"'"'\\]|\\.)*'"'"'/, "'"''"'", text)
            rest = text
            while (rest != "") {
                if (inside) {
                    ends = index(rest, "*/")
                    if (ends == 0) return 1
                    rest = substr(rest, ends + 2)
                    inside = 0
                } else {
                    opens = index(rest, "/*")
                    slashes = index(rest, "//")
                    if (opens == 0 || (slashes != 0 && slashes < opens)) return 0
                    rest = substr(rest, opens + 2)
                    inside = 1
                }
            }
            return inside
        }
        function mark(    word, key) {
            key = normalise(directive)
            word = key
            sub(/^#/, "", word)
            sub(/[^a-z].*$/, "", word)
            if (word == "if" || word == "ifdef" || word == "ifndef") {
                opening[++depth] = key
            } else if (depth > 0) {
                key = key " of " opening[depth]
            }
            print "\"@branch " path ":" directive_line "\""
            printf "%s:%d\t%s\n", path, directive_line, key >> index_file
        }
        {
            print
            if (pending) {
                directive = directive "\n" $0
                if ($0 !~ /\\$/) { pending = 0; mark() }
                next
            }
            starts_in_comment = in_comment
            in_comment = comment_state($0, in_comment)
            if (starts_in_comment) next
            if ($0 ~ /^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|elifdef|elifndef|else)([^A-Za-z0-9_]|$)/) {
                directive = $0
                directive_line = NR
                if ($0 ~ /\\$/) { pending = 1; next }
                mark()
            } else if ($0 ~ /^[ \t]*#[ \t]*endif([^A-Za-z0-9_]|$)/ && depth > 0) {
                depth--
            }
        }
    ' "$TREE/$1" > "$TREE/$1.marked"
    mv "$TREE/$1.marked" "$TREE/$1"
}

# The fixture: a directive inside a block comment, a condition continued over three lines
# with a trailing comment, an #elif and an #else, a nested #ifdef, and a string holding "/*".
self_test() {
    local fixture="$TREE/kernel/fixture.c" failures=0 macros expected found
    mkdir -p "$TREE/kernel"
    cat > "$fixture" <<'FIXTURE'
#ifndef FIXTURE_H
#define FIXTURE_H
/* a comment that says
#ifdef NOT_A_DIRECTIVE
   and ends here */
#if defined(A) && \
    defined(B) && \
    !defined(D)   /* trailing comment */
int a_and_b;
#elif defined(C)
int c;
#  else // fallback
#ifdef NESTED
int nested;
#endif
#endif
const char *s = "/* not a comment";
#ifdef AFTER_STRING
int after;
#endif
#endif
FIXTURE
    : > "$WORK/fixture.index"
    mark_branches kernel/fixture.c "$WORK/fixture.index"
    expected="$(printf '%s\n' \
        $'kernel/fixture.c:1\t#ifndef FIXTURE_H' \
        $'kernel/fixture.c:6\t#if defined(A) && defined(B) && !defined(D)' \
        $'kernel/fixture.c:10\t#elif defined(C) of #if defined(A) && defined(B) && !defined(D)' \
        $'kernel/fixture.c:12\t#else of #if defined(A) && defined(B) && !defined(D)' \
        $'kernel/fixture.c:13\t#ifdef NESTED' \
        $'kernel/fixture.c:18\t#ifdef AFTER_STRING')"
    if [ "$(cat "$WORK/fixture.index")" != "$expected" ]; then
        echo "self-test: the index is wrong:"
        diff <(echo "$expected") "$WORK/fixture.index" || true
        failures=$((failures + 1))
    fi
    for case in "-DA -DB|1 6" "-DA -DB -DD|1 12" "-DC -DNESTED -DAFTER_STRING|1 10 18" "|1 12" "-DNESTED|1 12 13"; do
        macros="${case%%|*}"
        expected="${case#*|}"
        # shellcheck disable=SC2086
        found="$(cc -E -P $macros "$fixture" | grep -o '"@branch [^"]*"' | sed 's/.*://; s/"$//' | tr '\n' ' ')"
        if [ "${found% }" != "$expected" ]; then
            echo "self-test: with '${macros:-no macro}' the marks are '${found% }', expected '$expected'"
            failures=$((failures + 1))
        fi
    done
    if [ "$failures" -eq 0 ]; then echo "self-test: pass"; else echo "self-test: $failures failure(s)"; fi
    [ "$failures" -eq 0 ]
}

if [ "$SELF_TEST" -eq 1 ]; then
    self_test
    exit $?
fi

sibling_root() {
    local variable
    variable="$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')_ROOT"
    if [ -n "${!variable:-}" ]; then
        printf '%s' "${!variable}"
    else
        printf '%s' "$(cd "$REPOSITORY/.." && pwd)/$1"
    fi
}

export LPLPLUGIN_ROOT LPLASSISTANT_ROOT LPLKNOWLEDGE_ROOT
LPLPLUGIN_ROOT="$(sibling_root LplPlugin)"
LPLASSISTANT_ROOT="$(sibling_root LplAssistant)"
LPLKNOWLEDGE_ROOT="$(sibling_root LplKnowledge)"
for sibling in "$LPLPLUGIN_ROOT/core/include" "$LPLASSISTANT_ROOT/infer/include" "$LPLKNOWLEDGE_ROOT/knowledge/include"; do
    if [ ! -d "$sibling" ]; then
        echo "unbuilt-branches: $sibling not found; the profiles that link the siblings would come out plain (tools/deps.sh fetch)" >&2
        exit 2
    fi
done

mkdir -p "$TREE"
(cd "$REPOSITORY" && git ls-files -z --cached --others --exclude-standard |
    tar --null --files-from=- --ignore-failed-read -cf - 2>/dev/null) | tar -xf - -C "$TREE"

git -C "$REPOSITORY" ls-files --cached --others --exclude-standard -- \
    'kernel/*.c' 'kernel/*.h' 'kernel/*.S' 'kernel/*.s' 'kernel/*.inc' | sort -u > "$WORK/files"
: > "$WORK/branches"
while IFS= read -r file; do
    case "$file" in *.s) continue ;; esac
    if [ -f "$TREE/$file" ]; then mark_branches "$file" "$WORK/branches"; fi
done < "$WORK/files"
sort -u -o "$WORK/branches" "$WORK/branches"
grep -E '\.(c|S|s)$' "$WORK/files" | sort -u > "$WORK/sources"

# One profile: configure it in the copy, read what the kernel compiles, preprocess each
# source and keep the marks that come out. Writes the profile's marks, sources, objects with
# no source and -D macros.
preprocess_profile() {
    local name="$1" options="$2" environment="$3" commands cc c_flags asm_flags source flags status=0
    rm -rf "$TREE/sysroot"
    commands="$WORK/$name.commands"
    # shellcheck disable=SC2086
    if ! (cd "$TREE" && env $environment ./build.sh $options --print-compile-commands) \
        > "$commands" 2> "$WORK/$name.configure.log"; then
        echo "profile $name: build.sh cannot configure it, see $WORK/$name.configure.log" >&2
        return 2
    fi
    cc="$(sed -n 's/^cc: //p' "$commands")"
    cc="${cc#ccache }"
    c_flags="$(sed -n 's/^c-flags: //p' "$commands")"
    asm_flags="$(sed -n 's/^asm-flags: //p' "$commands")"
    sed -n 's/^source: /kernel\//p' "$commands" | sort -u > "$WORK/$name.sources"
    sed -n 's/^no-source: /kernel\//p' "$commands" | sort -u > "$WORK/$name.orphans"
    printf '%s\n%s\n' "$c_flags" "$asm_flags" | grep -oE -- '-D[A-Za-z_][A-Za-z0-9_]*' |
        sed 's/^-D//' | sort -u > "$WORK/$name.defines"
    : > "$WORK/$name.marks"
    while IFS= read -r source; do
        case "$source" in
            *.s) continue ;;
            *.S) flags="$asm_flags" ;;
            *) flags="$c_flags" ;;
        esac
        # shellcheck disable=SC2086
        if ! (cd "$TREE/kernel" && $cc -E $flags "${source#kernel/}") > "$WORK/preprocessed" \
            2>> "$WORK/$name.preprocess.log"; then
            echo "profile $name: $source does not preprocess, see $WORK/$name.preprocess.log" >&2
            status=2
        fi
        grep -o '"@branch [^"]*"' "$WORK/preprocessed" >> "$WORK/$name.marks" || true
    done < "$WORK/$name.sources"
    sed 's/^"@branch //; s/"$//' "$WORK/$name.marks" | sort -u -o "$WORK/$name.marks"
    return "$status"
}

failed=0
: > "$WORK/compiled"
: > "$WORK/gate.compiled"
: > "$WORK/orphans"
: > "$WORK/defines"
for profile in "${PROFILES[@]}"; do
    IFS='|' read -r name options environment <<< "$profile"
    preprocess_profile "$name" "$options" "$environment" || failed=2
    cat "$WORK/$name.marks" "$WORK/$name.sources" | sed "s/\$/\t$name/" >> "$WORK/compiled"
    sed "s/\$/\t$name/" "$WORK/$name.orphans" >> "$WORK/orphans"
    sed "s/\$/\t$name/" "$WORK/$name.defines" >> "$WORK/defines"
    if [[ ",$GATE," == *",$name,"* ]]; then
        cat "$WORK/$name.marks" "$WORK/$name.sources" >> "$WORK/gate.compiled"
    fi
done
sort -u -o "$WORK/gate.compiled" "$WORK/gate.compiled"

# The profiles that list an item, comma-separated, in the order of PROFILES.
profiles_of() {
    awk -F'\t' -v item="$1" '$1 == item { printf "%s%s", separator, $2; separator = "," }' "$2"
}

# The entries, one per line: the place, its key, the profiles that compile it (none when no
# profile does). A source the gate does not compile is one entry, and so is a file no profile
# reaches: one line for the file, not one per branch inside it.
: > "$WORK/entries"
while IFS= read -r source; do
    grep -qxF "$source" "$WORK/gate.compiled" && continue
    printf '%s\tfile\t%s\n' "$source" "$(profiles_of "$source" "$WORK/compiled")"
done < "$WORK/sources" >> "$WORK/entries"
cut -f1 "$WORK/compiled" | sed 's/:[0-9]*$//' | sort -u > "$WORK/reached.files"
cut -f1 "$WORK/branches" | sed 's/:[0-9]*$//' | sort -u |
    comm -23 - "$WORK/reached.files" | comm -23 - "$WORK/sources" |
    sed 's/$/\tfile\t/' >> "$WORK/entries"
cut -f1 "$WORK/entries" | sort -u > "$WORK/entry.files"
while IFS=$'\t' read -r place key; do
    grep -qxF "$place" "$WORK/gate.compiled" && continue
    grep -qxF "${place%:*}" "$WORK/entry.files" && continue
    printf '%s\t%s\t%s\n' "$place" "$key" "$(profiles_of "$place" "$WORK/compiled")"
done < "$WORK/branches" >> "$WORK/entries"
cut -f1 "$WORK/orphans" | sort -u | while IFS= read -r object; do
    printf '%s\tno source\t%s\n' "$object" "$(profiles_of "$object" "$WORK/orphans")"
done >> "$WORK/entries"
cut -f1 "$WORK/defines" | sort -u | while IFS= read -r define; do
    grep -rqw --include='*.[chS]' --include='*.inc' -- "$define" "$TREE/kernel" && continue
    printf 'kernel\t-D%s read by no source\t%s\n' "$define" "$(profiles_of "$define" "$WORK/defines")"
done >> "$WORK/entries"

# Declarations: "<path> | <key> | <why>". The path may be a shell pattern; the key is `file`,
# `no source`, `-D<MACRO> read by no source`, the directive as the report prints it, which
# covers every line of a matching file whose branch has that key, or `*`, any entry of the file.
: > "$WORK/declared"
if [ -f "$DECLARED" ]; then
    awk -F' [|] ' '!/^[[:space:]]*(#|$)/ && NF >= 3 { print $1 "\t" $2 }' "$DECLARED" | sort -u > "$WORK/declared"
fi

declaration_of() {
    local file="$1" key="$2" pattern declared_key
    while IFS=$'\t' read -r pattern declared_key; do
        # shellcheck disable=SC2053
        if [[ ( "$declared_key" == "$key" || "$declared_key" == "*" ) && "$file" == $pattern ]]; then
            printf '%s\t%s' "$pattern" "$declared_key"
            return 0
        fi
    done < "$WORK/declared"
    return 1
}

undeclared=0
: > "$WORK/declared.used"
report() {
    local title="$1" wanted="$2" place key profiles declaration
    local -a lines=()
    while IFS=$'\t' read -r place key profiles; do
        case "$wanted" in
            none) [ -z "$profiles" ] && [ "$key" != "no source" ] && [[ "$key" != -D* ]] || continue ;;
            elsewhere) [ -n "$profiles" ] && [ "$key" != "no source" ] && [[ "$key" != -D* ]] || continue ;;
            orphans) [ "$key" = "no source" ] || continue ;;
            defines) [[ "$key" == -D* ]] || continue ;;
        esac
        lines+=("$place"$'\t'"$key"$'\t'"$profiles")
    done < <(sort -t$'\t' -k1,1V "$WORK/entries")
    [ "${#lines[@]}" -eq 0 ] && return 0
    echo "$title"
    for line in "${lines[@]}"; do
        IFS=$'\t' read -r place key profiles <<< "$line"
        if declaration="$(declaration_of "${place%:*}" "$key")"; then
            echo "$declaration" >> "$WORK/declared.used"
            printf '  %-58s %s%s\n' "$place" "$key" "${profiles:+  [$profiles]}"
        else
            undeclared=$((undeclared + 1))
            printf '! %-58s %s%s\n' "$place" "$key" "${profiles:+  [$profiles]}"
        fi
    done
    echo
}

echo "Gate profiles: $GATE"
echo
report "Compiled by no profile:" none
report "Compiled only outside the gate, by the profiles in brackets:" elsewhere
report "Objects no source builds:" orphans
report "Macros a profile defines and no source reads:" defines

sort -u -o "$WORK/declared.used" "$WORK/declared.used"
stale="$(comm -23 "$WORK/declared" "$WORK/declared.used")"
if [ -n "$stale" ]; then
    echo "Declared, and no longer listed: delete these from tools/unbuilt-branches.declared"
    printf '%s\n' "$stale" | sed 's/\t/ | /; s/^/  /'
    echo
fi

stale_count=0
[ -z "$stale" ] || stale_count="$(printf '%s\n' "$stale" | wc -l)"
echo "$(wc -l < "$WORK/entries") entries, $undeclared undeclared (marked !), $stale_count stale declaration(s)."
[ "$failed" -eq 0 ] || exit "$failed"
[ "$undeclared" -eq 0 ] && [ "$stale_count" -eq 0 ] || exit 1
