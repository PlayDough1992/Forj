# Security Side-by-Side: Forj vs Discord

This comparison is written for practical decision-making, not hype.

It compares:
- Forj as it exists in this repository today
- Discord based on public behavior and broadly known platform characteristics

## Executive Summary

Forj is stronger where identity assurance and code transparency matter.
Discord is stronger today on security maturity at global operational scale.

If your top priority is verified-identity communities with open-source auditability, Forj has real advantages.
If your top priority is proven large-scale hardening out of the box, Discord is still ahead today.

## Safety Points System

Scoring model used in this document:
- Each security area has a weight (total weight = 100)
- Each platform gets a score from 0 to 10 in each area
- Weighted points formula: `(score / 10) x weight`
- Total Safety Points = sum of all weighted points

Interpretation guide:
- 90-100: Exceptional current security posture
- 75-89: Strong current security posture
- 60-74: Moderate, with notable hardening gaps
- Below 60: High risk for production use without major hardening

## Side-by-Side Security Comparison

| Area | Weight | Forj (0-10) | Discord (0-10) | Forj (Current) | Discord (General/Public) | Security Takeaway |
|---|---:|---:|---:|---|---|---|
| Identity assurance | 14 | 9.0 | 5.0 | KYC-first access model; API gated until approved | Broad accessibility; no mandatory KYC for normal use | Forj is stronger for anti-impersonation and trust-gated communities |
| Code transparency | 10 | 9.0 | 5.0 | Open-source client and server | Closed-source platform internals | Forj is stronger for independent auditing and contributor-driven hardening |
| Hosting and ops control | 8 | 6.0 | 8.0 | Official VPS-hosted deployment | Vendor-managed hosted platform | Discord is stronger today on operational maturity at scale |
| Transport defaults | 14 | 3.0 | 9.0 | Client currently points to insecure HTTP/WS endpoints in source defaults | Uses HTTPS/WSS in normal product operation | Discord is stronger today until Forj enforces secure transport defaults |
| Local credential handling | 12 | 2.0 | 7.0 | Remember-me currently stores plaintext password in local settings | Vendor implementation details not public; no claim made | Forj has a clear fix needed here |
| Auth lifecycle | 12 | 5.0 | 8.0 | JWT with token-version invalidation on password change | Mature session ecosystem; implementation details not fully public | Forj has good primitives, but needs short-lived tokens plus refresh rotation |
| Webhook and callback integrity | 8 | 7.0 | 8.0 | Signed callback validation present | Webhook support with mature ecosystem | Forj has a solid base here |
| Abuse resistance | 10 | 5.0 | 9.0 | Safety filtering and moderation controls exist | Mature anti-abuse systems at scale | Discord is stronger today on depth and scale |
| Security governance | 6 | 8.0 | 7.0 | Open issue and patch path in repo | Centralized vendor security process | Forj is stronger for transparent community governance |
| Attack surface certainty | 6 | 7.0 | 6.0 | Smaller product scope, easier to reason about | Larger ecosystem and wider integration surface | Forj can be easier to harden quickly at project scope |

### Safety Points Results (Current State)

Calculated weighted totals:
- Forj: `56.6 / 100`
- Discord: `73.0 / 100`

Result reading:
- Forj currently lands in the high-risk-to-moderate zone for production security posture and needs the listed hardening work.
- Discord currently lands in the moderate-to-strong zone due to hardened defaults and operational maturity.

Forj can move this score quickly by completing the critical checklist items (transport, secret handling, and token/session hardening).

### Projected Forj Score After M1 Fixes

This projection assumes M1 checklist items are fully implemented and validated:
- Enforce HTTPS and WSS only
- Remove insecure endpoint defaults
- Stop storing plaintext remembered passwords
- Require strong JWT secret from environment
- Add rate limits on auth endpoints

Estimated score lift by category:

| Category | Current | Projected After M1 | Weighted Delta |
|---|---:|---:|---:|
| Transport defaults (weight 14) | 3.0 | 8.0 | +7.0 |
| Local credential handling (weight 12) | 2.0 | 8.0 | +7.2 |
| Auth lifecycle and auth surface (weight 12) | 5.0 | 7.0 | +2.4 |
| Abuse resistance via auth rate limiting (weight 10) | 5.0 | 6.0 | +1.0 |

Projected total:
- Forj current: 56.6 / 100
- Estimated M1 uplift: +17.6
- Forj projected after M1: 74.2 / 100

Contributor impact message:
- Completing M1 can move Forj from high-risk-to-moderate into the strong range.
- This is one of the fastest ways for contributors to create measurable security value.
- Final score should be recalculated after implementation and test evidence.

## Honest Bottom Line

Forj is not currently strongest in every security category.

Forj is strongest today in:
- Verified-identity trust model
- Open-source auditability
- Contributor-visible security controls

Discord is strongest today in:
- Production security maturity at internet scale
- Hardened defaults and operational depth
- Broad anti-abuse and incident-response capability

## What Must Happen for Forj to Win More Security Categories

High-priority changes already identified in the security checklist:
- Enforce HTTPS and WSS only in production
- Remove insecure hardcoded endpoint defaults
- Stop storing plaintext remembered passwords
- Require JWT secret from environment and fail fast on weak/default values
- Add short-lived access tokens with rotating refresh tokens

After those are completed, Forj can credibly claim stronger security posture for high-trust communities in more categories, not just identity and transparency.

## Reference

- Security backlog and milestones: SECURITY_CHECKLIST.md
