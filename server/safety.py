"""
Forj Child Safety – heuristic grooming detection.

Analyzes direct messages sent between a verified adult and a verified minor.
When the cumulative severity score for a conversation reaches FREEZE_THRESHOLD,
the conversation is frozen and both parties are notified.

Pattern categories (all case-insensitive):
  age_probe         – probing the other user's age/grade/school
  secrecy           – requests for secrecy / isolation from parents
  meetup            – soliciting an in-person meeting or sharing location
  platform_move     – attempting to migrate to an unmonitored platform
  flattery          – age-incongruent flattery ("mature for your age", etc.)
  explicit_request  – requests for photos/nudity
"""

import re
from dataclasses import dataclass

# ---------------------------------------------------------------------------
# Pattern definitions: (compiled_regex, category_name, severity_score)
# Severity: 1 = low concern, 2 = moderate, 3 = high
# ---------------------------------------------------------------------------

_PATTERNS: list[tuple[re.Pattern, str, int]] = [
    # Age / grade probing
    (re.compile(
        r"\b(how old are you|what(\'?s| is) your age|what grade|"
        r"are you in (school|middle school|high school|elementary)|"
        r"how (many years|old)\b)",
        re.IGNORECASE,
    ), "age_probe", 1),

    # Secrecy / parent isolation
    (re.compile(
        r"\b(don\'?t tell (your )?(parents?|mom|dad|anyone|anyone else)|"
        r"keep (this|it|our) (secret|between us|to yourself)|"
        r"our (little )?secret|"
        r"parents? (don\'?t|can\'?t|shouldn\'?t|won\'?t|must not) (know|find out|see)|"
        r"no one (needs?|has?) to know|just between (us|you and me))\b",
        re.IGNORECASE,
    ), "secrecy", 2),

    # Meet-up / location solicitation
    (re.compile(
        r"\b(meet (up|in person|irl|somewhere|you|sometime)|"
        r"come (over|to my (place|house|apartment|car))|"
        r"where do you live|what(\'?s| is) your address|"
        r"pick you up|i\'?ll (come get|drive) you|"
        r"hang out (alone|together|just us))\b",
        re.IGNORECASE,
    ), "meetup", 3),

    # Platform migration to avoid monitoring
    (re.compile(
        r"\b(my (snapchat|kik|telegram|whatsapp|instagram|tiktok|signal)|"
        r"(add|follow|dm|message) me on (snap|kik|telegram|insta|tiktok)|"
        r"(text|message) me (at|on) [+\d]|"
        r"give me your (number|phone|contact)|"
        r"let\'?s (talk|chat|move) (off|away from) (here|this app|discord|forj))\b",
        re.IGNORECASE,
    ), "platform_move", 2),

    # Age-incongruent flattery / grooming language
    (re.compile(
        r"\b(mature for (your age|[0-9]+)|"
        r"not like (other )?(kids?|boys?|girls?|teens?|teenagers?)|"
        r"so (special|unique|different)|"
        r"only (one|person|girl|boy) (who|that) (gets?|understands?|gets? me)|"
        r"(you\'?re|you are) (really |so )?(beautiful|handsome|cute|hot|sexy) for (your age|a [0-9]+))\b",
        re.IGNORECASE,
    ), "flattery", 1),

    # Explicit material requests
    (re.compile(
        r"\b(send (me )?(a )?(pic(ture)?s?|photo|nudes?|naked pic|selfie)|"
        r"show (me )?(your(self)?|your body|your (face|chest|anything))|"
        r"take (a )?(pic(ture)?|photo|naked|nude)|"
        r"(want|wanna) to see you)\b",
        re.IGNORECASE,
    ), "explicit_request", 3),
]

# Total accumulated severity before a conversation is frozen
FREEZE_THRESHOLD = 5


@dataclass
class AnalysisResult:
    flagged: bool
    patterns: list[str]      # category names that fired
    severity: int            # sum of severities for THIS message


def analyze_message(content: str) -> AnalysisResult:
    """
    Scan a single message for grooming indicators.
    Returns an AnalysisResult with flagged=True if any patterns matched.
    """
    found_patterns: list[str] = []
    total_severity = 0

    for pattern, name, severity in _PATTERNS:
        if pattern.search(content):
            found_patterns.append(name)
            total_severity += severity

    return AnalysisResult(
        flagged=bool(found_patterns),
        patterns=found_patterns,
        severity=total_severity,
    )


def build_reason(patterns: list[str]) -> str:
    """Human-readable freeze reason from pattern category names."""
    labels = {
        "age_probe":        "probing a minor's age",
        "secrecy":          "attempting to isolate the minor from their parents",
        "meetup":           "soliciting an in-person meeting with a minor",
        "platform_move":    "attempting to move the conversation off-platform to avoid monitoring",
        "flattery":         "using age-inappropriate flattery toward a minor",
        "explicit_request": "requesting explicit content from a minor",
    }
    parts = [labels.get(p, p) for p in patterns]
    return "; ".join(parts) if parts else "suspicious conversation patterns"
