#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Project rule: no AI attribution, in any form, in commit messages.
# usage: check-ai-attribution.sh FILE   (a commit message; exit 1 and report if it attributes an AI)
AI='(claude|anthropic|openai|chatgpt|gpt-[0-9]|copilot|gemini|codex|cursor|devin|llm|ai assistant|\bai\b|artificial intelligence|language model)'
bad=$(grep -n -i -E \
	-e "(co-authored-by|assisted-by|generated-by|helped-by|signed-off-by):.*$AI" \
	-e "(generated|written|created|authored|assisted|made) (with|by|using) .*$AI" \
	-e "noreply@(anthropic|openai)\.com|claude\.ai/code|claude-session:|chatgpt\.com" \
	-e "🤖" "$1" | grep -v '^[0-9]*:#')
if [ -n "$bad" ]; then
	echo "commit message rejected: this project allows no AI attribution (see CLAUDE.md, Conventions):" >&2
	echo "$bad" >&2
	exit 1
fi
exit 0
