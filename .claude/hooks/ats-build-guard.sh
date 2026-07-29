#!/usr/bin/env bash
# PreToolUse/Bash guard: redirect build, test, and format commands to the
# ats-build skill, which encodes project-specific requirements (ats-dev Docker
# container detection, branch-derived build directory) that raw cmake
# invocations skip.
#
# Escape hatch: prefix the command with ATS_BUILD=1 to acknowledge the skill's
# procedure is being followed and run the command anyway.

set -uo pipefail

payload=$(cat)
command=$(printf '%s' "$payload" | jq -r '.tool_input.command // empty')

if [[ -z $command ]]; then
  exit 0
fi

# Already acknowledged -- the skill (or the user) is driving.
if [[ $command == *ATS_BUILD=1* ]]; then
  exit 0
fi

matched=""

# Inspect the first word of each shell segment, so `cmake ...` as a command is
# caught while `grep -rn cmake src/` -- where cmake is merely an argument -- is
# not. Leading env assignments (FOO=1 cmake ...) are skipped. Wrapped forms such
# as `docker exec ats-dev cmake ...` pass through: that is the procedure the
# skill prescribes for docker mount mode.
while IFS= read -r segment; do
  read -r -a words <<<"$segment" || true
  for word in ${words[@]+"${words[@]}"}; do
    if [[ $word == *=* && $word != /* && $word != ./* ]]; then
      continue # env assignment prefix
    fi
    case ${word##*/} in
    cmake)
      matched="cmake"
      ;;
    ctest)
      matched="ctest"
      ;;
    autest.sh)
      matched="autest.sh"
      ;;
    clang-format | clang-format.sh | cmake-format.sh)
      matched="${word##*/}"
      ;;
    esac
    break # only the command word matters
  done
  [[ -n $matched ]] && break
done < <(printf '%s\n' "$command" | tr ';&|()' '\n')

if [[ -z $matched ]]; then
  exit 0
fi

jq -n --arg matched "$matched" '{
  hookSpecificOutput: {
    hookEventName: "PreToolUse",
    permissionDecision: "deny",
    permissionDecisionReason: (
      "Blocked: this is a \($matched) command. Use the ats-build skill for " +
      "builds, tests, and formatting instead of invoking the tool directly -- " +
      "the skill encodes project requirements that direct calls miss (checking " +
      "for the ats-dev Docker container, deriving the build directory from the " +
      "current branch, using the pinned clang-format via the format target). " +
      "Call: Skill(skill=\"ats-build\"). " +
      "If you are already following the skill'"'"'s procedure, re-run the command " +
      "with an ATS_BUILD=1 prefix to bypass this guard."
    )
  }
}'
