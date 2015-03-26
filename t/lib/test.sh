# Copyright (c) 2005 Junio C Hamano
# Copyright (c) 2011-2012 Mathias Lafeldt
# Copyright (c) 2014-2015 Maxim Bublis

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see http://www.gnu.org/licenses/.

# Keep the original TERM for say_color
ORIGINAL_TERM=$TERM

# For repeatability, reset the environment to known value.
LANG=C
LC_ALL=C
PAGER=cat
TZ=UTC
TERM=dumb
EDITOR=:
export EDITOR LANG LC_ALL PAGER TERM TZ
unset VISUAL CDPATH GREP_OPTIONS

[ "x$ORIGINAL_TERM" != "xdumb" ] && (
		TERM=$ORIGINAL_TERM &&
		export TERM &&
		[ -t 1 ] &&
		tput bold >/dev/null 2>&1 &&
		tput setaf 1 >/dev/null 2>&1 &&
		tput sgr0 >/dev/null 2>&1
	) &&
	color=t

while test "$#" -ne 0; do
	case "$1" in
	-d|--debug)
		debug=t; shift ;;
	-i|--immediate)
		immediate=t; shift ;;
	-h|--help)
		help=t; shift ;;
	-v|--verbose)
		verbose=t; shift ;;
	-q|--quiet)
		# Ignore --quiet under a TAP::Harness. Saying how many tests
		# passed without the ok/not ok details is always an error.
		test -z "$HARNESS_ACTIVE" && quiet=t; shift ;;
	--no-color)
		color=; shift ;;
	--root=*)
		root=$(expr "z$1" : 'z[^=]*=\(.*\)')
		shift ;;
	*)
		echo "error: unknown test option '$1'" >&2; exit 1 ;;
	esac
done

if test -n "$color"; then
	say_color() {
		(
		TERM=$ORIGINAL_TERM
		export TERM
		case "$1" in
		error)
			tput bold; tput setaf 1;; # bold red
		skip)
			tput setaf 4;; # blue
		warn)
			tput setaf 3;; # brown/yellow
		pass)
			tput setaf 2;; # green
		info)
			tput setaf 6;; # cyan
		*)
			test -n "$quiet" && return;;
		esac
		shift
		printf "%s" "$*"
		tput sgr0
		echo
		)
	}
else
	say_color() {
		test -z "$1" && test -n "$quiet" && return
		shift
		printf "%s\n" "$*"
	}
fi

error() {
	say_color error "error: $*"
	EXIT_OK=t
	exit 1
}

say() {
	say_color info "$*"
}

test -n "$test_description" || error "Test script did not set test_description."

if test "$help" = "t"; then
	printf '%s\n' "$test_description"
	exit 0
fi

exec 5>&1
exec 6<&0
if test "$verbose" = "t"; then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

test_failure=0
test_count=0
test_fixed=0
test_broken=0
test_success=0

die() {
	code=$?
	if test -n "$EXIT_OK"; then
		exit $code
	else
		echo >&5 "FATAL: unexpected exit with code $code"
		exit 1
	fi
}

EXIT_OK=
trap 'die' EXIT

match_pattern_list_() {
	arg="$1"
	shift
	test -z "$*" && return 1
	for pattern_; do
		case "$arg" in
		$pattern_)
			return 0
		esac
	done
	return 1
}

match_test_selector_list_() {
	title="$1"
	shift
	arg="$1"
	shift
	test -z "$1" && return 0

	# Both commas and whitespace are accepted as separators.
	OLDIFS=$IFS
	IFS=' 	,'
	set -- $1
	IFS=$OLDIFS

	# If the first selector is negative we include by default.
	include=
	case "$1" in
		!*) include=t ;;
	esac

	for selector; do
		orig_selector=$selector

		positive=t
		case "$selector" in
			!*)
				positive=
				selector=${selector##?}
				;;
		esac

		test -z "$selector" && continue

		case "$selector" in
			*-*)
				if expr "z${selector%%-*}" : "z[0-9]*[^0-9]" >/dev/null; then
					echo "error: $title: invalid non-numeric in range" \
						"start: '$orig_selector'" >&2
					exit 1
				fi
				if expr "z${selector#*-}" : "z[0-9]*[^0-9]" >/dev/null; then
					echo "error: $title: invalid non-numeric in range" \
						"end: '$orig_selector'" >&2
					exit 1
				fi
				;;
			*)
				if expr "z$selector" : "z[0-9]*[^0-9]" >/dev/null; then
					echo "error: $title: invalid non-numeric in test" \
						"selector: '$orig_selector'" >&2
					exit 1
				fi
		esac

		# Short cut for "obvious" cases
		test -z "$include" && test -z "$positive" && continue
		test -n "$include" && test -n "$positive" && continue

		case "$selector" in
			-*)
				if test $arg -le ${selector#-}; then
					include=$positive
				fi
				;;
			*-)
				if test $arg -ge ${selector%-}; then
					include=$positive
				fi
				;;
			*-*)
				if test ${selector%%-*} -le $arg && test $arg -le ${selector#*-}; then
					include=$positive
				fi
				;;
			*)
				if test $arg -eq $selector; then
					include=$positive
				fi
				;;
		esac
	done

	test -n "$include"
}

test_set_prereq_() {
	satisfied_prereq="$satisfied_prereq$1 "
}
satisfied_prereq=" "

test_have_prereq_() {
	# prerequisites can be concatenated with ','
	save_IFS=$IFS
	IFS=,
	set -- $*
	IFS=$save_IFS

	total_prereq=0
	ok_prereq=0
	missing_prereq=

	for prerequisite; do
		case "$prerequisite" in
		!*)
			negative_prereq=t
			prerequisite=${prerequisite#!}
			;;
		*)
			negative_prereq=
		esac

		total_prereq=$(($total_prereq + 1))
		case "$satisfied_prereq" in
		*" $prerequisite "*)
			satisfied_this_prereq=t
			;;
		*)
			satisfied_this_prereq=
		esac

		case "$satisfied_this_prereq,$negative_prereq" in
		t,|,t)
			ok_prereq=$(($ok_prereq + 1))
			;;
		*)
			# Keep a list of missing prerequisites; restore
			# the negative marker if necessary.
			prerequisite=${negative_prereq:+!}$prerequisite
			if test -z "$missing_prereq"; then
				missing_prereq=$prerequisite
			else
				missing_prereq="$prerequisite,$missing_prereq"
			fi
		esac
	done

	test $total_prereq = $ok_prereq
}

test_ok_() {
	test_success=$(($test_success + 1))
	say_color "" "ok $test_count - $@"
}

test_failure_() {
	test_failure=$(($test_failure + 1))
	say_color error "not ok $test_count - $1"
	shift
	printf '%s\n' "$*" | sed -e 's/^/#	/'
	test "$immediate" = "" || { EXIT_OK=t; exit 1; }
}

test_known_broken_ok_() {
	test_fixed=$(($test_fixed + 1))
	say_color error "ok $test_count - $@ # TODO known breakage vanished"
}

test_known_broken_failure_() {
	test_broken=$(($test_broken + 1))
	say_color warn "not ok $test_count - $@ # TODO known breakage"
}

test_debug() {
	test "$debug" = "" || eval "$1"
}

test_eval_() {
	# This is a separate function because some tests use
	# "return" to end a test_expect_success block early.
	eval </dev/null >&3 2>&4 "$*"
}

test_run_() {
	test_cleanup=:
	expecting_failure=$2
	test_eval_ "$1"
	eval_ret=$?

	if test -z "$immediate" || test $eval_ret = 0 || test -n "$expecting_failure"; then
		test_eval_ "$test_cleanup"
	fi
	if test "$verbose" = "t" && test -n "$HARNESS_ACTIVE"; then
		echo ""
	fi
	return "$eval_ret"
}

test_start_() {
	test_count=$(($test_count + 1))
}

test_finish_() {
	echo >&3 ""
}

test_skip_() {
	to_skip=
	skipped_reason=
	if match_pattern_list_ $this_test.$test_count $SKIP_TESTS; then
		to_skip=t
		skipped_reason="SKIP_TESTS"
	fi
	if test -z "$to_skip" && test -n "$test_prereq" && ! test_have_prereq_ "$test_prereq"; then
		to_skip=t

		of_prereq=
		if test "$missing_prereq" != "$test_prereq"; then
			of_prereq=" of $test_prereq"
		fi
		skipped_reason="missing $missing_prereq${of_prereq}"
	fi
	if test -z "$to_skip" && test -n "$run_list" && ! match_test_selector_list_ '--run' $test_count "$run_list"; then
		to_skip=t
		skipped_reason="--run"
	fi

	case "$to_skip" in
	t)
		say_color skip >&3 "skipping test: $@"
		say_color skip "ok $test_count # skip $1 ($skipped_reason)"
		: true
		;;
	*)
		false
		;;
	esac
}

# Public functions

test_expect_success() {
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	error "bug in the test script: not 2 or 3 parameters to test_expect_success"
	export test_prereq
	if ! test_skip_ "$@"; then
		say >&3 "expecting success: $2"
		if test_run_ "$2"; then
			test_ok_ "$1"
		else
			test_failure_ "$@"
		fi
	fi
	test_finish_
}

test_expect_failure() {
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	error "bug in the test script: not 2 or 3 parameters to test_expect_failure"
	export test_prereq
	if ! test_skip_ "$@"; then
		say >&3 "checking known breakage: $2"
		if test_run_ "$2" expecting_failure; then
			test_known_broken_ok_ "$1"
		else
			test_known_broken_failure_ "$1"
		fi
	fi
	test_finish_
}

test_must_fail() {
	"$@"
	exit_code=$?
	if test $exit_code = 0; then
		echo >&2 "test_must_fail: command succeeded: $*"
		return 1
	elif test $exit_code -gt 129 && test $exit_code -le 192; then
		echo >&2 "test_must_fail: died by signal: $*"
		return 1
	elif test $exit_code = 127; then
		echo >&2 "test_must_fail: command not found: $*"
		return 1
	fi
	return 0
}

test_might_fail() {
	"$@"
	exit_code=$?
	if test $exit_code -gt 129 && test $exit_code -le 192; then
		echo >&2 "test_might_fail: died by signal: $*"
		return 1
	elif test $exit_code = 127; then
		echo >&2 "test_might_fail: command not found: $*"
	fi
	return 0
}

test_expect_code() {
	want_code=$1
	shift
	"$@"
	exit_code=$?
	if test $exit_code = $want_code; then
		return 0
	fi

	echo >&2 "test_expect_code: command exited with $exit_code, we wanted $want_code $*"
	return 1
}

test_cmp() {
	${TEST_CMP:-diff -u} "$@"
}

test_done() {
	EXIT_OK=t

	if test "$test_fixed" != 0; then
		say_color error "# $test_fixed known breakage(s) vanished; please update test(s)"
	fi
	if test "$test_broken" != 0; then
		say_color warn "# still have $test_broken known breakage(s)"
	fi
	if test "$test_broken" != 0 || test "$test_fixed" != 0; then
		test_remaining=$(($test_count - $test_broken - $test_fixed))
		msg="remaining $test_remaining test(s)"
	else
		test_remaining=$test_count
		msg="$test_count test(s)"
	fi

	case "$test_failure" in
	0)
		# Maybe print SKIP message
		if test -n "$skip_all" && test $test_count -gt 0; then
			error "Can't use skip_all after running some tests"
		fi
		[ -z "$skip_all" ] || skip_all=" # SKIP $skip_all"

		if test $test_remaining -gt 0; then
			say_color pass "# passed all $msg"
		fi
		say "1..$test_count$skip_all"

		test -d "$remove_trash" &&
		cd "$(dirname "$remove_trash")" &&
		rm -rf "$(basename "$remove_trash")"

		exit 0 ;;
	*)
		say_color error "# failed $test_failure among $msg"
		say "1..$test_count"

		exit 1 ;;
	esac
}

: ${TEST_DIR:=$(pwd)}
export TEST_DIR

: ${BUILD_DIR:="$TEST_DIR/.."}
PATH="$BUILD_DIR:$PATH"
export PATH BUILD_DIR

TEST_FILE="$0"
export TEST_FILE

# Prepare test area.
test_dir="trash.$(basename "$TEST_FILE")"
test -n "$root" && test_dir="$root/$test_dir"

case "$test_dir" in
/*) TRASH_DIR="$test_dir" ;;
 *) TRASH_DIR="$TEST_DIR/$test_dir" ;;
esac

test "$debug" = "t" || remove_trash="$TRASH_DIR"
rm -rf "$test_dir" || {
    EXIT_OK=t
    echo >&5 "FATAL: Cannot prepare test area"
    exit 1
}

export TRASH_DIR

HOME="$TRASH_DIR"
export HOME

mkdir -p "$test_dir" || exit 1
# Use -P to resolve symlinks in our working directory so that the cwd
# in subprocesses like git equals our $PWD (for pathname comparisons).
cd -P "$test_dir" || exit 1

this_test=${TEST_FILE##*/}

for skp in $SKIP_TESTS; do
    case "$this_test" in
    $skp)
        say_color info >&3 "skipping test $this_test altogether"
        skip_all="skip all tests in $this_test"
        test_done
    esac
done
